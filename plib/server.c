/*
** server.c - TCP/IP socket server code
**
** Copyright (c) 1997-2000 Peter Eriksson <pen@lysator.liu.se>
**
** This program is free software; you can redistribute it and/or
** modify it as you wish - as long as you don't claim that you wrote
** it.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#include "plib/config.h"

#include <stdio.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef HAVE_THREADS
#include <sys/wait.h>
#endif

#include "plib/safeio.h"
#include "plib/safestr.h"
#include "plib/support.h"
#include "plib/aalloc.h"
#include "plib/sockaddr.h"

#include "plib/server.h"

int debug;



SERVER *
server_init(int fd,
	    const struct sockaddr_gen *sg,
	    int backlog,
	    int max_clients,
	    void (*handler)(CLIENT *cp))
{
    static int one = 1;
    socklen_t slen;
    SERVER *sp;
    

    if (debug)
	fprintf(stderr, "server_init(%d, ..., %d, %d, ...): Start\n",
		fd, backlog, max_clients);
    
    A_NEW(sp);

    sp->fd = -1;
    
    sp->state = 0;
    pthread_attr_init(&sp->ca_detached);
    pthread_attr_setdetachstate(&sp->ca_detached,
				PTHREAD_CREATE_DETACHED);
    
    pthread_mutex_init(&sp->clients_mtx, NULL);
    pthread_cond_init(&sp->clients_cv, NULL);
    sp->clients_cur = 0;
    sp->clients_max = max_clients;
    
    if (fd < 0)
    {
	if (sg == NULL)
	{
	    syslog(LOG_ERR,
		   "server_init: NULL address and no file descriptor");

	    server_destroy(sp);
	    if (debug)
		fprintf(stderr, "server_init(): End: Failure\n");
	    
	    return NULL;
	}
	
	sp->sin = *sg;
	
	sp->fd = socket(SGFAM(sp->sin), SOCK_STREAM, 0);
#ifdef HAVE_IPV6
	if (sp->fd < 0 &&
	    (errno == EAFNOSUPPORT || errno == EPFNOSUPPORT) &&
	    SGFAM(sp->sin) == AF_INET6)
	{
#if 0
	    /* Try to convert to IPv4 format... */
	    struct in6_addr *addr6 = (struct in6_addr *) SGADDRP(sp->sin);
	    
	    if (IN6_IS_ADDR_V4MAPPED(addr6))
	    {
		UINT32 addr4 = addr6->s6_addr32[3];
		
		SGFAM(sp->sin) = AF_INET;
		* (UINT32 *) SGADDRP(sp->sin) = addr4;
	    }
#endif

	    /* Let's try with an IPv4 socket - who knows, it might work */
	    errno = 0;
	    sp->fd = socket(PF_INET, SOCK_STREAM, 0);
	}
#endif
	
	if (sp->fd < 0)
	{
	    syslog(LOG_ERR, "socket(%s, SOCK_STREAM) failed (errno=%d): %m",
		   (SGFAM(sp->sin) == AF_INET ? "AF_INET" : "AF_INET6"),
		   errno);

	    server_destroy(sp);
	    if (debug)
		fprintf(stderr, "server_init(): End: Failure\n");
	    return NULL;
	}

	(void) setsockopt(sp->fd, SOL_SOCKET, SO_REUSEADDR,
			  (void *) &one, sizeof(one));

	if (s_bind(sp->fd, (struct sockaddr *) &sp->sin,
		   SGSOCKSIZE(sp->sin)) < 0)
	{
	    char buf1[16];

	    syslog(LOG_ERR, "bind(%d,%s:%d) failed: %m",
		   sp->fd,
		   s_inet_ntox(&sp->sin, buf1, sizeof(buf1)),
		   ntohs(SGPORT(sp->sin)));
	    
	    server_destroy(sp);
	    if (debug)
		fprintf(stderr, "server_init(): End: Failure\n");
	    return NULL;
	}
    }
    else
    {
	sp->fd = fd;
	slen = sizeof(sp->sin);
	getsockname(sp->fd, (struct sockaddr *) &sp->sin, &slen);
    }

    /* We do this outside the 'if' clause to support broken
       Inetd implementations */
    
    if (backlog >= 0 && listen(sp->fd, backlog) < 0)
    {
	syslog(LOG_ERR, "listen(%d, %d) failed: %m", sp->fd, backlog);
	
	server_destroy(sp);
	if (debug)
	    fprintf(stderr, "server_init(): End: Failure\n");
	return NULL;
    }
	
    sp->handler = handler;
    
    if (debug)
	fprintf(stderr, "server_init(): End: OK\n");
    return sp;
}

