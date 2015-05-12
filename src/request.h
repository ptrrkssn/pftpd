/*
** request.h - Handle an FTP protocol request
**
** Copyright (c) 1997-1999 Peter Eriksson <pen@lysator.liu.se>
**
** This program is free software; you can redistribute it and/or
** modify it as you wish - as long as you don't claim that you wrote
** it.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#ifndef PFTPD_REQUEST_H
#define PFTPD_REQUEST_H

#include "plib/server.h"

extern int request_timeout;

extern void request_handler(CLIENT *cp);

#endif
