/*
** ftpdata.c - Manage the data connection
**
** Copyright (c) 1999-2000 Peter Eriksson <pen@lysator.liu.se>
**
** This program is free software; you can redistribute it and/or
** modify it as you wish - as long as you don't claim that you wrote
** it.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#include "config.h"

#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "pftpd.h"

#include "plib/aalloc.h"
#include "plib/threads.h"
#include "plib/safeio.h"
#include "plib/safestr.h"
#include "plib/support.h"
#include "plib/timeout.h"

#define PASV_TIMEOUT 120

static void *
ftpdata_thread(void *vp)
{
    FTPCLIENT *fcp = (FTPCLIENT *) vp;
    int code;
    FTPDATA_RES *res;


    pthread_mutex_lock(&fcp->data->mtx);
    fcp->data->state = 1;
    pthread_mutex_unlock(&fcp->data->mtx);
    
    if (debug)
	fprintf(stderr, "ftpdata_thread: start\n");

    code = fcp->data->handler(fcp, fcp->data->vp);

    if (debug)
	fprintf(stderr, "ftpdata_thread: handler called\n");

    A_NEW(res);
    res->code = code;
    
    /* Signal that we are done */
    pthread_mutex_lock(&fcp->data->mtx);
    code = s_close(fcp->data->fd);
    if (code < 0)
	syslog(LOG_ERR, "ftpdata_thread: close: %m");
	    
    fcp->data->fd = -1;
    fcp->data->state = 3; 
    
    pthread_mutex_unlock(&fcp->data->mtx);
    pthread_cond_broadcast(&fcp->data->cv);
    
    switch (res->code)
    {
      case 0:
	break;
	
      case 226:
	fd_puts(fcp->fd, "226 Transfer complete.\n");
	break;
	
      case 425:
	fd_puts(fcp->fd, "425 Can not build data connection.\n");
	break;
	
      case 426:
	fd_puts(fcp->fd, "426 Connection closed; transfer aborted.\n");
	break;
	
      default:
	syslog(LOG_ERR, "Internal error: Unhandled FTPDATA exit code: %d", code);
	fd_printf(fcp->fd, "%d ???\n", code);
    }
    
    if (debug)
	fprintf(stderr, "ftpdata_thread: Stop\n");
    
    return res;
}



static pthread_mutex_t ftpdata_mtx;

void
ftpdata_init(void)
{
    pthread_mutex_init(&ftpdata_mtx, NULL);
}


