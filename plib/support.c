/*
** support.c - Miscellaneous support functions.
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

#define PLIB_IN_SUPPORT_C

#include "plib/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <pwd.h>
#include <grp.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef HAVE_UNAME
#include <sys/utsname.h>
#endif

#include "plib/threads.h"
#include "plib/safestr.h"
#include "plib/support.h"
#include "plib/sockaddr.h"


/*
** Get the OS name and version number
*/
char *
osinfo_get(char *buf,
	   int bufsize)
{
#ifdef HAVE_UNAME
    struct utsname ub;

    if (uname(&ub) < 0)
	return NULL;
    
#ifndef _AIX
    s_snprintf(buf, bufsize, "%s %s", ub.sysname, ub.release);
#else
    s_snprintf(buf, bufsize, "%s %s.%s", ub.sysname, ub.version, ub.release);
#endif
#else
    if (strlcpy(buf, "<unknown>", bufsize) >= bufsize)
	return NULL;
#endif

    return buf;
}



/*
** Classify what type of socket the file descript "fd" is
*/
int
socktype(int fd)
{
    struct sockaddr_gen remote_sin, local_sin;
    socklen_t len;
    int code;


    /* Try to get the local socket adress and port number */
    len = sizeof(local_sin);
    code = getsockname(fd, (struct sockaddr *) &local_sin, &len);
    if (code < 0)
    {
	if (errno == ENOTSOCK || errno == EINVAL)
	    /* Not a TCP/IP socket */
	    return SOCKTYPE_NOTSOCKET; 
	else
	    return -1;
    }

     
    /* Try to get the remote socket adress and port number */
    len = sizeof(remote_sin);
    code = getpeername(fd, (struct sockaddr *) &remote_sin, &len);
    if (code < 0)
    {
	if (errno == ENOTCONN)
	    /* Locally bound TCP socket, awaiting connections */
	    return SOCKTYPE_LISTEN; 
	else
	    return -1;
    }
    
    /* Established TCP connection */
    return SOCKTYPE_CONNECTED; 
}



#ifndef HAVE_STRERROR
char *
strerror(int err)
{
#ifdef NEED_SYS_ERRLIST
    extern int sys_nerr;
    extern char *sys_errlist[];
#endif
    static char errbuf[64];

    if (err < 0 || err >= sys_nerr)
    {
	if (s_snprintf(errbuf, sizeof(errbuf), "#%d", err) < 0)
	    return "<unknown>";
	
	return errbuf;
    }
    else
	return sys_errlist[err];
}
#endif




#if !defined(HAVE_GETPWNAM_R) || !defined(HAVE_GETPWUID_R)
static pthread_mutex_t pwd_lock;
static pthread_once_t pwd_once = PTHREAD_ONCE_INIT;

static void
pwd_lock_init(void)
{
    pthread_mutex_init(&pwd_lock, NULL);
}

static char *
strcopy(const char *str, char **buf, size_t *avail)
{
    char *start;
    size_t len;

    
    if (str == NULL)
	return NULL;

    len = strlen(str)+1;
    if (len > *avail)
	return NULL;

    start = *buf;
    
    memcpy(*buf, str, len);
    *avail -= len;
    *buf += len;
    
    return start;
}
#endif


int
s_getpwnam_r(const char *name,
	   struct passwd *pwd,
	   char *buffer, size_t bufsize,
	   struct passwd **result)
{
#ifdef HAVE_GETPWNAM_R
    int code;

    
    memset(pwd, 0, sizeof(*pwd));
    memset(buffer, 0, bufsize);

#ifdef HAVE_UI_GETPW /* Unix International / Solaris / UnixWare */
    
    while ((*result = getpwnam_r(name, pwd, buffer, bufsize)) == NULL &&
	   errno == EINTR)
	;

    if (*result == NULL)
	code = errno;
    else
	code = 0;
    
#elif HAVE_DCE_GETPW /* DCE/CMA */
    
    while ((code = getpwnam_r(name, pwd, buffer, bufsize)) != 0 &&
	   errno == EINTR)
	;
    if (code == 0)
	*result = pwd;
    else
	code = errno;
    
#else /* Posix version */
    
    while ((code = getpwnam_r(name, pwd, buffer, bufsize, result)) == EINTR)
	;

#endif
    
    return code;
    
#else /* No reentrant getpw*_r calls available */
    
    struct passwd *pp;

    pthread_once(&pwd_once, pwd_lock_init);
    
    pthread_mutex_lock(&pwd_lock);
    
    pp = getpwnam(name);
    if (pp == NULL)
    {
	pthread_mutex_unlock(&pwd_lock);
	*result = NULL;
	return -1;
    }

    memset(pwd, 0, sizeof(*pwd));
    
