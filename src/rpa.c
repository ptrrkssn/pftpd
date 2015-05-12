/*
** rpa.c - Reserved Port Allocation service
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
#include <string.h>
#include <fcntl.h>
#include <syslog.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef HAVE_DOOR_H
#include <door.h>
#endif

#include <sys/uio.h>
#include <stropts.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "plib/safeio.h"
#include "plib/aalloc.h"

#include "pftpd.h"
#include "rpa.h"

char *path_rpad_dir = PATH_RPAD_DIR;

void
rpa_init(const char *path)
{
    if (path)
	path_rpad_dir = strdup(path);
}


int
rpa_door_open(RPA *rp)
{
#ifdef HAVE_DOOR_CALL
    char buf[1024];
    struct stat sb;
    struct door_info dib;
    

    pthread_mutex_lock(&rp->mtx);
    
    s_snprintf(buf, sizeof(buf), "%s/%s/door", path_rpad_dir, rp->service);

    rp->fd = s_open(buf, O_RDONLY);
    if (rp->fd < 0)
    {
	syslog(LOG_ERR, "rpa_door_open: open(\"%s\") failed: %m", buf);
	goto Fail;
    }
    
    if (fstat(rp->fd, &sb) < 0)
    {
	syslog(LOG_ERR, "rpa_door_open: fstat() on %s failed: %m", buf);
	goto Fail;
    }

    if (S_ISDOOR(sb.st_mode) == 0)
    {
	syslog(LOG_ERR, "rpa_door_open: %s: is not a DOOR", buf);
	goto Fail;
    }

    if (door_info(rp->fd, &dib) < 0)
    {
	syslog(LOG_ERR, "rpa_door_open: door_info() on %s failed: %m", buf);
	goto Fail;
    }

    if (debug)
    {
	printf("rpa_door_open: door_info() -> server pid = %ld, uniquifier = %ld",
	       (long) dib.di_target,
	       (long) dib.di_uniquifier);
	
	if (dib.di_attributes & DOOR_LOCAL)
	    printf(", LOCAL");
	if (dib.di_attributes & DOOR_PRIVATE)
	    printf(", PRIVATE");
	if (dib.di_attributes & DOOR_REVOKED)
	    printf(", REVOKED");
	if (dib.di_attributes & DOOR_UNREF)
	    printf(", UNREF");
	putchar('\n');
    }

    if (dib.di_attributes & DOOR_REVOKED)
    {
	syslog(LOG_ERR, "rpa_door_open: door revoked");
	goto Fail;
    }
    
    rp->type = 2;
    
    pthread_mutex_unlock(&rp->mtx);
    return 0;

  Fail:
    if (rp->fd >= 0)
	s_close(rp->fd);
    
    rp->fd = -1;
    pthread_mutex_unlock(&rp->mtx);
#endif
    return -1;
}


int
rpa_pipe_open(RPA *rp)
{
#ifdef I_RECVFD
    char buf[1024];
    struct stat sb;
    

    if (debug)
	fprintf(stderr, "rpa_pipe_open: Start\n");
    
    pthread_mutex_lock(&rp->mtx);
    
    s_snprintf(buf, sizeof(buf), "%s/%s/pipe", PATH_RPAD_DIR, rp->service);

    rp->fd = s_open(buf, O_RDONLY);
    if (rp->fd < 0)
    {
	syslog(LOG_ERR, "rpa_pipe_open: open(\"%s\") failed: %m", buf);
	goto Fail;
    }
    
    if (fstat(rp->fd, &sb) < 0)
    {
	syslog(LOG_ERR, "rpa_pipe_open: fstat() on %s failed: %m", buf);
	goto Fail;
    }

    if (S_ISFIFO(sb.st_mode) == 0)
    {
	syslog(LOG_ERR, "rpa_pipe_open: %s: is not a FIFO", buf);
	goto Fail;
    }

    rp->type = 3;
    
    pthread_mutex_unlock(&rp->mtx);
    
    if (debug)
	fprintf(stderr, "rpa_pipe_open: Done\n");
    
    return 0;

  Fail:
    if (rp->fd >= 0)
	s_close(rp->fd);

    rp->fd = -1;
    pthread_mutex_unlock(&rp->mtx);
#endif
    if (debug)
	fprintf(stderr, "rpa_pipe_open: Failed\n");
    return -1;
}

int
rpa_unix_open(RPA *rp)
{
    char path[1024];
    struct sockaddr_un usb;
    

    pthread_mutex_lock(&rp->mtx);
    
    rp->fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (rp->fd < 0)
    {
	syslog(LOG_ERR, "rpa_unix_open: socket() failed: %m");
	goto Fail;
    }
    
    strcpy(path, "/var/tmp/rpa.XXXXXX");
    mktemp(path);
    if (!path[0])
    {
	syslog(LOG_ERR, "rpa_unix_open: unable to create local socket name");
	goto Fail;
    }
    
    memset(&usb, 0, sizeof(sun));
    usb.sun_family = AF_UNIX;
    strcpy(usb.sun_path, path);
    
    if (s_bind(rp->fd, (struct sockaddr *) &usb, sizeof(usb)+strlen(usb.sun_path)) < 0)
    {
	syslog(LOG_ERR, "rpa_unix_open: bind(\"%s\"): %m", path);
	return errno;
    }

    (void) unlink(path);
    
    s_snprintf(path, sizeof(path), "%s/%s/unix", PATH_RPAD_DIR, rp->service);

    memset(&usb, 0, sizeof(sun));
    usb.sun_family = AF_UNIX;
    strcpy(usb.sun_path, path);
    
    if (s_connect(rp->fd, (struct sockaddr *) &usb, sizeof(usb)+strlen(usb.sun_path)) < 0)
    {
	syslog(LOG_ERR, "rpa_unix_open: connect(\"%s\"): %m", path);
	goto Fail;
    }

    rp->type = 1;
    
    pthread_mutex_unlock(&rp->mtx);
    return 0;

  Fail:
    if (rp->fd >= 0)
	s_close(rp->fd);
    
    rp->fd = -1;
    pthread_mutex_unlock(&rp->mtx);
    return -1;
}






RPA *
rpa_open(const char *service)
{
    RPA *rp = NULL;
    int code = -1;

    rp = a_malloc(sizeof(*rp), "RPA");
    pthread_mutex_init(&rp->mtx, NULL);
    rp->fd = -1;
    rp->type = 0;
    rp->service = a_strdup(service, "RPA service");

    code = rpa_door_open(rp);
    
    if (code < 0)
	code = rpa_pipe_open(rp);

    if (code < 0)
	code = rpa_unix_open(rp);

    if (code < 0)
    {
	rpa_close(rp);
	return NULL;
    }
    
    return rp;
}


void
rpa_close(RPA *rp)
{
    if (rp)
    {
        if (rp->fd >= 0)
	    s_close(rp->fd);
    
	a_free(rp);
    }
}

int
rpa_door_rresvport(RPA *rp,
		   struct sockaddr_gen *sp)
{
#ifdef HAVE_DOOR_CALL
    int fd = -1;
    int code;
    char rbuf[512];
    long *rcode;
    struct door_arg da;
    int attempt = 0;
    struct door_info dib;
    
    
    if (rp == NULL)
	return -1;

    attempt = 0;
    while (attempt < 3 &&
	   (door_info(rp->fd, &dib) < 0 || (dib.di_attributes & DOOR_REVOKED)))
    {
	rpa_door_open(rp);
	++attempt;
    }
	
    if (attempt >= 3)
    {
	syslog(LOG_ERR, "rpa_door_rresvport: invalid door");
	return -1;
    }

    attempt = 0;
    do
    {
	memset(&da, 0, sizeof(da));
	
	da.data_ptr = (char *) sp;
	da.data_size = SGSOCKSIZE(*sp);
	
	da.desc_ptr = NULL;
	da.desc_num = 0;

	da.rbuf = rbuf;
	da.rsize = sizeof(rbuf);

	++attempt;
	
    } while ((code = door_call(rp->fd, &da)) < 0 &&
	     errno == EINTR && attempt < 256);

    /* XXX: Is 256 a good choice? */

    if (code < 0)
    {
	syslog(LOG_ERR, "rpa_door_rresvport: door_call(%d) failed: %m", rp->fd);
	
	if (debug)
	    fprintf(stderr, "rpa_door_rresvport: door_call() failed\n");

	return -1;
    }

    if (da.desc_ptr != NULL && da.desc_num == 1 &&
	da.desc_ptr[0].d_attributes == DOOR_DESCRIPTOR)
    {
	fd = da.desc_ptr[0].d_data.d_desc.d_descriptor;
    }
    else
    {
	if (da.data_size != sizeof(*rcode))
	    syslog(LOG_ERR, "rpa_door_rresvport: door_call(%d) returned invalid rcode buffer size(%lu)", rp->fd,
		   da.data_size);

	rcode = (long *) da.data_ptr;
	
	if (*rcode > 0)
	    errno = *rcode;
	else
	    errno = EINVAL;
    }

    if (da.rbuf != rbuf)
	munmap(da.rbuf, da.rsize);
    
    return fd;
