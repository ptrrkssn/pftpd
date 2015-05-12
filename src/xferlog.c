/*
** xferlog.c - Transfer logging stuff
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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <fcntl.h>

#include "pftpd.h"

#include "plib/safeio.h"
#include "plib/safestr.h"
#include "plib/support.h"


char *xferlog_path = "/var/adm/xferlog";
static int xferlog_fd = -1;
static pthread_mutex_t xferlog_mtx;
static pthread_once_t xferlog_once = PTHREAD_ONCE_INIT;


static void
xferlog_lock_init(void)
{
    pthread_mutex_init(&xferlog_mtx, NULL);
}


void
xferlog_init(void)
{
    pthread_once(&xferlog_once, xferlog_lock_init);

    pthread_mutex_lock(&xferlog_mtx);
    
    if (xferlog_fd != -1)
	s_close(xferlog_fd);

    xferlog_fd = s_open(xferlog_path, O_WRONLY|O_CREAT|O_APPEND, 0644);
    pthread_mutex_unlock(&xferlog_mtx);
}
	     

void
xferlog_close(void)
{
    pthread_mutex_lock(&xferlog_mtx);
    s_close(xferlog_fd);
    xferlog_fd = -1;
    pthread_mutex_unlock(&xferlog_mtx);
}


void
xferlog_append(FTPCLIENT *fp,
	       int xfertime,		/* unsigned long ? */
	       int nbytes,		/* size_t ? */
	       const char *filename,
	       int direction)
{
    char buf[2048], ibuf[256], vbuf[256];
    time_t now;
    struct tm tmb;
    const char *hostname;
    int anonymous;
    

    /* XXX: Validate the user supplied parameters */

    
    hostname = s_inet_ntox(&fp->cp->rsin, ibuf, sizeof(ibuf));
    time(&now);

    anonymous = (s_strcasecmp(fp->user, "anonymous") == 0 ||
		 s_strcasecmp(fp->user, "ftp") == 0);
    
    if (localtime_r(&now, &tmb) == NULL ||
	strftime(vbuf, sizeof(vbuf), "%a %b %d %H:%M:%S %Y", &tmb) == 0)
    {
	syslog(LOG_ERR, "Internal Error: Invalid time (%lu) from time()",
	       (unsigned long) now);
	return;
    }

    if (s_snprintf(buf, sizeof(buf),
		   "%.24s %u %s %u %s %c %s %c %c %s ftp %d %s\n",
		   vbuf,
		   xfertime,
		   hostname,
		   nbytes,
		   filename,
		   fp->type == ftp_ascii ? 'a' : 'b',
		   "_", /* options? */
		   direction == 1 ? 'o' : 'i',
		   anonymous ? 'a' : 'r', /* 'g' = guest */
		   anonymous ? fp->pass : "*",
		   0, "*") < 0)
    {
	syslog(LOG_WARNING, "%s: xferlog: buffer overflow\n",
	       hostname);
	return;
    }

    pthread_mutex_lock(&xferlog_mtx);
    if (xferlog_fd != -1)
	if (s_write(xferlog_fd, buf, strlen(buf)) < 0)
	{
	    syslog(LOG_ERR, "xferlog: write: %m");
	    s_close(xferlog_fd);
	    xferlog_fd = -1;
	}
    pthread_mutex_unlock(&xferlog_mtx);
}
	    
