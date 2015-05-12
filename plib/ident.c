/*
** ident.c - IDENT (RFC1413) lookup functions.
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

#include "plib/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "plib/safeio.h"
#include "plib/safestr.h"
#include "plib/fdbuf.h"
#include "plib/timeout.h"
#include "plib/aalloc.h"

#include "plib/ident.h"


struct ident_lookup
{
    struct sockaddr_gen remote;
    struct sockaddr_gen local;
    IDENT *ip;
    int timeout;
};


static char *
strtrim(char *s)
{
    char *start;

    if (s == NULL)
	return NULL;
    
    while (*s == ' ' || *s == '\t')
	++s;
    
    start = s;
    
    while (*s)
	++s;
    while (s > start && (s[-1] == ' ' || s[-1] == '\t'))
	--s;
    *s = '\0';

    return start;
}


static void
timeout_handler(void *vp)
{
    int fd = * (int *) vp;

    shutdown(fd, 2);
}


static void *
ident_thread(void *vp)
{
    const static int one = 1;

    struct ident_lookup *ilp = (struct ident_lookup *) vp;
    IDENT *ip = ilp->ip;
    int fd, err, r_port, l_port, r_r_port, r_l_port;
    struct sockaddr_gen ident_sin, our_sin;
    FDBUF *fdp;
    char buf[1024], *tok, *cp;
    TIMEOUT *tp = NULL;
    

    fdp = NULL;
    err = 0;
    fd = -1;
    
    ident_sin = ilp->remote;
    SGPORT(ident_sin) = htons(IPPORT_IDENT);

    our_sin = ilp->local;
    SGPORT(our_sin) = 0;

    r_port = ntohs(SGPORT(ilp->remote));
    l_port = ntohs(SGPORT(ilp->local));
    
    fd = socket(SGFAM(our_sin), SOCK_STREAM, 0);
    if (fd < 0)
    {
	err = errno;
	goto Exit;
    }

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *) &one, sizeof(one));

    if (ilp->timeout)
	tp = timeout_create(ilp->timeout, timeout_handler, &fd);
    
    if (s_bind(fd, (struct sockaddr *) &our_sin, SGSOCKSIZE(our_sin)) < 0)
    {
	err = errno;
	goto Exit;
    }

    if (s_connect(fd, (struct sockaddr *) &ident_sin, SGSOCKSIZE(ident_sin)) < 0)
    {
	err = errno;
	goto Exit;
    }

    fdp = fd_create(fd, FDF_CRLF);

    err = fd_printf(fdp, "%d , %d\n", r_port, l_port);
    if (err)
	goto Exit;

    fd_flush(fdp);
    shutdown(fd, 1);
    
    err = fd_gets(fdp, buf, sizeof(buf));
    if (err)
	goto Exit;

    tok = s_strtok_r(buf, ":", &cp);
    tok = strtrim(tok);
    
    if (tok == NULL ||
	(err = sscanf(tok, " %d , %d", &r_r_port, &r_l_port)) != 2 ||
	r_r_port != r_port || r_l_port != l_port)
    {
	err = EINVAL;
	goto Exit;
    }

    tok = s_strtok_r(NULL, ":", &cp);
    tok = strtrim(tok);
    if (tok == NULL)
    {
	err = EINVAL;
	goto Exit;
    }
    
    if (s_strcasecmp(tok, "USERID") == 0)
    {
	char *p;
	
	tok = s_strtok_r(NULL, ":", &cp);
	tok = strtrim(tok);

	if (tok == NULL)
	{
	    err = EINVAL;
	    goto Exit;
	}
	
	p = strchr(tok, ',');
	if (p)
	{
	    *p++ = '\0';
	    
	    tok = strtrim(tok);
	    p = strtrim(p);
	    
	    ip->charset = a_strdup(p, "IDENT charset");
	}

	ip->opsys = a_strdup(tok, "IDENT opsys");

	tok = s_strtok_r(NULL, "\n\r", &cp);
	tok = strtrim(tok);
	if (tok == NULL)
	{
	    err = EINVAL;
	    goto Exit;
	}
	
	ip->userid = a_strdup(tok, "IDENT userid");
	err = 0;
    }
    
    else if (s_strcasecmp(tok, "ERROR") == 0)
    {
	tok = s_strtok_r(NULL, "\n\r", &cp);
	tok = strtrim(tok);
	if (tok == NULL)
	{
	    err = EINVAL;
	    goto Exit;
	}

	ip->error = a_strdup(tok, "IDENT error");
	err = -2;
    }
    
    else
	err = EINVAL;
	
  Exit:
    timeout_destroy(tp);
    fd_destroy(fdp);
    
    if (fd != -1)
	s_close(fd);
    
    pthread_mutex_lock(&ip->mtx);
    ip->status = err;
    pthread_mutex_unlock(&ip->mtx);
    pthread_cond_broadcast(&ip->cv);

    a_free(ilp);
    return NULL;
}



int
ident_lookup(struct sockaddr_gen *remote,
	     struct sockaddr_gen *local,
	     int timeout,
	     IDENT *ip)
{
    struct ident_lookup *ilp;
    int err;
    pthread_t tid;
    pthread_attr_t cattr;

    
    pthread_attr_init(&cattr);
    pthread_attr_setdetachstate(&cattr, PTHREAD_CREATE_DETACHED);
    
    A_NEW(ilp);
    
    ilp->remote = *remote;
    ilp->local = *local;
    ilp->ip = ip;
    ilp->timeout = timeout;
    
    pthread_mutex_init(&ip->mtx, NULL);
    pthread_cond_init(&ip->cv, NULL);
    ip->status = -1;
    ip->opsys = NULL;
    ip->charset = NULL;
    ip->userid = NULL;
    ip->error = NULL;

    err = pthread_create(&tid, &cattr, ident_thread, (void *) ilp);
    if (err)
	a_free(ilp);
    
    return err;
}


int
ident_lookup_fd(int fd,
		int timeout,
		IDENT *ip)
{
    struct sockaddr_gen remote;
    struct sockaddr_gen local;
    socklen_t slen;
    

    slen = sizeof(remote);
    if (getpeername(fd, (struct sockaddr *) &remote, &slen) < 0)
	return errno;

    slen = sizeof(local);
    if (getsockname(fd, (struct sockaddr *) &local, &slen) < 0)
	return errno;

    return ident_lookup(&remote, &local, timeout, ip);
}


int
ident_get(IDENT *ip,
	  char **opsys,
	  char **charset,
	  char **id)
{
    int err = 0;

    
    pthread_mutex_lock(&ip->mtx);
    while (ip->status == -1)
	pthread_cond_wait(&ip->cv, &ip->mtx);

    if (ip->status == 0)
    {
	if (opsys)
	    *opsys = ip->opsys;
	if (charset)
	    *charset = ip->charset;
	if (id)
	    *id = ip->userid;
    }
    else if (ip->status > 0)
	err = ip->status;
    else
	err = EINVAL;
    
    pthread_mutex_unlock(&ip->mtx);
    
    return err;
}



void
ident_destroy(IDENT *ip)
{
    /* XXX: Need to make sure the thread is dead! */
    
    pthread_mutex_destroy(&ip->mtx);
    pthread_cond_destroy(&ip->cv);
    a_free(ip->opsys);
    a_free(ip->charset);
    a_free(ip->userid);
    a_free(ip->error);
}
