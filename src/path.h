/*
** path.h
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

#ifndef PFTPD_PATH_H
#define PFTPD_PATH_H

#include <sys/types.h>
#include "ftpcmd.h"

extern char *user_ftp_dir;
extern char *server_ftp_dir;


extern char *
path_mk(FTPCLIENT *fp,
	const char *arg,
	char *buf,
	size_t bufsize);

extern char *
path_v2r(const char *path,
	 char *buf,
	 size_t bufsize);

#endif
