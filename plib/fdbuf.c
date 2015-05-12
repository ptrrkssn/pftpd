/*
** fdbuf.c - Buffering functions for file descriptors
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
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif

#include <arpa/telnet.h>


#include "plib/safeio.h"
#include "plib/safestr.h"
#include "plib/fdbuf.h"
#include "plib/aalloc.h"

#define inline


/*
** Setup a new buffer. Never fails.
*/
FDBUF *
fd_create(int fd,
	  int flags)
{
    FDBUF *fdp;


    A_NEW(fdp);

    pthread_mutex_init(&fdp->refcnt_lock, NULL);
    fdp->refcnt = 1;
    
    fdp->flags = flags;
    fdp->fd = fd;

    pthread_mutex_init(&fdp->in_lock, NULL);
    fdp->ungetc = -1;
    
    fdp->in_start = 0;
    fdp->in_end = 0;
    fdp->inbufsize = FDBUF_INBUFSIZE;
    fdp->inbuf = a_malloc(fdp->inbufsize, "FDBUF inbuf");

    pthread_mutex_init(&fdp->out_lock, NULL);
    fdp->lastc = -1;
    
    fdp->outbuflen = 0;
    fdp->outbufsize = FDBUF_OUTBUFSIZE;
    fdp->outbuf = a_malloc(fdp->outbufsize, "FDBUF outbuf");

    return fdp;
}


/*
** Free and destroy the buffer. Never fails.
*/
void
fd_destroy(FDBUF *fdp)
{
    if (fdp == NULL)
	return;
    
    fd_flush(fdp);

    pthread_mutex_lock(&fdp->refcnt_lock);
    fdp->refcnt--;
    if (fdp->refcnt > 0)
    {
	pthread_mutex_unlock(&fdp->refcnt_lock);
	return;
    }
	
    a_free(fdp->outbuf);
    a_free(fdp->inbuf);
    a_free(fdp);
}



/*
** Load more data into the buffer
*/
static inline int
_fd_fill(FDBUF *fdp)
{
    int maxlen, len, avail, err;


    if (fdp->in_start == fdp->in_end)
	fdp->in_start = fdp->in_end = 0;
	    
    /* Free space in the input buffer */
    maxlen = fdp->inbufsize - fdp->in_end;
    if (maxlen == 0)
	return 0;

    avail = 0;
    err = ioctl(fdp->fd, FIONREAD, &avail);

    if (err < 0 || avail == 0)
	avail = 1;

    if (avail > maxlen)
	avail = maxlen;
    
    len = s_read(fdp->fd, fdp->inbuf+fdp->in_start, avail);
    
    if (len == 0)
	return -1;
    else if (len < 0)
    {
	return errno;
    }

    fdp->in_end += len;
    return 0;
}

int
fd_fill(FDBUF *fdp)
{
    int err;
    
    pthread_mutex_lock(&fdp->in_lock);
    err = _fd_fill(fdp);
    pthread_mutex_unlock(&fdp->in_lock);

    return err;
}


/*
** Send any buffered data
*/
static inline int
_fd_flush(FDBUF *fdp)
{
    int len, rest;


    if (fdp->outbuflen == 0)
	return 0;

    len = s_write(fdp->fd, fdp->outbuf, fdp->outbuflen);
    if (len < 0)
	return errno;

    if (len == 0)
	return 0;

    rest = fdp->outbuflen - len;
    if (rest > 0)
	memcpy(fdp->outbuf, fdp->outbuf+len, rest);
    fdp->outbuflen -= len;
    return 0;
}


int
fd_flush(FDBUF *fdp)
{
    int err;

    
    pthread_mutex_lock(&fdp->out_lock);
    err = _fd_flush(fdp);
    pthread_mutex_unlock(&fdp->out_lock);
    
    return err;
}


void
fd_purge(FDBUF *fdp)
{
    pthread_mutex_lock(&fdp->out_lock);
    fdp->outbuflen = 0;
    fdp->lastc = -1;
    pthread_mutex_unlock(&fdp->out_lock);
}    


/*
** Send one character
*/
static inline int
_fd_putc(FDBUF *fdp,
	 int c)
{
    int err;

    
    if ((fdp->flags & FDF_CRLF) && c == '\n')
	if ((err = _fd_putc(fdp, '\r')) != 0)
	    return err;

    if (fdp->outbuflen == fdp->outbufsize)
    {
	_fd_flush(fdp);
	
	if (fdp->outbuflen == fdp->outbufsize)
	    return EAGAIN;
    }

    fdp->outbuf[fdp->outbuflen++] = c;
    if ((fdp->flags & FDF_LINEBUF) && c == '\n')
	_fd_flush(fdp);
    
    return 0;
}

int
fd_putc(FDBUF *fdp,
	int c)
{
    int err;

    pthread_mutex_lock(&fdp->out_lock);
    err = _fd_putc(fdp, c);
    pthread_mutex_unlock(&fdp->out_lock);

    return err;
}


