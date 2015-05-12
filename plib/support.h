/*
** support.h - Miscellaneous support functions.
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

#ifndef PLIB_SUPPORT_H
#define PLIB_SUPPORT_H

#include <sys/types.h>
#include <pwd.h>

extern char osinfo_build[];
extern char *osinfo_get(char *buf, int bufsize);


#define SOCKTYPE_NOTSOCKET 0
#define SOCKTYPE_LISTEN    1
#define SOCKTYPE_CONNECTED 2

#ifdef DEBUG
#ifndef PLIB_IN_SUPPORT_C
#define getpwnam #error
#define getpwnam_r #error
#define getpwuid #error
#define getpwuid_r #error
#define openlog #error
#endif
#endif

extern int
socktype(int fd);


extern int
s_getpwnam_r(const char *name,
	     struct passwd *pwd,
	     char *buffer, size_t bufsize,
	     struct passwd **result);

extern int
s_getpwuid_r(uid_t uid,
	     struct passwd *pwd,
	     char *buffer, size_t bufsize,
	     struct passwd **result);

extern void
s_openlog(const char *ident,
	  int logopt, int facility);

extern int
syslog_str2fac(const char *name);

extern int
syslog_str2lev(const char *name);

#ifndef PLIB_IN_SUPPORT_C
#ifndef LOG_DAEMON
#define LOG_DAEMON 0
#endif
#endif

#endif