/*
** XXX: Can only have one active FTPDATA process in use between
** XXX: bind() and connect() - per FTP listen source address
*/
FTPDATA *
ftpdata_start(FTPCLIENT *fcp,
	      const char *reason,
	      int (*handler)(FTPCLIENT *fcp, void *vp),
	      void *vp)
{
    int err, attempt;
    int one = 1;
    FTPDATA *fdp;
    struct sockaddr_gen sin;
    socklen_t slen;
    
    
    if (debug)
	fprintf(stderr, "ftpdata_start(\"%s\"): start\n",
		reason);
    
    /* Already have an active FTPDATA session? -> Fail */
    if (fcp->data != NULL)
    {
	syslog(LOG_ERR, "ftpdata_start: Multiple active FTPDATA sessions not supported");
	return NULL;
    }
    

    A_NEW(fdp);
    fdp->state = 0;
    fdp->handler = handler;
    fdp->vp = vp;

    fd_printf(fcp->fd,
	      "150 Opening %s mode data connection for %s.\n",
	      fcp->type == ftp_binary ? "BINARY" : "ASCII",
	      reason);

    if (fcp->pasv != -1)
    {
	/* Passive connection */

	if (debug)
	    fprintf(stderr, "ftpdata_start: Waiting for PASV connection\n");
	
	slen = sizeof(sin);
	fdp->fd = s_accept2(fcp->pasv, (struct sockaddr *) &sin, &slen, PASV_TIMEOUT);
	
	if (fdp->fd < 0)
	{
	    syslog(LOG_ERR, "ftpdata_start: accept(Passive FTP-DATA): %m");

	    s_close(fcp->pasv);
	    fcp->pasv = -1;
	    
	    a_free(fdp);
	    return NULL;
	}
	
	if (debug)
	{
	    char buf1[256];
	    
	    fprintf(stderr, "Got passive connection from:\n");
	    fprintf(stderr, "\tsin_family = %d\n", SGFAM(sin));
	    fprintf(stderr, "\tsin_addr   = %s\n",
		    s_inet_ntox(&sin, buf1, sizeof(buf1)));
	    fprintf(stderr, "\tsin_port   = %d\n",
		    ntohs(SGPORT(sin)));
	}
	
	/* Make sure the connection came from the same host as the
	   control connection was from */

	if (SGSIZE(sin) != SGSIZE(fcp->cp->rsin) ||
	    memcmp(SGADDRP(sin),
		   SGADDRP(fcp->cp->rsin),
		   SGSIZE(sin)) != 0)
	{
	    char buf1[256], buf2[256];
	    
	    syslog(LOG_WARNING,
		   "ftpdata_start: Foreign PASV FTP-DATA from %s:%d (control %s:%d) - rejected\n",
		   s_inet_ntox(&sin, buf2, sizeof(buf2)),
		   ntohs(SGPORT(sin)),
		   s_inet_ntox(&fcp->cp->rsin, buf1, sizeof(buf1)),
		   ntohs(SGPORT(fcp->cp->rsin)));

	    s_close(fdp->fd);
	    a_free(fdp);
	    return NULL;
	}

	/* Shut down the listening socket for the passive connection */
	s_close(fcp->pasv);
	fcp->pasv = -1;
    }
    else
    {
	/* Active connection */
	
	if (!SGPORT(fcp->port))
	{
	    syslog(LOG_ERR, "ftpdata_start: Internal Error - No FTP-DATA port assigned");
	    
	    a_free(fdp);
	    return NULL;
	}
	
	SGINIT(sin);
	slen = SGSOCKSIZE(sin);

	/* Get the local address that the FTP client connected to */
	if (getsockname(fcp->cp->fd, (struct sockaddr *) &sin, &slen) != 0)
	{
	    syslog(LOG_WARNING, "ftpdata_start: getsockname: %m");
	    a_free(fdp);
	    return NULL;
	}

	pthread_mutex_lock(&ftpdata_mtx);
	
	fdp->fd = rpa_rresvport(rpap, &sin);
	if (fdp->fd < 0)
	    fdp->fd = create_bind_socket(&sin, 0);

	if (fdp->fd < 0)
	{
	    syslog(LOG_ERR, "ftpdata_start: socket/bind(Active FTP-DATA): %m");
	    
	    pthread_mutex_unlock(&ftpdata_mtx);
	    a_free(fdp);
	    return NULL;
	}
	    
	if (s_connect(fdp->fd,
		      (struct sockaddr *) &fcp->port,
		      SGSOCKSIZE(fcp->port)) < 0)
	{
	    char buf1[256];
	    
	    
	    syslog(LOG_WARNING, "ftpdata_start: connect(%d,%s:%d): %m",
		   fdp->fd,
		   s_inet_ntox(&fcp->port, buf1, sizeof(buf1)),
		   ntohs(SGPORT(fcp->port)));
	    
	    s_close(fdp->fd);
	    pthread_mutex_unlock(&ftpdata_mtx);
	    
	    a_free(fdp);
	    return NULL;
	}
    }

    pthread_mutex_unlock(&ftpdata_mtx);
    
    if (setsockopt(fdp->fd, IPPROTO_TCP, TCP_NODELAY, (void *) &one, sizeof(one)))
	syslog(LOG_WARNING, "ftpdata_start: setsockopt(TCP_NODELAY): %m");
    
#ifdef IPTOS_THROUGHPUT
    {
	int tos = IPTOS_THROUGHPUT;
	if (setsockopt(fdp->fd, IPPROTO_IP, IP_TOS, (void *) &tos, sizeof(tos)))
	    syslog(LOG_WARNING, "ftpdata_start: setsockopt(IPTOS_THROUGHPUT): %m");
    }
#endif	
#ifdef TCP_NOPUSH
    if (setsockopt(fdp->fd, IPPROTO_TCP, TCP_NOPUSH, (void *) &one, sizeof(one)))
	syslog(LOG_WARNING, "ftpdata_start: setsockopt(TCP_NOPUSH): %m");
#endif
    
    fcp->data = fdp;
    
    err = pthread_create(&fdp->tid, NULL, ftpdata_thread, (void *) fcp);
    if (err)
    {
	syslog(LOG_ERR, "ftpdata_start: pthread_create: %s",
	       strerror(err));

	s_close(fdp->fd);
	a_free(fdp);
	return NULL;
    }
    
    return fdp;
}


