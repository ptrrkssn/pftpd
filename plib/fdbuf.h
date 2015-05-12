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

#ifndef PLIB_FDBUF_H
#define PLIB_FDBUF_H

#include "plib/threads.h"

#define FDBUF_INBUFSIZE 8192
#define FDBUF_OUTBUFSIZE 8192

#define FDF_CRLF  	0x0001
#define FDF_LINEBUF	0x0002
#define FDF_TELNET	0x0004


typedef struct
{
    pthread_mutex_t refcnt_lock;
    int refcnt;
    
    int flags;
    int fd;

    pthread_mutex_t in_lock;
    int ungetc;
    int in_start;
    int in_end;
    int inbufsize;
    unsigned char *inbuf;

    pthread_mutex_t out_lock;
    int lastc;
    int outbuflen;
    int outbufsize;
    unsigned char *outbuf;
} FDBUF;



extern FDBUF *
fd_create(int fd,
	  int flags);

extern void
fd_destroy(FDBUF *fdp);

extern int
fd_fill(FDBUF *fdp);

extern int
fd_flush(FDBUF *fdp);

extern int
fd_putc(FDBUF *fdp,
	int c);

extern int
fd_puts(FDBUF *fdp,
	const char *str);

extern int
fd_printf(FDBUF *fdp,
	  const char *fmt,
	  ...);

extern int
fd_getc(FDBUF *fdp);

extern int
fd_gets(FDBUF *fdp,
	char *buf,
	int bufsize);

#endif
