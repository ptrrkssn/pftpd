/*
** ident.h - IDENT (RFC1413) lookup functions.
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

#ifndef PLIB_IDENT_H
#define PLIB_IDENT_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "plib/threads.h"
#include "plib/sockaddr.h"

#ifndef IPPORT_IDENT
#define IPPORT_IDENT 113
#endif


typedef struct
{
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
    int status;
    char *opsys;
    char *charset;
    char *userid;
    char *error;
} IDENT;

extern int
ident_lookup(struct sockaddr_gen *remote,
	     struct sockaddr_gen *local,
	     int timeout,
	     IDENT *ip);

extern int
ident_lookup_fd(int fd,
		int timeout,
		IDENT *ip);

extern int
ident_get(IDENT *ip,
	  char **opsys,
	  char **charset,
	  char **id);

extern void
ident_destroy(IDENT *ip);

#endif