int
ftpdata_state(FTPDATA *fdp)
{
    int state;

    
    if (!fdp)
	return -1;
    
    pthread_mutex_lock(&fdp->mtx);
    state = fdp->state;
    pthread_mutex_unlock(&fdp->mtx);

    return state;
}


void
ftpdata_wait(FTPDATA *fdp)
{
    if (!fdp)
	return;
    
    if (debug)
	fprintf(stderr, "ftpdata_wait(%p): Start\n", fdp);

    pthread_mutex_lock(&fdp->mtx);
    while (fdp->state <= 2)
	pthread_cond_wait(&fdp->cv, &fdp->mtx);
    pthread_mutex_unlock(&fdp->mtx);
    
    if (debug)
	fprintf(stderr, "ftpdata_wait(%p): Done\n", fdp);

}


int
ftpdata_join(FTPDATA *fdp)
{
    FTPDATA_RES *res;
    int code;
    

    if (debug)
	fprintf(stderr, "ftpdata_join(%p): Start\n", fdp);

    if (!fdp)
	return -1;

    pthread_mutex_lock(&fdp->mtx);
    if (fdp->state < 1 || fdp->state > 3)
    {
	syslog(LOG_ERR, "ftpdata_join: bad state: %d", fdp->state);
	pthread_mutex_unlock(&fdp->mtx);
	return -1;
    }
    
    while (fdp->state <= 2)
	pthread_cond_wait(&fdp->cv, &fdp->mtx);

    if (fdp->state == 3)
    {
	if (debug)
	    fprintf(stderr,
		    "ftpdata_join(%p): Waiting for thread to exit\n",
		    fdp);
	
	while ((code = pthread_join(fdp->tid, (void *) &res)) == EINTR)
	    ;
	
	if (code)
	    syslog(LOG_ERR,
		   "ftpdata_join(%p): pthread_join(): %s",
		   fdp,
		   strerror(code));
    }

    fdp->state = 4;
    pthread_mutex_unlock(&fdp->mtx);
    
    code = res->code;
    a_free(res);

    if (debug)
	fprintf(stderr, "ftpdata_join(%p): Done (code=%d)\n", fdp, code);

    return code;
}

void
ftpdata_abort(FTPDATA *fdp)
{
    if (debug)
	fprintf(stderr, "ftpdata_abort(%p): Start\n", fdp);

    if (!fdp)
	return;
        
    pthread_mutex_lock(&fdp->mtx);
    if (fdp->state < 2)
    {
	if (shutdown(fdp->fd, 2) < 0)
	    syslog(LOG_ERR, "ftpdata_abort(%p): shutdown(%d, 2): %m",
		   fdp, fdp->fd);
	
	fdp->state = 2;
	/* XXX: cancel thread also? */
    }
    pthread_mutex_unlock(&fdp->mtx);
    
    if (debug)
	fprintf(stderr, "ftpdata_abort(%p): Done\n", fdp);
}

	   
void
ftpdata_destroy(FTPDATA *fdp)
{
    void *res = NULL;
    int code, state;
    

    if (debug)
	fprintf(stderr, "ftpdata_destroy(%p): Start\n", fdp);

    if (!fdp)
	return;
    
    if (ftpdata_state(fdp) <= 2)
	ftpdata_abort(fdp);

    pthread_mutex_lock(&fdp->mtx);
    
    while (fdp->state <= 2)
	pthread_cond_wait(&fdp->cv, &fdp->mtx);

    if (fdp->state == 3)
    {
	if (debug)
	    fprintf(stderr,
		    "ftpdata_destroy(%p): Waiting for thread to exit\n",
		    fdp);
	
	while ((code = pthread_join(fdp->tid, &res)) == EINTR)
	    ;

	if (code)
	    syslog(LOG_ERR,
		   "ftpdata_destroy(%p): pthread_join(): %s",
		   fdp,
		   strerror(code));
    }

    a_free(fdp);

    if (debug)
	fprintf(stderr, "ftpdata_destroy(%p): Done\n", fdp);
}
