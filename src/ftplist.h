/*
** ftplist.h
**
** Copyright (c) 1999 Peter Eriksson <pen@lysator.liu.se>
**
** This program is free software; you can redistribute it and/or
** modify it as you wish - as long as you don't claim that you wrote
** it.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#ifndef PFTPD_FTPLIST_H
#define PFTPD_FTPLIST_H

#include "ftpcmd.h"

#define LS_ALL    	0x0001
#define LS_LONG   	0x0002
#define LS_FTYPE  	0x0004
#define LS_SKIPDOTDOT   0x0008

#define LS_FTPDATA	0x0100


extern int
ftplist_send(FTPCLIENT *fp,
	     const char *path,
	     int flags);

#endif
