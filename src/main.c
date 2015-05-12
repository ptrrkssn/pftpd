/*
** main.c - Main entrypoint for Pftpd 1.0
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "pftpd.h"

#include "plib/aalloc.h"
#include "plib/safeio.h"
#include "plib/safestr.h"
#include "plib/petopt.h"
#include "plib/support.h"
#include "plib/timeout.h"
#include "plib/str2.h"
#include "plib/daemon.h"


#if defined(HAVE_LIBTHREAD) && defined(HAVE_THR_SETCONCURRENCY)
#  include <thread.h>
#endif

char *argv0 = "ftpd";

char *rpad_dir = NULL;
int debug = 0;
uid_t server_uid = NO_UID;
gid_t server_gid = ROOT_GID;
char *pidfile_path = PATH_PIDFILE;

char *server_root_dir = NULL;
int use_chroot = 0;

#ifndef IPPORT_FTP
#define IPPORT_FTP 21
#endif

int listen_sock = -1;
int listen_port = IPPORT_FTP;
struct sockaddr_gen listen_addr;
int listen_backlog = 256;

int requests_max = 0;
int skip_dotfiles = 1;

int socket_type = -1;
int init_mode = 0;

RPA *rpap = NULL;


static PETOPTS pov[] =
{
    {
	'h',
	POF_NONE,
	"HeLP",
	NULL,
	"Print help"
    },
    
    {
	'V',
	POF_NONE,
	"Version",
	NULL,
	"Print version"
    },
    
    {
	'd',
	POF_OPT|POF_INT,
	"DeBuG",
	&debug,
	"Enable debug mode"
    },
    
    {
	'w',
	POF_NONE,
	"inetd-WAIT-mode",
	NULL,
	"Start in inetd/wait mode"
    },
    
    {
	'i',
	POF_NONE,
	"Inetd-NOWAIT-mode",
	NULL,
	"Start in inetd/nowait mode"
    },
    
    {
	'I',
	POF_NONE,
	"Inittab-mode",
	NULL,
	"Start in inittab mode"
    },
    
    {
	'b',
	POF_NONE,
	"Daemon-mode",
	NULL,
	"Start in daemon background mode"
    },
    
    {
	'p',
	POF_STR,
	"Port",
	NULL,
	"TCP/IP listening port"
    },
    
    {
	'a',
	POF_STR,
	"Address",
	NULL,
	"TCP/IP listening address"
    },
    
    {
	't',
	POF_INT,
	"TimeOut",
	&request_timeout,
	"Request timeout"
    },
    
    {
	'g',
	POF_STR,
	"GRouP-ID",
	NULL,
	"Group"
    },
    
    {
	'u',
	POF_STR,
	"User-ID",
	NULL,
	"User"
    },
    
    {
	'L',
	POF_STR,
	"Syslog-facility",
	NULL,
	"Facility for system logging"
    },

    {
	'X',
	POF_STR,
	"Xferlog-file",
	&xferlog_path,
	"Path to transfer log file"
    },
    
    {
	'C',
	POF_STR,
	"ConFiG-file",
	NULL,
	"Configuration file"
    },

    {
	'W',
	POF_BOOL,
	"Read-Write",
	&readwrite_flag,
	"Enable Read/Write mode"
    },
    
    {
	'P',
	POF_STR,
	"Pid-file",
	&pidfile_path,
	"Where to store the PID"
    },

    {
	'R',
	POF_STR,
	"Root-directory",
	&server_root_dir,
	"Root directory for all I/O"
    },
    
    {
	'r',
	POF_STR,
	"RPAD-directory",
	&rpad_dir,
	"Directory for RPAD"
    },
    
    {
	'D',
	POF_STR,
	"Database-directory",
	&server_ftp_dir,
	"Top level directory"
    },
    
    {
	'U',
	POF_STR,
	"User-directory",
	&user_ftp_dir,
	"User's anonymous directory"
    },

    
    { -1, 0, NULL, NULL, NULL }
};

static void
program_header(FILE *fp)
{
    fprintf(fp, "[Pftpd, version %s - %s %s]\n",
	    server_version, 
	    __DATE__, __TIME__);
}



static void
usage(PETOPT *pp,
      FILE *fp)
{
    fprintf(fp, "Usage: %s [options]\n", argv0);

    fputs("\n\
This daemon implements a multithreaded Anonymous FTP server.\n\
More information may be found at:\n\
\thttp://www.lysator.liu.se/~pen/pftpd\n\
\n\
Command line options:\n", fp);
    
    petopt_print_usage(pp, fp);
    
    fputs("\n\
Long options are case insensitive, and may be abbreviated either by\n\
truncation or by using only the uppercase characters. '[ARG]' denotes\n\
an optional argument and 'ARG' denotes a required argument.\n", fp);
}


static int
parse_option(PETOPT *pp,
	     PETOPTS *pov,
	     const char *arg)
{
    int code;

    
    switch (pov->s)
    {
      case 'h':
	usage(pp, stdout);
	exit(EXIT_SUCCESS);
	    
      case 'w':
	listen_sock = STDIN_FILENO;
	socket_type = SOCKTYPE_LISTEN;
	break;
	
      case 'i':
	listen_sock = STDIN_FILENO;
	socket_type = SOCKTYPE_CONNECTED;
	break;
	
      case 'I':
	listen_sock = -1;
	socket_type = SOCKTYPE_NOTSOCKET;
	init_mode = 1;
	break;
	
      case 'b':
	listen_sock = -1;
	socket_type = SOCKTYPE_NOTSOCKET;
	break;
	
      case 'a':
	if (str2addr(arg, &listen_addr) < 0)
	    return POE_INVALID;
	if (SGPORT(listen_addr) != 0)
	    listen_port = ntohs(SGPORT(listen_addr));
	break;
	
      case 'p':
	if (str2port(arg, &listen_port) < 0)
	    return POE_INVALID;
	break;
	
      case 'g':
	if (str2gid(arg, &server_gid) < 0)
	    return POE_INVALID;
	break;
	
      case 'u':
	if (str2uid(arg, &server_uid, &server_gid) < 0)
	    return POE_INVALID;
	break;

      case 'L':
	code = syslog_str2fac(arg);
	if (code < 0)
	    return POE_INVALID;
	closelog();
	s_openlog(argv0, LOG_PID|LOG_ODELAY, code);
	break;
	
      case 'C':
	if (conf_parse(arg, 0) < 0)
	{
	    if (socket_type == -1 || socket_type == SOCKTYPE_NOTSOCKET)
		fprintf(stderr, "%s: error parsing config file: %s\n",
			argv0, arg);
	    exit(EXIT_FAILURE);
	}
	break;
	
      case 'V':
	program_header(stdout);
	exit(EXIT_SUCCESS);
	
      default:
	return 1;
    }

    return 0;
}


void
drop_root_privs(void)
{
    if (server_uid == NO_UID)
    {
	if (str2uid("ftp", &server_uid, &server_gid) < 0)
	    if (str2uid("nobody", &server_uid, &server_gid) < 0)
		server_uid = ROOT_UID;
    }
    
    if (server_gid != ROOT_GID)
	setgid(server_gid);
    
    if (server_uid != ROOT_UID)
	setuid(server_uid);
}    


static RETSIGTYPE
sighup_handler(int sig)
{
    s_signal(SIGHUP, sighup_handler);
    
    /* XXX: Should not do things like this from a signal handler... */
    xferlog_init();
}

     