    pwd->pw_name = strcopy(pp->pw_name, &buffer, &bufsize);
    pwd->pw_passwd = strcopy(pp->pw_passwd, &buffer, &bufsize);
    pwd->pw_uid = pp->pw_uid;
    pwd->pw_gid = pp->pw_gid;
    pwd->pw_gecos = strcopy(pp->pw_gecos, &buffer, &bufsize);
    pwd->pw_dir = strcopy(pp->pw_dir, &buffer, &bufsize);
    pwd->pw_shell = strcopy(pp->pw_shell, &buffer, &bufsize);

    *result = pwd;
    
    pthread_mutex_unlock(&pwd_lock);
    return 0;
#endif
}



int
s_getpwuid_r(uid_t uid,
	     struct passwd *pwd,
	     char *buffer, size_t bufsize,
	     struct passwd **result)
{
#ifdef HAVE_GETPWUID_R
    int code;

    
    memset(pwd, 0, sizeof(*pwd));
    memset(buffer, 0, bufsize);

#ifdef HAVE_UI_GETPW /* Unix International / Solaris / UnixWare */
    
    while ((*result = getpwuid_r(uid, pwd, buffer, bufsize)) == NULL &&
	   errno == EINTR)
	;

    if (*result == NULL)
	code = errno;
    else
	code = 0;
    
#elif HAVE_DCE_GETPW /* DCE/CMA */
    
    while ((code = getpwuid_r(uid, pwd, buffer, bufsize)) != 0 &&
	   errno == EINTR)
	;
    if (code == 0)
	*result = pwd;
    else
	code = errno;
    
#else /* Posix version */
    
    while ((code = getpwuid_r(uid, pwd, buffer, bufsize, result)) == EINTR)
	;
    
#endif
    
    return code;
    
#else
    struct passwd *pp;

    pthread_once(&pwd_once, pwd_lock_init);
    pthread_mutex_lock(&pwd_lock);

    pp = getpwuid(uid);
    if (pp == NULL)
    {
	pthread_mutex_unlock(&pwd_lock);

	*result = NULL;
	return -1;
    }

    memset(pwd, 0, sizeof(*pwd));
    
    pwd->pw_name = strcopy(pp->pw_name, &buffer, &bufsize);
    pwd->pw_passwd = strcopy(pp->pw_passwd, &buffer, &bufsize);
    pwd->pw_uid = pp->pw_uid;
    pwd->pw_gid = pp->pw_gid;
    pwd->pw_gecos = strcopy(pp->pw_gecos, &buffer, &bufsize);
    pwd->pw_dir = strcopy(pp->pw_dir, &buffer, &bufsize);
    pwd->pw_shell = strcopy(pp->pw_shell, &buffer, &bufsize);

    *result = pwd;
    
    pthread_mutex_unlock(&pwd_lock);
    
    return 0;
#endif
}


int
s_getgrgid_r(gid_t gid,
	     struct group *grp,
	     char *buffer, size_t bufsize,
	     struct group **result)
{
#ifdef HAVE_GETPWUID_R
    int code;

    
    memset(grp, 0, sizeof(*grp));
    memset(buffer, 0, bufsize);

#ifdef HAVE_UI_GETPW /* Unix International / Solaris / UnixWare */
    
    while ((*result = getgrgid_r(gid, grp, buffer, bufsize)) == NULL &&
	   errno == EINTR)
	;

    if (*result == NULL)
	code = errno;
    else
	code = 0;
    
#elif HAVE_DCE_GETPW /* DCE/CMA */
    
    while ((code = getgrgid_r(gid, grp, buffer, bufsize)) != 0 &&
	   errno == EINTR)
	;
    if (code == 0)
	*result = pwd;
    else
	code = errno;
    
#else /* Posix version */
    
    while ((code = getgrgid_r(gid, grp, buffer, bufsize, result)) == EINTR)
	;
    
#endif
    
    return code;
    
#else
    struct group *gp;
    int i, len;

    
    pthread_once(&grp_once, grp_lock_init);
    pthread_mutex_lock(&grp_lock);

    gp = getgrgid(gid);
    if (gp == NULL)
    {
	pthread_mutex_unlock(&grp_lock);

	*result = NULL;
	return -1;
    }

    memset(grp, 0, sizeof(*grp));
    
    grp->gr_name = strcopy(gp->gr_name, &buffer, &bufsize);
    grp->gr_passwd = strcopy(gp->gr_passwd, &buffer, &bufsize);
    grp->gr_gid = gp->gr_gid;

    for (i = 0; gp->gr_mem[i]; ++i)
	;
    len = i;
    
    grp->gr_mem = a_malloc(sizeof(char *) * (len+1));
    for (i = 0; i < len; ++i)
	grp->gr_mem[i] = strcopy(gp->gr_mem[i], &buffer, &bufsize);
    grp->gr_mem[i] = NULL;

    *result = grp;
    
    pthread_mutex_unlock(&grp_lock);
    
