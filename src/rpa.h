/*
** rpa.c - Reserved Port Allocation
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

#ifndef RPA_H
#define RPA_H

#include "plib/sockaddr.h"

#ifndef PATH_PREFIX
#define PATH_PREFIX
#endif

#ifndef PATH_SYSCONFDIR
#define PATH_SYSCONFDIR PATH_PREFIX "/etc"
#endif

#ifndef PATH_LOCALSTATEDIR
#define PATH_LOCALSTATEDIR PATH_PREFIX "/var"
#endif

#ifndef PATH_RPAD_DIR
#define PATH_RPAD_DIR  PATH_LOCALSTATEDIR "/rpad"
#endif

typedef struct
{
    pthread_mutex_t mtx;
    int fd;
    char *service;
    int type; /* 1 = AF_UNIX, 2 = DOORS */
} RPA;


extern void rpa_init(const char *path);
extern RPA *rpa_open(const char *service);
extern void rpa_close(RPA *rp);
extern int  rpa_rresvport(RPA *rp, struct sockaddr_gen *sp);

#endif