int
main(int argc,
     char *argv[])
{
    int err, sig, nfmax;
    char *optarg, *orig_server_ftp_dir;
    PETOPT *pop;
    int code = LOG_DAEMON;
    SERVER *servp = NULL;
    sigset_t srvsigset;
    

    a_init();
    
    sigemptyset(&srvsigset);
    sigaddset(&srvsigset, SIGHUP);
    sigaddset(&srvsigset, SIGTERM);
    sigaddset(&srvsigset, SIGPIPE);
#ifdef SIGTTOU
    sigaddset(&srvsigset, SIGTTOU);
#endif
    
    SGINIT(listen_addr);
    SGPORT(listen_addr) = htons(21);
    
    if ((optarg = getenv("PFTPD_DEBUG")) != NULL)
    {
	if (*optarg)
	    debug = atoi(optarg);
	else
	    ++debug;
    }

    if (argv[0] != NULL)
    {
	char *cp;
	
	cp = strrchr(argv[0], '/');
	if (cp != NULL)
	    argv0 = a_strdup(cp+1, "argv0");
	else
	    argv0 = a_strdup(argv[0], "argv0");
    }

#if 0
#ifdef SIGTTOU    
    s_signal(SIGTTOU, SIG_IGN);
#endif
    s_signal(SIGPIPE, SIG_IGN);
#endif
    
    nfmax = unlimit_nofile();

    /* Set max concurrent requests (allow 3 per client) */
    requests_max = nfmax/3-8;
    if (debug)
	fprintf(stderr, "Max concurrent clients: %d\n", requests_max);
    
    s_openlog(argv0, LOG_PID|LOG_ODELAY, code);

    err = petopt_setup(&pop, POF_SYSLOG,
		       argc, argv,
		       pov,
		       parse_option, NULL);
    if (err)
    {
	if (err > 0)
	    fprintf(stderr, "%s: petopt_setup: %s\n",
		    argv[0], strerror(err));
	return EXIT_FAILURE;
    }
    
    /*
    ** Try to autodetect how we was started.
    */
    socket_type = socktype(STDIN_FILENO);
    
    if (debug)
	fprintf(stderr, "socktype = %d\n", socket_type);

    if (socket_type == SOCKTYPE_LISTEN || socket_type == SOCKTYPE_CONNECTED)
	listen_sock = STDIN_FILENO;
    
    conf_parse(PATH_CFGFILE, 1);

    err = petopt_parse(pop, &argc, &argv);
    if (err)
    {
	if (err > 0)
	    fprintf(stderr, "%s: petopt_parse() failed: %s\n",
		    argv[0], strerror(err));
        return EXIT_FAILURE;
    }
    
    if (debug)
	program_header(stderr);

    if (socket_type == -1)
    {
	syslog(LOG_ERR, "unable to autodetect socket type");
	fprintf(stderr, "%s: unable to autodetect socket type\n",
		argv0);
	return EXIT_FAILURE;
    }
    
    
    if (!debug && 
	getppid() != INIT_PID && !init_mode &&
	socket_type != SOCKTYPE_CONNECTED &&
	listen_sock < 0)
    {
	become_daemon();
    }

#ifdef HAVE_THR_SETCONCURRENCY
    /* XXX: FIXME */
    thr_setconcurrency(16);
#endif
    
    if (socket_type != SOCKTYPE_CONNECTED &&
	!debug && pidfile_path != NULL)
    {
	pidfile_create(pidfile_path);
    }

    if (listen_port > 0)
	SGPORT(listen_addr) = htons(listen_port);

    timeout_init();
    
    servp = server_init(listen_sock,
			&listen_addr,
			(socket_type != SOCKTYPE_CONNECTED ?
			 listen_backlog : -1), 
			requests_max,
			request_handler);
    if (servp == NULL)
    {
	if (debug)
	    fprintf(stderr, "%s: failed binding to the TCP/IP socket\n",
		    argv[0]);
	goto Exit;
    }


    if (server_root_dir)
    {
	if (chroot(server_root_dir) < 0)
	{
	    syslog(LOG_ERR, "chroot: %s: %m", server_root_dir);
	    if (debug)
		fprintf(stderr, "%s: chroot(\"%s\") failed: %s\n",
			argv[0], server_root_dir,
			strerror(errno));
	    goto Exit;
	}
	
	if (chdir("/") < 0)
	{
	    syslog(LOG_ERR, "chdir: /: %m");
	    if (debug)
		fprintf(stderr,
			"%s: chdir(\"/\") after chroot() failed: %s\n",
			argv[0], 
			strerror(errno));
	    goto Exit;
	}
    }

    if (server_ftp_dir == NULL)
    {
	struct passwd pwb, *pwp = NULL;
	char buf[2048];
	int err;
	
	
	err = s_getpwnam_r("ftp", &pwb, buf, sizeof(buf), &pwp);
	if (err)
	{
	    fprintf(stderr, "%s: lookup of user %s failed: %s\n",
			argv[0], "ftp", strerror(err));
	    goto Exit;
	}

	if (pwp)
	    server_ftp_dir = a_strdup(pwp->pw_dir, "server_ftp_dir");
	else
	    server_ftp_dir = PATH_FTPDIR;
    }

    orig_server_ftp_dir = server_ftp_dir;
    
    if (chdir(server_ftp_dir) == 0)
	server_ftp_dir = ".";
    else
	syslog(LOG_ERR, "chdir(\"%s\"): %m", server_ftp_dir);

    umask(0);
    
    drop_root_privs();
    xferlog_init();
    ftpdata_init();

    if (rpad_dir)
	rpa_init(rpad_dir);
    
    rpap = rpa_open("ftp");
    if (!rpap)
	syslog(LOG_ERR, "rpa_open(\"ftp\"): %m");
	
    syslog(LOG_NOTICE, "started (home=%s, uid=%u, gid=%u, type=%u, maxclients=%u)",
	   orig_server_ftp_dir,
	   server_uid, server_gid,
	   socket_type,
	   requests_max);

    if (socket_type == SOCKTYPE_CONNECTED)
	return server_run_one(servp, 0, 1);

    
    if (debug)
	fprintf(stderr, "entering server main loop\n");
    
    pthread_sigmask(SIG_BLOCK, &srvsigset, NULL);

    server_start(servp);

    /* XXX: Handle server thread termination! */
    
    while ((err = sigwait(&srvsigset, &sig)) == 0)
    {
	if (debug)
	    fprintf(stderr, "sigwait returned sig %d\n", sig);
	
	switch (sig)
	{
	  case SIGHUP:
	    /* Close and reopen the xferlog file */
	    syslog(LOG_NOTICE, "SIGHUP received - reinitializing xferlog");
	    xferlog_init();
	    break;

	  case SIGTERM:
	    /* Terminate gracefully - close server socket, but wait for
	       and active clients to finish */
	    syslog(LOG_NOTICE, "SIGTERM received - terminating");
	    server_destroy(servp);
	    pthread_exit(NULL);

	  case SIGPIPE:
#ifdef SIGTTOU
	  case SIGTTOU:
#endif
	    /* Ignore */
	    break;

	  default:
	    syslog(LOG_NOTICE, "main: unhandled signal (%d) received - ignored", sig);
	}
    }
    
    syslog(LOG_ERR, "sigwait(): %s", strerror(err));
		
  Exit:
    syslog(LOG_NOTICE, "terminating");
    petopt_cleanup(pop);
    return EXIT_FAILURE;
}
