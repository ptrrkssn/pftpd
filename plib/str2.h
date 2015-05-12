/*
** str2.h - String to Foo conversion routines.
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

#ifndef PLIB_STR2_H
#define PLIB_STR2_H

#include "plib/sockaddr.h"

extern int
str2int(const char *buf,
	int *out);

extern int
str2str(const char *buf,
	char **out);

extern int
str2bool(const char *buf,
	 int *out);

extern int
str2port(const char *str,
	 int *out);

extern int
str2addr(const char *str,
	 struct sockaddr_gen *sg);

extern int
str2gid(const char *str,
	gid_t *out);

extern int
str2uid(const char *str,
	uid_t *uid,
	gid_t *gid);

#endif

