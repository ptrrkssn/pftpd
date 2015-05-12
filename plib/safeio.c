/*
** safeio.c - Signal/Async safe wrapper functions
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

#define PLIB_IN_SAFEIO_C

#include "plib/config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <syslog.h>
#include <signal.h>

#ifdef HAVE_POLL
#include <poll.h>
#else
#include <sys/time.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#ifndef MAP_FAILED
#define MAP_FAILED ((void *) -1)
#endif

#ifdef HAVE_SYS_SENDFILE_H
#include <sys/sendfile.h>
#endif

#include "plib/threads.h"
#include "plib/sockaddr.h"

#include "plib/safeio.h"


void
s_abort(void)
{
    int *p = (int *) NULL;

    *p = 4711;

    /* Emergency fallback */
    abort();
}



int
s_open(const char *path,
       int oflag,
       ...)
{
    int s;
    mode_t mode = 0;

    if (oflag & O_CREAT)
    {
	va_list ap;

	va_start(ap, oflag);
	/* FIXME: need to use widened form of mode_t here.  */
	mode = va_arg(ap, int);
	va_end(ap);
    }

    while ((s = open(path, oflag, mode)) < 0 && errno == EINTR)
	;
    
    if (s < 0 && (errno == EMFILE
		  || errno == ENFILE
		  || errno == ENOMEM 
#ifdef ENOSR
		  || errno == ENOSR
#endif
		  ))
    {
	/* Too many open files */
	
	syslog(LOG_WARNING, "s_open(\"%s\", 0%o): %m", path, oflag);
    }
    
    return s;
}



ssize_t
s_write(int fd,
	const void *buf,
	size_t len)
{
    ssize_t code;
    
    while ((code = write(fd, buf, len)) < 0 && errno == EINTR)
	;

    return code;
}



ssize_t
s_read(int fd,
       void *buf,
       size_t len)
{
    ssize_t code;

    
    while ((code = read(fd, buf, len)) < 0 && errno == EINTR)
	;
    
    return code;
}



ssize_t
s_pread(int fd,
	void *buf,
	size_t len,
	off_t off)
{
    ssize_t code;
    
#ifdef HAVE_PREAD
    while ((code = pread(fd, buf, len, off)) < 0 && errno == EINTR)
	;
    return code;
#else
    if (lseek(fd, off, SEEK_SET) == (off_t) -1)
	return -1;
    
    while ((code = read(fd, buf, len)) < 0 && errno == EINTR)
	;
    return code;
#endif
}


int
s_close(int fd)
{
    int code;
    
    while ((code = close(fd)) < 0 && errno == EINTR)
	;
    
    return code;
}


int
s_waitfd(int fd,
	 int timeout)
{
    int res;

    
#ifdef HAVE_POLL
    struct pollfd pfd;
    
    
    pfd.fd = fd;
    pfd.events = POLLIN;

    
    while ((res = poll(&pfd, 1, timeout*1000)) == -1 &&
	   errno == EINTR)
	;
#else
    fd_set rfds;
    struct timeval to;

#ifdef FD_SETSIZE
    if (fd >= FD_SETSIZE)
    {
	errno = EINVAL;
	return -1;
    }
#endif
    
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    to.tv_sec = timeout;
    to.tv_usec = 0;
    
    while ((res = select(fd+1, &rfds, NULL, NULL, &to)) < 0 &&
	   errno == EINTR)
	;

#endif
    if (res < 0)
	return -1;
	
    if (res == 0)
	return 0;

    return 1;
}


int
s_accept2(int listen_fd,
	  struct sockaddr *sin,
	  socklen_t *len,
	  int timeout)
{
    int fd, res;

    if (timeout > 0)
    {
	res = s_waitfd(listen_fd, timeout);
	if (res < 0)
	    return res;
	if (res == 0)
	{
	    errno = ETIMEDOUT;
	    return -1;
	}
    }
    
    while ((fd = accept(listen_fd, sin, len)) < 0 &&
	   errno == EINTR)
	;
    
    return fd;
}

int
s_accept(int listen_fd,
	 struct sockaddr *sin,
	 socklen_t *len)
{
    return s_accept2(listen_fd, sin, len, -1);
}


