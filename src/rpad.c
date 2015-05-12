/*
** rpad.c - Reserved Port Allocation service Daemon
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <sys/socket.h>

#ifdef HAVE_DOOR_H
#include <door.h>
#endif

#include <sys/uio.h>
#include <stropts.h>

#include "rpa.h"

#include "plib/safeio.h"
#include "plib/daemon.h"

#define INIT_PID 1

int debug = 0;
int init_mode = 0;

int access_euid = -1;
int access_egid = -1;
int access_ruid = -1;
int access_rgid = -1;

#define PORT_FTP_DATA 20

char *path_rpad_dir = PATH_RPAD_DIR;


void
servproc(void *cookie,
	 char *dataptr,
	 size_t datasize,
	 door_desc_t *descptr,
	 size_t ndesc)
{
    door_desc_t db;
    door_cred_t cb;
    struct sockaddr_gen sin;
    int rcode = -1, rfd;



    if (debug)
	fprintf(stderr, "rpad: servproc: Start\n");
    
    door_cred(&cb);

    if (access_euid != -1 && access_euid != cb.dc_euid)
    {
	rcode = -2;
	goto Fail;
    }
    
    if (access_egid != -1 && access_egid != cb.dc_egid)
    {
	rcode = -3;
	goto Fail;
    }
    
    if (access_ruid != -1 && access_ruid != cb.dc_ruid)
    {
	rcode = -4;
	goto Fail;
    }
    
    if (access_rgid != -1 && access_rgid != cb.dc_rgid)
    {
	rcode = -5;
	goto Fail;
    }
    
    if (dataptr == DOOR_UNREF_DATA)
    {
	if (debug)
	    printf("rpad: servproc: door unreferenced\n");
	
	rcode = -6;
	goto Fail;
    }

    sin = * (struct sockaddr_gen *) dataptr;
    
    rfd = create_bind_socket(&sin, PORT_FTP_DATA);
    if (rfd < 0)
    {
	rcode = errno;
	door_return((char *) &rcode,  sizeof(rcode), NULL, 0);
    }

    db.d_data.d_desc.d_descriptor = rfd;
    db.d_attributes = DOOR_DESCRIPTOR|DOOR_RELEASE;

    rcode = 0;
    door_return((char *) &rcode, sizeof(rcode), &db, 1);

  Fail:
    door_return((char *) &rcode, sizeof(rcode), NULL, 0);
}



int
start_door_server(char *rpad_dir,
		  char *service)
{
    int dfd, pfd, i, j, rcode;
    char path_door[1024];

    
    s_snprintf(path_door, sizeof(path_door), "%s/%s/door", rpad_dir, service);
    if (unlink(path_door) < 0 && errno != ENOENT)
    {
	rcode = errno;
	syslog(LOG_ERR, "unlink(\"%s\") failed: %s\n", path_door, strerror(errno));
	return rcode;
    }
    
    pfd = s_open(path_door, O_CREAT|O_RDWR, 0644);
    if (pfd < 0)
    {
	rcode = errno;
	syslog(LOG_ERR, "open(\"%s\") failed: %s\n", path_door, strerror(errno));
	return rcode;
    }
    
    (void) s_close(pfd);
    
    dfd = door_create(servproc, NULL, DOOR_UNREF);
    if (dfd < 0)
    {
	rcode = errno;
	syslog(LOG_ERR, "door_create() failed: %s\n", strerror(errno));
	(void) unlink(path_door);
	return rcode;
    }
    
    if (fattach(dfd, path_door) < 0)
    {
	rcode = errno;
	syslog(LOG_ERR, "fattach(\"%s\") failed: %s\n",
	       path_door, strerror(errno)); 

	(void) unlink(path_door);
	return rcode;
    }
    
    return 0;
}    

static int pipe_fd = -1;
static pthread_t pipe_tid;


void *
pipe_thread(void *vp)
{
    int rlen, rcode, rfd;
    struct sockaddr_gen sin;


    /* XXX: Make sure this is done privately (a lock file?) so multiple
       XXX: clients talking to RPAD concurrently doesn't cause problems */
    
    while (1)
    {
	rlen = s_read(pipe_fd, &sin, sizeof(sin));
	if (debug)
	    fprintf(stderr, "pipe_thread: Got request (%d bytes)\n", rlen);
	    
	if (rlen == sizeof(sin))
	{
	    rcode = 0;
	    
	    rfd = create_bind_socket(&sin, PORT_FTP_DATA);
	    if (rfd < 0)
		rcode = errno;

	    if (s_write(pipe_fd, &rcode, sizeof(rcode)) < 0)
		syslog(LOG_ERR, "pipe_thread: unable to send errno to client");

	    while ((rcode = ioctl(pipe_fd, I_SENDFD, rfd)) < 0 && errno == EINTR)
		;

	    if (rcode < 0)
		syslog(LOG_ERR, "pipe_thread: ioctl(I_SENDFD) failed: %m");
	    
	    s_close(rfd);

	    if (debug)
		fprintf(stderr, "pipe_thread: Result sent\n");
	}
    }
    
    return NULL;
}

