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

#ifndef PFTPD_SOCKET_H
#define PFTPD_SOCKET_H

#include "plib/sockaddr.h"

extern int
create_bind_socket(struct sockaddr_gen *sin, int port);

#endif
     