static pthread_mutex_t random_lock;
static pthread_once_t random_once = PTHREAD_ONCE_INIT;

static void
random_lock_init(void)
{
    unsigned int seed;
    
    pthread_mutex_init(&random_lock, NULL);
    
    seed = time(NULL);
#ifdef HAVE_SRANDOM
    srandom(seed);
#else
    srand(seed);
#endif
}


long
s_random(void)
{
    long res;
    
    pthread_once(&random_once, random_lock_init);
    
    pthread_mutex_lock(&random_lock);
#ifdef HAVE_RANDOM
    res = random();
#else
    res = rand();
#endif
    pthread_mutex_unlock(&random_lock);

    return res;
}



/*
** Set up a signal handler without restartable syscall.
*/
SIGHANDLER_T
s_signal(int sig,
	 SIGHANDLER_T handler)
{
#ifdef HAVE_SIGACTION
    if (handler != SIG_IGN)
    {
	struct sigaction osa, nsa;
	
	if (sigaction(sig, NULL, &osa) == -1)
	    return SIG_ERR;

	nsa = osa;
	nsa.sa_handler = handler;
	nsa.sa_flags = 0;
	sigaction(sig, &nsa, NULL);
	return osa.sa_handler;
    }
#endif
    return signal(sig, handler);
}


#define COPYFD_BUFSIZE (64*1024)

static int
simple_copyfd(int to_fd,
	      int from_fd,
	      off_t *start,
	      size_t maxlen)
{
    char buf[COPYFD_BUFSIZE];
    int err, tlen, written, i;


    if (start && *start > 0)
	if (lseek(from_fd, *start, SEEK_SET) == (off_t) -1)
	{
	    /* Can't seek - wind to the starting point */
	    tlen = 0;
	    while (tlen < *start)
	    {
		i = *start - tlen;
		if (i > sizeof(buf))
		    i = sizeof(buf);
		
		err = s_read(from_fd, buf, i);
		if (err <= 0)
		    return -1;
		
		tlen += err;
	    }
	    
	    if (tlen < *start)
		return -1;		
	}
    
    written = 0;
    tlen = -1;
    
    while ((maxlen == 0 || written < maxlen) &&
	   (tlen = s_read(from_fd, buf,
			  (maxlen == 0 || maxlen - written > sizeof(buf) ?
			   sizeof(buf) : maxlen - written))) > 0)
    {
	err = s_write(to_fd, buf, tlen);
	if (err < 0)
	    return -1;
	
	written += tlen;
	if (start)
	    *start += tlen;
    }
    
    if (tlen < 0)
	return -1;

    return written;
}

#if !defined(HAVE_SENDFILE) && defined(HAVE_SENDFILEV)
static ssize_t
sendfile(int out_fd,
	 int in_fd,
	 off_t *off,
	 size_t len)
{
    struct sendfilevec vb;
    int code;
    size_t written = 0;
    

    vb.sfv_fd = in_fd;
    vb.sfv_flag = 0;
    vb.sfv_off = *off;
    vb.sfv_len = len;

    if (1)
	fprintf(stderr, "sendfilev(), off = %lu, len = %lu\n",
		(unsigned long) *off,
		(unsigned long) len);
    
    code = sendfilev(out_fd, &vb, 1, &written);

    if (1)
	fprintf(stderr, "\t-> code = %d, written = %lu\n",
		code, (unsigned long) written);
		
    if (written > 0)
    {
	*off += written;
	return written;
    }

    return code;
}

#define HAVE_SENDFILE 1

#endif