#else
    return -1;
#endif
}

int
rpa_unix_rresvport(RPA *rp,
		   struct sockaddr_gen *sp)
{
    int code, rcode, rfd;
    struct iovec iov[1];
    struct msghdr msg;
    

    if (rp == NULL)
	return -1;

    if (debug)
	fprintf(stderr, "rpa_unix_rresvport: Requesting descriptor...\n");
    
    code = s_write(rp->fd, sp, sizeof(*sp));
    if (code < 0)
    {
	syslog(LOG_ERR, "rpa_unix_rresvport: write(): %m");
	return -1;
    }

    iov[0].iov_base = (void *) &rcode;
    iov[0].iov_len = sizeof(rcode);

    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_name = NULL;
    msg.msg_accrights = (void *) &rfd;
    msg.msg_accrightslen = sizeof(rfd);

    rfd = -1;
    rcode = -1;
    
    while ((code = recvmsg(rp->fd, &msg, 0)) < 0 && errno == EINTR)
	;

    if (rcode < 0)
    {
	syslog(LOG_ERR, "rpa_unix_rresvport: recvmsg: %m");
	return -1;
    }

    if (debug)
	fprintf(stderr, "rpa_unix_rresvport: returning fd=%d, rcode=%d\n",
		rfd, rcode);
    
