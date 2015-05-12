/*
** pftpd.h - Definitions needing global visibility
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

#ifndef PFTPD_H
#define PFTPD_H


#include <sys/types.h>

#if 0
#include "plib/threads.h"
#include "plib/fdbuf.h"
#include "plib/daemon.h"
#include "plib/server.h"
#include "plib/safeio.h"
#include "plib/support.h"
#include "plib/str2.h"
#include "plib/timeout.h"
#include "plib/system.h"
#endif

#include "request.h"
#include "conf.h"
#include "path.h"
#include "ftpcmd.h"
#include "ftplist.h"
#include "ftpdata.h"
#include "xferlog.h"
#include "socket.h"
#include "rpa.h"


#ifndef PATH_PREFIX
#define PATH_PREFIX 
#endif

#ifndef PATH_SYSCONFDIR
#define PATH_SYSCONFDIR PATH_PREFIX "/etc"
#endif

#ifndef PATH_LOCALSTATEDIR
#define PATH_LOCALSTATEDIR PATH_PREFIX "/var"
#endif

#ifndef PATH_CFGFILE
#define PATH_CFGFILE PATH_SYSCONFDIR "/pftpd.conf"
#endif

#ifndef PATH_PIDFILE
#define PATH_PIDFILE PATH_LOCALSTATEDIR "/run/pftpd.pid"
#endif

#ifndef PATH_DATADIR
#define PATH_DATADIR PATH_PREFIX "/share"
#endif

#ifndef PATH_FTPDIR
#define PATH_FTPDIR PATH_DATADIR "/ftp"
#endif


#define NO_PID ((pid_t) -1)
#define INIT_PID 1

#define NO_UID ((uid_t) -1)
#define ROOT_UID 0
#define ROOT_GID 0

extern int debug;
extern uid_t server_uid;
extern gid_t server_gid;
extern char *argv0;

extern int listen_sock;
extern int listen_port;
extern struct sockaddr_gen listen_addr;
extern int listen_backlog;

extern int requests_max;

extern char *server_root_dir;
extern char *pidfile_path;
extern char server_version[];
extern int socket_type;

extern int skip_dotfiles;

extern RPA *rpap;

#endif