/*
** Send a string
*/
int
fd_puts(FDBUF *fdp,
	const char *str)
{
    int err = 0;


    pthread_mutex_lock(&fdp->out_lock);
    while (*str && err == 0)
	err = _fd_putc(fdp, *str++);
    pthread_mutex_unlock(&fdp->out_lock);
    
    return err;
}



/*
** Send a formatted string
*/
int
fd_printf(FDBUF *fdp,
	  const char *fmt,
	  ...)
{
    va_list ap;
    char buf[8192];
    int ecode;
    
    
    va_start(ap, fmt);
    ecode = s_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (ecode < 0 || ecode >= sizeof(buf))
	return EINVAL;

    return fd_puts(fdp, buf);
}



/*
** Unget a character
*/
int
fd_ungetc(FDBUF *fdp,
	  int c)
{
    if (fdp->ungetc != -1)
	return ENOMEM;

    fdp->ungetc = c;
    return 0;
}

/*
** Get a character from the buffer
*/
static inline int
_fd_rgetc(FDBUF *fdp)
{
    if (fdp->in_start == fdp->in_end && _fd_fill(fdp))
	return -1;
    
    return fdp->inbuf[fdp->in_start++];
}

int
fd_rgetc(FDBUF *fdp)
{
    int c;

    pthread_mutex_lock(&fdp->in_lock);
    c = _fd_rgetc(fdp);
    pthread_mutex_unlock(&fdp->in_lock);
    return c;
}


static inline int
_fd_getc(FDBUF *fdp)
{
    int c;


    if (fdp->ungetc != -1)
    {
	c = fdp->ungetc;
	fdp->ungetc = -1;
	return c;
    }
    
  Again:
    c = _fd_rgetc(fdp);
    if (c == EOF)
	return EOF;

    /* Simple, basic TELNET protocol handling */
    if ((fdp->flags & FDF_TELNET) && c == IAC)
    {
	c = _fd_rgetc(fdp);
	switch (c)
	{
	  case EOF:
	    return EOF;

	  case AO:
	    fd_purge(fdp);
	    goto Again;
	    
	  case AYT:
	    fd_puts(fdp, "\n[Yes]\n");
	    fd_flush(fdp);
	    goto Again;

	  case NOP:
	    goto Again;

	  case IP: /* Interrupt process */
	    /* XXX: What should we do? */
	    goto Again;

	  case SYNCH: /* Synchronize */
	    return -2;
	    
	  case WILL:
	  case WONT:
	    c = _fd_rgetc(fdp);
	    if (c == EOF)
		return EOF;
	    pthread_mutex_lock(&fdp->out_lock);
	    _fd_putc(fdp, IAC);
	    _fd_putc(fdp, DONT);
	    _fd_putc(fdp, c);
	    _fd_flush(fdp);
	    pthread_mutex_unlock(&fdp->out_lock);
	    goto Again;

	  case DO:
	  case DONT:
	    c = _fd_rgetc(fdp);
	    if (c == EOF)
		return EOF;
	    pthread_mutex_lock(&fdp->out_lock);
	    _fd_putc(fdp, IAC);
	    _fd_putc(fdp, WONT);
	    _fd_putc(fdp, c);
	    _fd_flush(fdp);
	    pthread_mutex_unlock(&fdp->out_lock);
	    goto Again;

	  default:
	    goto Again;
	}
    }
	
    if ((fdp->flags & FDF_CRLF) && fdp->lastc == '\r' && c == '\n')
	goto Again;
    
    fdp->lastc = c;
    if ((fdp->flags & FDF_CRLF) && c == '\r')
	c = '\n';

    return c;
}


int
fd_getc(FDBUF *fdp)
{
    int c;


    pthread_mutex_lock(&fdp->in_lock);
    
    while ((c = _fd_getc(fdp)) == -2)
	;
    
    pthread_mutex_unlock(&fdp->in_lock);

    return c;
}


/*
** Get a line from the buffer
*/
int
fd_gets(FDBUF *fdp,
	char *buf,
	int bufsize)
{
    int c, i;


    if (bufsize == 0 || !buf)
	return -1;

    pthread_mutex_lock(&fdp->in_lock);
    
    i = 0;
    while ((c = _fd_getc(fdp)) != -1 &&
	   c != '\n' && c != '\r' &&
	   (i < (bufsize-1)))
    {
	if (c == -2) /* TELNET SYNCH */
	    i = 0;
	else
	    buf[i++] = c;
    }
    buf[i] = '\0';
    
    pthread_mutex_unlock(&fdp->in_lock);
    
    if (i == 0 && c == -1)
	return -1;

    if (i == (bufsize-1) && !(c == '\n' || c == '\r' || c == -1))
	return -1;
    
    return 0;
}
