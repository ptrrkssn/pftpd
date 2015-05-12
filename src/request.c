/*
** request.c - Handle an FTP protocol request
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

#include "config.h"

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifndef HAVE_THREADS
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#endif

#include <arpa/inet.h>
#include <signal.h>


#include "pftpd.h"

#include "plib/support.h"
#include "plib/safeio.h"
#include "plib/timeout.h"

int request_timeout = 15*60;  /* 15 minutes */


static void
send_timeout_message(int fd)
{
    /* XXX: Use FDBUF instead */
    
    char *msg = "421 Service not available, closing control connection.\r\n";
    
    s_write(fd, msg, strlen(msg));
}


static void
timeout_handler(void *vp)
{
    FTPCLIENT *fp = (FTPCLIENT *) vp;

    
    if (debug)
	fprintf(stderr, "timeout_handler(%08lx)\n", (unsigned long) vp);

    if (fp != NULL)
    {
	send_timeout_message(fp->cp->fd);
	shutdown(fp->cp->fd, 0);
    }
}


#ifndef HAVE_THREADS
static RETSIGTYPE
sigurg_handler(int sig)
{
    if (debug)
	fprintf(stderr, "SIGURG\n");
}
#endif


void
request_handler(CLIENT *cp)
{
    char buf[512];
    int err;
    size_t len;
    ssize_t pos;
    FTPCLIENT *fp;
    TIMEOUT *tp = NULL;
    

    if (debug)
	fprintf(stderr, "request_thread: fd#%d: start (client: %s:%d)\n",
		cp->fd,
		s_inet_ntox(&cp->rsin, buf, sizeof(buf)),
		ntohs(SGPORT(cp->rsin)));

#ifndef HAVE_THREADS
    s_signal(SIGURG, sigurg_handler);
#ifdef F_SETOWN
    if (fcntl(cp->fd, F_SETOWN, getpid()) == -1)
	syslog(LOG_ERR, "fcntl(F_SETOWN): %m");
#else
#ifdef SIOCSPGRP
    {
	int pid = getpid();

	if (ioctl(fileno(stdin), SIOCSPGRP, &pid) == -1)
	    syslog(LOG_ERR, "ioctl(SIOCSPGRP): %m");
    }
#endif
#endif
#endif
    
    fp = ftpcmd_create(cp);
    if (fp == NULL)
    {
	/* XXX: Print error message */
	return;
    }
    
    len = 0;
    pos = 0;
    if (request_timeout > 0)
	tp = timeout_create(request_timeout, timeout_handler, fp);

    ftpcmd_send_banner(fp);
    
    while (fd_gets(fp->fd, buf, sizeof(buf)) == 0)
    {
	if (ftpdata_state(fp->data) == 3)
	{
	    int code;
	
    
	    /* Harvest the FTP DATA thread */

	    code = ftpdata_join(fp->data);
	    
	    ftpdata_destroy(fp->data);
	    fp->data = NULL;
	}
	
	if (tp)
	    timeout_reset(tp, request_timeout);

	if (debug)
	    fprintf(stderr, "Got: %s\n", buf);
	
	err = ftpcmd_parse(fp, buf);
	if (err)
	    break;
    }

    if (tp)
	timeout_destroy(tp);
    
    if (debug)
	fprintf(stderr, "request_thread: fd#%d: terminating\n", fp->fd->fd);

    ftpcmd_destroy(fp);
}