int
start_pipe_server(char *rpad_dir,
		  char *service)
{
    int fds[2], pfd, i, j, rcode, code;
    char path[1024];

    
    s_snprintf(path, sizeof(path), "%s/%s/pipe", rpad_dir, service);
    if (unlink(path) < 0 && errno != ENOENT)
    {
	rcode = errno;
	syslog(LOG_ERR, "start_pipe_server: unlink(\"%s\") failed: %s\n", path, strerror(errno));
	return rcode;
    }
    
    pfd = s_open(path, O_CREAT|O_RDWR, 0644);
    if (pfd < 0)
    {
	rcode = errno;
	syslog(LOG_ERR, "start_pipe_server: open(\"%s\") failed: %s\n", path, strerror(errno));
	return rcode;
    }
    
    (void) s_close(pfd);

    if (pipe(fds) < 0)
    {
	rcode = errno;
	syslog(LOG_ERR, "start_pipe_server: pipe() failed: %s\n", strerror(errno));
	(void) unlink(path);
	return rcode;
    }
    
    if (fattach(fds[0], path) < 0)
    {
	rcode = errno;
	syslog(LOG_ERR, "start_pipe_server: fattach(\"%s\") failed: %s\n",
	       path, strerror(errno)); 

	(void) unlink(path);
	return rcode;
    }

    /*    s_close(fds[1]); */
    
    pipe_fd = fds[0];
    
    code = pthread_create(&pipe_tid, NULL, pipe_thread, NULL);
    return code;
}    

static int unix_fd = -1;
static pthread_t unix_tid;


void *
unix_thread(void *vp)
{
    int fd, rfd, rlen, rcode;
    struct sockaddr_un usb;
    struct sockaddr_gen sin;
    socklen_t len;
    struct iovec iov[1];
    struct msghdr msg;
		
    
    len = sizeof(usb);
    while ((fd = s_accept(unix_fd, (struct sockaddr *) &usb, &len)) >= 0)
    {
	fprintf(stderr, "unix_thread: accepted call on fd #%d from %s\n",
		fd, usb.sun_path);


	rlen = s_read(fd, &sin, sizeof(sin));
	if (rlen == sizeof(sin))
	{
	    rcode = 0;
	    
	    rfd = create_bind_socket(&sin, PORT_FTP_DATA);
	    if (rfd < 0)
		rcode = errno;

	    iov[0].iov_base = (void *) &rcode;
	    iov[0].iov_len = sizeof(rcode);

	    msg.msg_iov = iov;
	    msg.msg_iovlen = 1;
	    msg.msg_name = NULL;
	    if (rfd >= 0)
	    {
		msg.msg_accrights = (void *) &rfd;
		msg.msg_accrightslen = sizeof(rfd);
	    }
	    else
	    {
		msg.msg_accrights = NULL;
		msg.msg_accrightslen = 0;
	    }

	    while ((rcode = sendmsg(fd, &msg, 0)) < 0 && errno == EINTR)
		;

	    if (rcode < 0)
		syslog(LOG_ERR, "unix_thread: sendmsg: %m");
	    
	    s_close(rfd);
	}

      Next:
	s_close(fd);
	len = sizeof(usb);
    }

    return NULL;
}



