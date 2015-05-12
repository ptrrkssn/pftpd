/*
** xferlog.h - Transfer logging stuff
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

#ifndef PFTPD_XFERLOG_H
#define PFTPD_XFERLOG_H

extern char *xferlog_path;

extern void
xferlog_init(void);

extern void
xferlog_close(void);

extern void
xferlog_append(FTPCLIENT *fp,
	       int xfertime,
	       int nbytes,
	       const char *filename,
	       int direction);

#endif
