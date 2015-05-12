/*
** path.c
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <sys/types.h>

#include "pftpd.h"

#include "plib/aalloc.h"
#include "plib/safestr.h"
#include "plib/support.h"


char *user_ftp_dir = NULL;
char *server_ftp_dir = NULL;

/*
** Validate, merge CWD+ARG and trim virtual path
*/
char *
path_mk(FTPCLIENT *fp,
	const char *arg,
	char *buf,
	size_t bufsize)
{
    char *path, *cp;
    int alen, clen;


    if (arg == NULL)
	return NULL;

    if (debug > 2)
	fprintf(stderr, "path_mk, at start: cwd=%s, arg=%s\n",
		fp->cwd, arg);

    alen = strlen(arg);
    
    if (arg[0] == '/')
    {
	if (buf)
	{
	    if (alen+1 > bufsize)
		return NULL;

	    path = buf;
	}
	else
	{
	    bufsize = alen+1;
	    path = a_malloc(bufsize, "path_mk: path");
	}
	
	if (strlcpy(path, arg, bufsize) >= bufsize)
	{
	    if (path != buf)
		a_free(path);
	    return NULL;
	}
    }
    else
    {
	clen = strlen(fp->cwd);

	if (buf)
	{
	    if (clen+alen+1 > bufsize)
		return NULL;
	    path = buf;
	}
	else
	{
	    bufsize = clen + alen + 2;
	    path = a_malloc(bufsize, "path_mk: path");
	}
	
	if (strlcpy(path, fp->cwd, bufsize) >= bufsize)
	{
	    if (path != buf)
		a_free(path);
	    return NULL;
	}
	
	if (clen > 0 && path[clen-1] != '/')
	    path[clen++] = '/';
	
	if (strlcpy(path+clen, arg, bufsize-clen) >= bufsize-clen)
	{
	    if (path != buf)
		a_free(path);
	    return NULL;
	}
    }

    if (debug > 2)
	fprintf(stderr, "path_mk, after merge: path=%s\n", path);
    
    /* Trim any '..' components from the path */
    cp = path;
    while (*cp)
    {
	if (cp[0] == '/' &&
	    ((cp > path && cp[1] == '\0') || cp[1] == '/'))
	{
	    strlcpy(cp, cp+1, bufsize - (cp-path));
	}
	else if (cp[0] == '/' &&
		 cp[1] == '.' &&
		 (cp[2] == '\0' || cp[2] == '/'))
	{
	    strlcpy(cp, cp+2, bufsize - (cp-path));
	}
	else if (cp[0] == '/' &&
		 cp[1] == '.' &&
		 cp[2] == '.' &&
		 (cp[3] == '\0' || cp[3] == '/'))
	{
	    char *rest = cp+3;
	    while (cp > path && *--cp != '/')
		;
	    strlcpy(cp, rest, bufsize - (cp-path));
	}
	else
	    ++cp;
    }

    
    if (*path == '\0')
	strlcpy(path, "/", bufsize - (cp-path));
    
    if (debug > 2)
	fprintf(stderr, "path_mk, after trim: path=%s\n", path);
    
    return path;
}


char *
path_v2r(const char *path,
	 char *buf,
	 size_t bufsize)
{
    int blen, plen, dlen;


    if (path == NULL)
	return NULL;

    if (user_ftp_dir &&
	path[0] == '/' &&
	path[1] == '~')
    {
	/* User's public FTP dir */
	struct passwd pwb, *pwp = NULL;
	char user[128];
	char tmpbuf[2048], *cp;
	int err, len;
	

	cp = strchr(path+2, '/');
	if (cp)
	{
	    len = cp - (path+2);
	    if (len >= sizeof(user))
		return NULL;
	    
	    memcpy(user, path+2, len);
	    user[len] = '\0';
	}
	else
	{
	    strlcpy(user, path+2, sizeof(user));
	    cp = "";
	}

	if (debug)
	    fprintf(stderr, "[%s]\n", user);
	
	err = s_getpwnam_r(user, &pwb, tmpbuf, sizeof(tmpbuf), &pwp);
	if (err || pwp == NULL)
	    return NULL;

	blen = strlen(pwp->pw_dir);
	dlen = strlen(user_ftp_dir);
	plen = strlen(path);
	
	if (buf)
	{
	    if (blen+dlen+plen+2 > bufsize)
		return NULL;
	}
	else
	{
	    bufsize = blen+dlen+plen+2;
	    buf = a_malloc(bufsize, "path_v2r: buf");
	}
	
	strlcpy(buf, pwp->pw_dir, bufsize);
	buf[blen] = '/';
	strlcpy(buf+blen+1, user_ftp_dir, bufsize - (blen+1));
	strlcpy(buf+dlen+blen+1, cp, bufsize - (dlen+blen+1));

	if (debug)
	    fprintf(stderr, "path_v2r: %s -> %s\n", path, buf);
	
	return buf;
    }

    blen = strlen(server_ftp_dir);
    plen = strlen(path);

    if (buf)
    {
	if (blen+plen+1 > bufsize)
	    return NULL;
    }
    else
    {
	bufsize = blen + plen + 1;
	buf = a_malloc(bufsize, "path_v2r: buf");
    }
    
    strlcpy(buf, server_ftp_dir, bufsize);
    strlcpy(buf+blen, path, bufsize - blen);

    if (debug)
	fprintf(stderr, "path_v2r: %s -> %s\n", path, buf);
	
    return buf;
}


