/*
** daemon.h - Become a daemon, and support functions.
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

#ifndef PLIB_DAEMON_H
#define PLIB_DAEMON_H

extern void
become_daemon(void);

extern void
pidfile_create(const char *path);

extern int
unlimit_nofile(void);

#endif