int
server_destroy(SERVER *sp)
{
    server_stop(sp);

    pthread_attr_destroy(&sp->ca_detached);
    pthread_mutex_destroy(&sp->clients_mtx);
    pthread_cond_destroy(&sp->clients_cv); 

    if (sp->fd >= 0)
	s_close(sp->fd);

    a_free(sp);
}

static RETSIGTYPE
sigusr1_handler(int sig)
{
    /* Do nothing */
}


static void *
server_thread(void *vp)
{
    SERVER *sp = (SERVER *) vp;


    if (debug)
	fprintf(stderr, "server_thread: Started.\n");
    
    s_signal(SIGUSR1, sigusr1_handler);
    
    server_run_all(sp);

    pthread_mutex_lock(&sp->mtx);
    sp->state = 0;
    pthread_mutex_unlock(&sp->mtx);

    if (debug)
	fprintf(stderr, "server_thread: Terminating.\n");
    
    return NULL;
}



int
server_stop(SERVER *sp)
{
    int code;
    void *res;
    
    
    pthread_mutex_lock(&sp->mtx);
    if (sp->state < 1 || sp->state > 2)
    {
	pthread_mutex_unlock(&sp->mtx);
	return -1;
    }
    
    sp->state = 2;
    
    shutdown(sp->fd, 2);
    pthread_kill(sp->tid, SIGUSR1); /* Some other signal? */
    pthread_mutex_unlock(&sp->mtx);

    while ((code = pthread_join(sp->tid, (void *) &res)) == EINTR)
	;

    return 0;
}

int
server_start(SERVER *sp)
{
    int code;


    pthread_mutex_lock(&sp->mtx);
    if (sp->state != 0)
    {
	pthread_mutex_unlock(&sp->mtx);
	syslog(LOG_ERR, "server_start: Invalid state (%d) - should be zero", sp->state);
	return -1;
    }

    pthread_mutex_unlock(&sp->mtx);
    
    code = pthread_create(&sp->tid, NULL, server_thread, (void *) sp);
    if (code)
    {
	syslog(LOG_ERR, "pthread_create(server_thread) failed: %m");
	if (debug)
	    fprintf(stderr, "server_start(...): End: Failure\n");
	return -1; /* XXX: abort? */
    }

    if (debug)
	fprintf(stderr, "server_start(...): End: OK\n");
    return 0;
}


static void *
client_thread(void *vp)
{
    CLIENT *cp = (CLIENT *) vp;
    SERVER *sp = cp->sp;
    
    sp->handler(cp);
    
    s_close(cp->fd);
    
#ifdef HAVE_THREADS
    if (sp->clients_max > 0)
    {
	pthread_mutex_lock(&sp->clients_mtx);
	if (sp->clients_cur == sp->clients_max)
	    pthread_cond_signal(&sp->clients_cv);
	
	sp->clients_cur--;
	pthread_mutex_unlock(&sp->clients_mtx);
    }
#endif

    a_free(cp);
    return NULL;
}