    return rfd;
}


int
rpa_pipe_rresvport(RPA *rp,
		   struct sockaddr_gen *sp)
{
/* XXX: Change to a configure-check! */
#ifdef I_RECVFD
    int code, rcode, rfd;
    struct strrecvfd rb;

    
    if (rp == NULL)
	return -1;

    if (debug)
	fprintf(stderr, "rpa_pipe_rresvport: Requesting descriptor...\n");

    /* XXX: Make sure this is done privately (a lock file?) so multiple
       XXX: clients talking to RPAD concurrently doesn't causes problems */
    
    code = s_write(rp->fd, sp, sizeof(*sp));
    if (code < 0)
    {
	syslog(LOG_ERR, "rpa_pipe_rresvport: write(): %m");
	return -1;
    }

    while ((code = ioctl(rp->fd, I_RECVFD, &rb)) < 0 && errno == EINTR)
	;

    if (code < 0)
    {
	syslog(LOG_ERR, "rpa_pipe_rresvport: ioctl(I_RECVFD): %m");
	return -1;
    }

    if (debug)
	fprintf(stderr, "rpa_pipe_rresvport: fd=%d, uid=%d, gid=%d\n",
		rb.fd, rb.uid, rb.gid);
    
    return rb.fd;
#else
    return -1;
#endif
}




rpa_rresvport(RPA *rp,
	      struct sockaddr_gen *sp)
{
    if (rp)
	switch (rp->type)
	{
	  case 1:
	    return rpa_unix_rresvport(rp, sp);
	    
	  case 2:
	    return rpa_door_rresvport(rp, sp);
	    
	  case 3:
	    return rpa_pipe_rresvport(rp, sp);
	}

    return -1;
}


	    

#if DEBUG
int
main(int argc,
     char *argv[])
{
  int fd;
  struct stat sb;
  struct door_info dib;
  long ival, res;
  struct door_arg da;

  
  if (argc != 3)
    {
      fprintf(stderr, "usage: %s <server-pathname> <int-value>\n", argv[0]);
      exit(1);
    }

  fd = open(argv[1], O_RDONLY);
  if (fd < 0)
    {
      perror("open");
      exit(1);
    }

  if (fstat(fd, &sb) < 0)
    {
      perror("fstat");
      exit(1);
    }

  if (S_ISDOOR(sb.st_mode) == 0)
    {
      fprintf(stderr, "%s: %s is not a door.\n", argv[0], argv[1]);
      exit(1);
    }

  if (door_info(fd, &dib) < 0)
    {
      perror("door_info");
      exit(1);
    }

  printf("server pid = %ld, uniquifier = %ld",
	 (long) dib.di_target,
	 (long) dib.di_uniquifier);

  if (dib.di_attributes & DOOR_LOCAL)
    printf(", LOCAL");
  if (dib.di_attributes & DOOR_PRIVATE)
    printf(", PRIVATE");
  if (dib.di_attributes & DOOR_REVOKED)
    printf(", REVOKED");
  if (dib.di_attributes & DOOR_UNREF)
    printf(", UNREF");
  putchar('\n');
  
  ival = atoi(argv[2]);
  da.data_ptr = (char *) &ival;
  da.data_size = sizeof(ival);
  da.desc_ptr = NULL;
  da.desc_num = 0;
  da.rbuf = (char *) &res;
  da.rsize = sizeof(res);

  if (door_call(fd, &da) < 0)
    {
      perror("door_call");
      exit(1);
    }

  if (da.desc_ptr != NULL && da.desc_num > 0)
    {
      int i;
      
      printf("door descriptors returned: %d\n", da.desc_num);

      for (i = 0; i < da.desc_num; i++)
	{
	  if (da.desc_ptr[i].d_attributes == DOOR_DESCRIPTOR)
	    {
	      int dfd;
	      
	      dfd = da.desc_ptr[i].d_data.d_desc.d_descriptor;
	      fstat(dfd, &sb);
	      
	      printf("#%d: fd=%d, size=%d\n", i, dfd, sb.st_size);
	    }
	  else
	    printf("#%d: unknown data\n");
	}
    }
  
  printf("result = %ld\n", res);
  exit(0);
}

#endif
