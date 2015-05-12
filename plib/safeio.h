/*
** safeio.h - Signal/Async safe wrapper functions
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

#ifndef PLIB_SAFEIO_H
#define PLIB_SAFEIO_H

#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "plib/sockaddr.h"

typedef RETSIGTYPE (*SIGHANDLER_T)();

#ifdef  DEBUG
#ifndef PLIB_IN_SAFEIO_C
#define abort #error
#define open #error
#define write #error
#define read #error
#define pread #error
#define close #error
#define accept #error
#define random #error
#define signal #error
#define bind #error
#define connect #error
#define inet_ntoa #error
#endif
#endif

extern void
s_abort(void);

extern int
s_open(const char *path,
       int oflag,
       ...);

extern ssize_t
s_write(int fd,
	const void *buf,
	size_t len);

extern ssize_t
s_read(int fd,
       void *buf,
       size_t len);

extern ssize_t
s_pread(int fd,
	void *buf,
	size_t len,
	off_t off);

extern int
s_close(int fd);

extern int
s_accept2(int fd,
	  struct sockaddr *sa,
	  socklen_t *len,
	  int timeout);

extern int
s_accept(int fd,
	 struct sockaddr *sa,
	 socklen_t *len);


extern long
s_random(void);


extern SIGHANDLER_T
s_signal(int sig,
	 SIGHANDLER_T handler);

extern int
s_copyfd(int to_fd,
	 int from_fd,
	 off_t *start,
	 size_t len);

extern int
s_bind(int fd,
       struct sockaddr *sin,
       socklen_t len);

extern int
s_connect(int fd,
	  struct sockaddr *sin,
	  socklen_t len);

extern const char *
s_inet_ntox(struct sockaddr_gen *ia,
	    char *buf,
	    size_t bufsize);

#endif