int
server_run_one(SERVER *sp,
	       int client_fd,
	       int nofork)
{
    CLIENT *cp;
    pthread_t tid;
#ifndef HAVE_THREADS
    pid_t pid;
    int status;
#endif
    int ecode;
    socklen_t slen;
    
    
    pthread_mutex_lock(&sp->clients_mtx);
    sp->clients_cur++;
    if (debug)
	fprintf(stderr, "server_run_one: Number of clients is now: %d\n",
		sp->clients_cur);
    
    pthread_mutex_unlock(&sp->clients_mtx);
    
    if (debug)
	fprintf(stderr, "server_run_one(..., %d, %d): Start\n", client_fd, nofork);
    
    A_NEW(cp);
    cp->fd = client_fd;
    cp->sp = sp;
    
    slen = sizeof(cp->rsin);
    if (getpeername(cp->fd,
		    (struct sockaddr *) &cp->rsin, &slen) < 0)
    {
	syslog(LOG_ERR, "getpeername(%d): %m", cp->fd);
	if (debug)
	    fprintf(stderr, "server_run_one(...): End: FAILURE\n");
	return EXIT_FAILURE;
    }
    
    slen = sizeof(cp->lsin);
    if (getsockname(cp->fd,
		    (struct sockaddr *) &cp->lsin, &slen) < 0)
    {
	syslog(LOG_ERR, "getsockname(%d): %m", cp->fd);
	if (debug)
	    fprintf(stderr, "server_run_one(...): End: FAILURE\n");
	return EXIT_FAILURE;
    }
    
    if (nofork)
    {
	(void) client_thread(cp);
	if (debug)
	    fprintf(stderr, "server_run_one(...): End: OK\n");
	return EXIT_SUCCESS;
    }
    else
    {
#ifdef HAVE_THREADS
	ecode = pthread_create(&tid,
			       &sp->ca_detached,
			       client_thread, (void *) cp);
	if (ecode)
	{
	    syslog(LOG_ERR, "pthread_create(client_thread) failed: %s",
		   strerror(ecode));
	    if (debug)
		fprintf(stderr, "server_run_one(...): End: FAILURE\n");
	    return EXIT_FAILURE;
	}

#else
	
	/* Try to reap the status of as many subprocesses as possible */
	
	/*
	** XXX: This will break if we are using multiple
	**      SERVER's in a single process and aren't using
	**      threads.
	*/
	while (sp->clients_cur > 0 &&
	       (pid = waitpid((pid_t) -1, &status, WNOHANG)) > 0)
	{
	    if (WIFEXITED(status) || WIFSIGNALED(status))
	    {
		sp->clients_cur--;
	    }
	}

	if (sp->clients_max > 0)
	{
	    /* Wait for atleast one slot to be available */
	    while (sp->clients_cur >= sp->clients_max &&
		   (pid = waitpid((pid_t) -1, &status, 0)) > 0)
	    {
		if (WIFEXITED(status) || WIFSIGNALED(status))
		{
		    sp->clients_cur--;
		}
	    }
	}
	
	while ((status = fork()) < 0 && errno == EAGAIN)
	{
	    /* Fork failed - too many processes */
	    sleep(1);

	    pid = waitpid((pid_t) -1, &status,
			  (sp->clients_cur > 0) ? 0 : WNOHANG);
		
	    if (pid > 0 && (WIFEXITED(status) || WIFSIGNALED(status)))
	    {
		sp->clients_cur--;
	    }
	}
	      
	if (status < 0)
	{
	    syslog(LOG_ERR, "fork() failed: %m");
	    s_abort();
	}
	
	if (status == 0)
	{
	    /* In child process */
	    (void) client_thread(cp);

	    _exit(EXIT_SUCCESS);
	}

	sp->clients_cur++;

	s_close(cp->fd);
	a_free(cp);
#endif
    }

    if (debug)
	fprintf(stderr, "server_run_one(...): End: OK\n");
    
    return EXIT_SUCCESS;
}



int
server_run_all(SERVER *sp)
{
    int fd, state;


    if (debug)
	fprintf(stderr, "server_run_all(...): Start\n");
    

    pthread_mutex_lock(&sp->mtx);
    if (sp->state != 0)
    {
	if (debug)
	    fprintf(stderr, "server_run_all(...): invalid initial state (%d)\n", state);
	sp->state = 3;
	sp->error = EINVAL;
	pthread_mutex_unlock(&sp->mtx);
	return -1;
    }
    
    sp->state = 1;
    
    while (sp->state == 1)
    {
	pthread_mutex_unlock(&sp->mtx);
#ifdef HAVE_THREADS
	if (sp->clients_max > 0)
	{
	    pthread_mutex_lock(&sp->clients_mtx);
	    while (sp->clients_cur >= sp->clients_max)
	    {
		if (debug)
		    fprintf(stderr, "server_run_all: Too many clients (%d > %d) - waiting for a slot\n",
			    sp->clients_cur, sp->clients_max);
    
		pthread_cond_wait(&sp->clients_cv, &sp->clients_mtx);
	    }
	    
	    pthread_mutex_unlock(&sp->clients_mtx);
	}
#endif
	
#undef accept	
	while ((fd = accept(sp->fd, NULL, NULL)) < 0 && errno == EINTR)
	{
	    pthread_mutex_lock(&sp->mtx);
	    sp->error = errno;
	    state = sp->state;
	    pthread_mutex_unlock(&sp->mtx);
	    
	    if (state != 1)
	    {
		if (debug)
		    fprintf(stderr, "server_run_all(...): Switching to state: %d\n", state);
		return state;
	    }
	    
	}
	
	if (fd < 0)
	{
	    syslog(LOG_ERR, "accept() failed: %m");
	    
	    switch (errno)
	    {
	      case EBADF:
	      case EMFILE:
	      case ENODEV:
	      case ENOMEM:
	      case ENOTSOCK:
	      case EOPNOTSUPP:
	      case EWOULDBLOCK:
	      case EINVAL:
	      case ENFILE:
		sp->error = errno;
		state = 3;
		pthread_mutex_unlock(&sp->mtx);
		return -1;
	    }
	}
	else
	    server_run_one(sp, fd, 0);
	
	pthread_mutex_lock(&sp->mtx);
    }

    if (debug)
	fprintf(stderr, "server_run_all(...): Terminating with state %d\n", sp->state);
    
    pthread_mutex_unlock(&sp->mtx);

    return -1;
}