    return 0;
#endif
}

int
s_getgrnam_r(const char *name,
	     struct group *grp,
	     char *buffer, size_t bufsize,
	     struct group **result)
{
#ifdef HAVE_GETPWNAM_R
    int code;

    
    memset(grp, 0, sizeof(*grp));
    memset(buffer, 0, bufsize);

#ifdef HAVE_UI_GETPW /* Unix International / Solaris / UnixWare */
    
    while ((*result = getgrname_r(name, grp, buffer, bufsize)) == NULL &&
	   errno == EINTR)
	;

    if (*result == NULL)
	code = errno;
    else
	code = 0;
    
#elif HAVE_DCE_GETPW /* DCE/CMA */
    
    while ((code = getgrnam_r(name, grp, buffer, bufsize)) != 0 &&
	   errno == EINTR)
	;
    if (code == 0)
	*result = pwd;
    else
	code = errno;
    
#else /* Posix version */
    
    while ((code = getgrnam_r(name, grp, buffer, bufsize, result)) == EINTR)
	;
    
#endif
    
    return code;
    
#else
    struct group *gp;
    int i, len;

    
    pthread_once(&grp_once, grp_lock_init);
    pthread_mutex_lock(&grp_lock);

    gp = getgrnam(name);
    if (gp == NULL)
    {
	pthread_mutex_unlock(&grp_lock);

	*result = NULL;
	return -1;
    }

    memset(grp, 0, sizeof(*grp));
    
    grp->gr_name = strcopy(gp->gr_name, &buffer, &bufsize);
    grp->gr_passwd = strcopy(gp->gr_passwd, &buffer, &bufsize);
    grp->gr_gid = gp->gr_gid;

    for (i = 0; gp->gr_mem[i]; ++i)
	;
    len = i;
    
    grp->gr_mem = a_malloc(sizeof(char *) * (len+1));
    for (i = 0; i < len; ++i)
	grp->gr_mem[i] = strcopy(gp->gr_mem[i], &buffer, &bufsize);
    grp->gr_mem[i] = NULL;

    *result = grp;
    
    pthread_mutex_unlock(&grp_lock);
    
    return 0;
#endif
}


    

void
s_openlog(const char *ident, int logopt, int facility)
{
    openlog(ident, logopt
#ifdef LOG_DAEMON
	    , facility
#endif
	    );
}


#ifdef LOG_KERN
static struct logfacname
{
    const char *name;
    int code;
} facility[] =
{
    { "kern", 	LOG_KERN },
    { "user", 	LOG_USER },
    { "mail", 	LOG_MAIL },
    { "daemon", LOG_DAEMON },
    { "auth",	LOG_AUTH },
    { "syslog",	LOG_SYSLOG },
    { "lpr", 	LOG_LPR },
#ifdef LOG_NEWS
    { "news",   LOG_NEWS },
#endif
#ifdef LOG_UUCP
    { "uucp", 	LOG_UUCP },
#endif
#ifdef LOG_CRON
    { "cron", 	LOG_CRON },
#endif
    { "local0",	LOG_LOCAL0 },
    { "local1",	LOG_LOCAL1 },
    { "local2",	LOG_LOCAL2 },
    { "local3",	LOG_LOCAL3 },
    { "local4",	LOG_LOCAL4 },
    { "local5",	LOG_LOCAL5 },
    { "local6",	LOG_LOCAL6 },
    { "local7",	LOG_LOCAL7 },
    { NULL, -1 }
};


int
syslog_str2fac(const char *name)
{
    int i;

    if (name == NULL)
	return -1;

    if (isdigit(*((unsigned char *)name)))
	return atoi(name);
	
    for (i = 0; facility[i].name != NULL &&
	     s_strcasecmp(facility[i].name, name) != 0; i++)
	;

    return facility[i].code;
}

#else /* !LOG_KERN */

int
syslog_str2fac(const char *name)
{
    return 0;
}
#endif


#ifdef LOG_EMERG
static struct loglevname
{
    const char *name;
    int level;
} level[] =
{
    { "emerg", 	 LOG_EMERG },
    { "alert",   LOG_ALERT },
    { "crit",    LOG_CRIT },
    { "err",     LOG_ERR },
    { "warning", LOG_WARNING },
    { "notice",  LOG_NOTICE },
    { "info",    LOG_INFO },
    { "debug",   LOG_DEBUG },

    { NULL, -1 }
};


int
syslog_str2lev(const char *name)
{
    int i;

    if (name == NULL)
	return -1;
    
    if (isdigit(*((unsigned char *)name)))
	return atoi(name);
	
    for (i = 0; level[i].name != NULL &&
	     s_strcasecmp(level[i].name, name) != 0; i++)
	;

    return level[i].level;
}

#else /* !LOG_KERN */

int
syslog_str2fac(const char *name)
{
    return 0;
}
#endif