int
s_copyfd(int to_fd,
	 int from_fd,
	 off_t *start,
	 size_t maxlen)
{
    struct stat sb;
    int len;
    

    /* If source isn't a plain file, use simple copyfd */
    if (fstat(from_fd, &sb) < 0)
	return simple_copyfd(to_fd, from_fd, start, maxlen);
    
    if (start && (*start >= sb.st_size))
	return -1;

    if (maxlen == 0)
	len = sb.st_size-(start != NULL ? *start : 0);
    else
	len = maxlen;

#ifdef HAVE_SENDFILE
    {
	int err;
	
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
	off_t sbytes;
	while ((err = sendfile(to_fd, from_fd, *start, len,
			       NULL, &sbytes, 0)) == -1) {
	    switch (errno) {
	    case EINTR:
		continue;
	    case EAGAIN:
		*start += sbytes;
		len -= sbytes;
		break;
	    default:
		sbytes = 0;
		break;
	    }
	}
	*start += sbytes;
#else
	/* Assuming Linux / Solaris style sendfile() */
	while ((err = sendfile(to_fd, from_fd, start, len)) == -1 &&
	       errno == EINTR)
	    ;
#endif

	if (err == -1 && errno == EINVAL)
	    return simple_copyfd(to_fd, from_fd, start, maxlen);
    
	return err;
    }
#else
#ifndef HAVE_MMAP
    return simple_copyfd(to_fd, from_fd, start, maxlen);
#else
    {
	char *buf;
	int err;
	
	
	/* Don't bother with mmap() for small files */
	if (sb.st_size <= COPYFD_BUFSIZE)
	    return simple_copyfd(to_fd, from_fd, start, maxlen);
	
	buf = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE,
		   from_fd, 0);
	
	if (buf == (char *) MAP_FAILED)
	    return simple_copyfd(to_fd, from_fd, start, maxlen);

#ifdef MADV_SEQUENTIAL
	/* XXX: Enable MADV_WILLNEED? */
	madvise(buf, sb.st_size, MADV_SEQUENTIAL);
#endif
	
	/* XXX: Lock the file region in order to protect against
	   someone doing an ftruncate()? */
	
	err = s_write(to_fd, buf+(start != NULL ? *start : 0), len);
	munmap(buf, sb.st_size);
	if (err > 0 && start)
	*start += err;
	
	return err;
    }
#endif
#endif
}


int
s_bind(int fd,
       struct sockaddr *sin,
       socklen_t len)
{
    int err;


    while ((err = bind(fd, sin, len)) < 0 && errno == EINTR)
	;

    return err;
}

int
s_connect(int fd,
	  struct sockaddr *sin,
	  socklen_t len)
{
    int err;


    while ((err = connect(fd, sin, len)) < 0 && errno == EINTR)
	;

    return err;
}


/*
** An MT-safe version of inet_ntoa() for IPv4 and
** inet_ntop() for IPv6 (already MT-safe). 
**
** IPv6 addresses is printed using RFC2732 notation.
*/

const char *
s_inet_ntox(struct sockaddr_gen *ia,
	    char *buf,
	    size_t bufsize)
{
    const char *start = buf;

#ifdef HAVE_INET_NTOP
#ifdef HAVE_IPV6
    if (SGFAM(*ia) == AF_INET6)
    {
	if (IN6_IS_ADDR_V4MAPPED((struct in6_addr *) SGADDRP(*ia)))
	{
	    struct in_addr iab;

	    IN6_V4MAPPED_TO_INADDR((struct in6_addr *) SGADDRP(*ia), &iab);
	    
	    if (inet_ntop(AF_INET, &iab, buf, bufsize) == NULL)
		return NULL;
	    
	    bufsize -= strlen(buf);
	}
	else
	{
	    *buf++ = '[';
	    bufsize--;
	    if (bufsize <= 1)
		return NULL;
	    
	    if (inet_ntop(SGFAM(*ia), SGADDRP(*ia), buf, bufsize) == NULL)
		return NULL;
	    
	    if (strlcat(buf, "]", bufsize) >= bufsize)
	    {
		syslog(LOG_ERR, "s_inet_ntox: buffer overflow");
		return NULL;
	    }
		    
	    bufsize -= strlen(buf);
	}
    }
    else
#endif
    {
	if (inet_ntop(SGFAM(*ia), SGADDRP(*ia), buf, bufsize) == NULL)
	    return NULL;
	bufsize -= strlen(buf);
    }

#if 0
    snprintf(buf+strlen(buf), bufsize, ":%d", SGPORT(*ia));
#endif
    
    return start;
    
#else
    
    unsigned char *bp;

    bp = (unsigned char *) &ia;
    
    if (s_snprintf(buf, bufsize, "%u.%u.%u.%u", bp[0], bp[1], bp[2], bp[3]) < 0)
	return NULL;

    return buf;
#endif
    }
