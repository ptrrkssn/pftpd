/*
** socket.c - Socket handling routines
**
** Copyright (c) 2002 Peter Eriksson <pen@lysator.liu.se>
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

int
create_bind_socket(struct sockaddr_gen *sin,
		   int port)
{
    int fd, attempt;
    static int one = 1;
    char buf1[256];
    

    fd = socket(SGFAM(*sin), SOCK_STREAM, 0);
    if (fd < 0)
    {
	syslog(LOG_ERR, "socket() failed: %m");
	return -1;
    }
    
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *) &one, sizeof(one)))
	syslog(LOG_WARNING, "setsockopt(SO_REUSEADDR) failed (ignored): %m");
    
    SGPORT(*sin) = htons(port);

    syslog(LOG_DEBUG, "bind(%d,%s:%d) - before",
	   fd,
	   s_inet_ntox(sin, buf1, sizeof(buf1)),
	   ntohs(SGPORT(*sin)));

    attempt = 0;
    while (s_bind(fd, (struct sockaddr *) sin, SGSOCKSIZE(*sin)) < 0)
    {
	if (debug)
	{
	    fprintf(stderr,
		    "ftpdata_run: s_bind(): attempt=%d, addr=%s:%d, failed: %s\n",
		    attempt,
		    s_inet_ntox(sin, buf1, sizeof(buf1)),
		    ntohs(SGPORT(*sin)),
		    strerror(errno));
	}
	
	if (attempt++ > 10 || errno != EADDRINUSE)
	{
	    char buf1[256];
	    
	    
	    syslog(LOG_WARNING, "bind(%d,%s:%d) failed (final): %m",
		   fd,
		   s_inet_ntox(sin, buf1, sizeof(buf1)),
		   ntohs(SGPORT(*sin)));
	    
	    s_close(fd);
	    return -1;
	}
	else if (attempt == 1)
	    syslog(LOG_NOTICE, "bind(%d,%s:%d) failed (retrying): %m",
		   fd,
		   s_inet_ntox(sin, buf1, sizeof(buf1)),
		   ntohs(SGPORT(*sin)),
		   attempt);
	    
	
	sleep(1);
    }
    
    return fd;
}