int
start_unix_server(char *dir,
		  char *service)
{
    char path[1024];
    struct sockaddr_un usb;
    int code;
    
    
    s_snprintf(path, sizeof(path), "%s/%s/unix", dir, service);

    if (unlink(path) < 0 && errno != ENOENT)
	return errno;
    
    memset(&usb, 0, sizeof(sun));
    usb.sun_family = AF_UNIX;
    strcpy(usb.sun_path, path);
    
    unix_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (unix_fd < 0)
    {
	syslog(LOG_ERR, "start_unix_server: socket(PF_UNIX, SOCK_STREAM, 0): %m");
	return errno;
    }

    if (s_bind(unix_fd, (struct sockaddr *) &usb, sizeof(usb)+strlen(usb.sun_path)) < 0)
    {
	syslog(LOG_ERR, "start_unix_server: bind(\"%s\"): %m", path);
	return errno;
    }
    
    if (listen(unix_fd, 1) < 0)
    {
	syslog(LOG_ERR, "start_unix_server: listen(): %m");
	return errno;
    }

    code = pthread_create(&unix_tid, NULL, unix_thread, NULL);

    return code;
}


int
main(int argc,
     char *argv[])
{
    int fd, i, j, dcode, ucode, pcode;
    char path[1024];
    char *service = "ftp";
    

    for (i = 1; i < argc && argv[i][0] == '-'; ++i)
	for (j = 1; j > 0 && argv[i][j]; ++j)
	    switch (argv[i][j])
	    {
	      case '-':
		goto EndOptions;
		
	      case 'd':
		++debug;
		break;

	      case 's':
		if (argv[i][j+1])
		{
		    service = strdup(argv[i]+j+1);
		    j = -1;
		}
		else if (i+1 < argc)
		{
		    service = strdup(argv[++i]);
		}
		else
		{
		    fprintf(stderr, "%s: -%c: missing service name\n",
			    argv[0], argv[i][j]);
		    exit(1);
		}
		break;
		
	      case 'D':
		if (argv[i][j+1])
		{
		    path_rpad_dir = strdup(argv[i]+j+1);
		    j = -1;
		}
		else if (i+1 < argc)
		{
		    path_rpad_dir = strdup(argv[++i]);
		}
		else
		{
		    fprintf(stderr, "%s: -%c: missing rpad directory\n",
			    argv[0], argv[i][j]);
		    exit(1);
		}
		break;
		
	      case 'I':
		++init_mode;
		break;

	      default:
		fprintf(stderr, "%s: unknown command line option: -%c\n",
			argv[0], argv[i][j]);
		exit(1);
	    }

  EndOptions:

    s_openlog(argv[0], LOG_PID|LOG_ODELAY, LOG_DAEMON);
    
    if (i < argc)
    {
	fprintf(stderr, "usage: %s [<options>]\n", argv[0]);
	exit(1);
    }

    if (chdir(path_rpad_dir) != 0)
    {
	fprintf(stderr, "%s: chdir: %s: %s\n", argv[0], path_rpad_dir,
		strerror(errno));
	exit(1);
    }
    
    if (!debug && 
	getppid() != INIT_PID && !init_mode)
    {
	become_daemon();
    }

    dcode = ucode = pcode = -1;
    
#ifdef HAVE_DOOR_CALL
    dcode = start_door_server(path_rpad_dir, service);
#endif

#ifdef AF_UNIX
    ucode = start_unix_server(path_rpad_dir, service);
#endif

#ifdef I_SENDFD
    pcode = start_pipe_server(path_rpad_dir, service);
#endif
    
    syslog(LOG_NOTICE, "started (%s%s%s )",
	   (!ucode ? " unix" : ""),
	   (!pcode ? " pipe" : ""),
	   (!dcode ? " doors" : ""));

    /* XXX: Loop handling AF_UNIX socket connections */
    
    while (1)
	pause();
    
    exit(1);
}
