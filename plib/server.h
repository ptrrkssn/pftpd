/*
** server.h - IDENT TCP/IP socket server code
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

#ifndef PLIB_SERVER_H
#define PLIB_SERVER_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "plib/threads.h"
#include "plib/sockaddr.h"

struct server_info;

typedef struct
{
    int fd;
    struct sockaddr_gen rsin; /* Remote address */
    struct sockaddr_gen lsin; /* Local address */
    struct server_info *sp;
} CLIENT;

typedef struct server_info
{
    int fd;
    struct sockaddr_gen sin;
    void (*handler)(CLIENT *cp);

    pthread_mutex_t mtx;
    int state; /* 0 = not running, 1 = starting/started, 2 = stopping, 3 = failure */
    int error; /* Failure reason (errno) */
    pthread_t tid;
    
    int clients_max;
    int clients_cur;
    pthread_mutex_t clients_mtx;
    pthread_cond_t clients_cv;
    pthread_attr_t ca_detached;
} SERVER;


extern SERVER *
server_init(int fd,
	    const struct sockaddr_gen *sg,
	    int listen_backlog,
	    int max_clients,
	    void (*handler)(CLIENT *cp));

extern int
server_start(SERVER *sp);

extern int
server_stop(SERVER *sp);

extern int
server_run_all(SERVER *sp);

extern int
server_run_one(SERVER *sp,
	       int fd,
	       int nofork);

extern void
server_cancel(SERVER *sp);
     
#endif
