/*
** dirlist.c - Get a directory listing
**
** Copyright (c) 1999-2000 Peter Eriksson <pen@lysator.liu.se>
**
** This program is free software; you can redistribute it and/or
** modify it as you wish - as long as you don't claim that you wrote
** it.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#include "plib/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "plib/safeio.h"
#include "plib/safestr.h"
#include "plib/dirlist.h"
#include "plib/strl.h"
#include "plib/aalloc.h"



/* XXX: Should perhaps be dynamically allocated? */
#define MAX_PATHNAMELEN 2048

int dirlist_use_lstat = 1;


static int
dirent_compare(const void *e1,
	       const void *e2)
{
    const struct dirlist_ent *d1, *d2;
    

    d1 = (struct dirlist_ent *) e1;
    d2 = (struct dirlist_ent *) e2;

    if (S_ISDIR(d1->s.st_mode) &&
	!S_ISDIR(d2->s.st_mode))
	return -1;

    if (!S_ISDIR(d1->s.st_mode) &&
	S_ISDIR(d2->s.st_mode))
	return 1;
	
    return strcmp(d1->d->d_name, d2->d->d_name);
}


static struct dirent *
dirent_dup(const struct dirent *dep)
{
    struct dirent *n_dep;
    size_t len;
    
#if 0
    len = sizeof(*dep)+dep->d_reclen;
    n_dep = a_malloc(len, "struct dirent");
    memcpy(n_dep, dep, len);
#else
    n_dep = a_malloc(len = dep->d_reclen, "struct dirent");
#endif
    memcpy(n_dep, dep, len);
    
    return n_dep;
}


static struct dirent *
dirent_alloc(ino_t ino, const char *name)
{
    struct dirent *dp;
    int len;



    len = strlen(name);

#if 0
    fprintf(stderr, "dirent_alloc: len=%d, name=%s\n",
	    len, name);
#endif
	    
    dp = a_malloc(sizeof(*dp)+len, "struct dirent");
    dp->d_ino = ino;
    dp->d_off = 0;
    dp->d_reclen = len;
    strcpy(dp->d_name, name);

    return dp;
}


DIRLIST *
dirlist_get(const char *path)
{
    DIR *dp;
    DIRLIST *dlp;
    struct dirent *dep;
    char p_buf[MAX_PATHNAMELEN], *pp, *pend;
    int err, p_len;
    struct stat sb;
    const char *cp;
    

    if (path == NULL)
	return NULL;

    dp = opendir(path);
    if (dp == NULL)
    {
	if (dirlist_use_lstat)
	    err = lstat(path, &sb);
	else
	    err = stat(path, &sb);

	if (err)
	    return NULL;

	A_NEW(dlp);
	dlp->dec = 0;
	dlp->des = 2;
	dlp->dev = a_malloc(dlp->des * sizeof(dlp->dev[0]), "DIRLIST dev[]");

	cp = strrchr(path, '/');
	if (cp)
	    ++cp;
	else
	    cp = path;
	
	dlp->dev[dlp->dec].s = sb;
	dlp->dev[dlp->dec].d = dirent_alloc(sb.st_ino, cp);

	dlp->dec++;
	dlp->dev[dlp->dec].d = NULL;
	
	return dlp;
    }

    p_len = strlcpy(p_buf, path, sizeof(p_buf));
    if (p_len >= sizeof(p_buf))
	return NULL;

    pp = p_buf+p_len;
    if (pp > p_buf && pp[-1] != '/')
    {
	if (p_len + 1 >= sizeof(p_buf))
	    return NULL;
	
	*pp++ = '/';
	*pp = '\0';
    }

    pend = pp;
    
    A_NEW(dlp);
    
    dlp->dec = 0;
    dlp->des = 64;
    dlp->dev = a_malloc(dlp->des * sizeof(dlp->dev[0]), "DIRLIST dev[]");

    while ((dep = readdir(dp)) != NULL)
    {
#if 0
	fprintf(stderr, "dirent_get: readdir: dep->d_reclen = %d, dep->d_name = %s\n",
		dep->d_reclen, dep->d_name);
#endif
	
	*pend = '\0';
	
	if (dlp->dec == dlp->des - 1)
	{
	    dlp->des += 64;
	    dlp->dev = a_realloc(dlp->dev,
				 dlp->des * sizeof(dlp->dev[0]),
				 "DIRENT dev[]");
	}

	dlp->dev[dlp->dec].d = dirent_dup(dep);

	if (strlcat(p_buf, dep->d_name, sizeof(p_buf)) >= sizeof(p_buf))
	{
	    syslog(LOG_ERR, "dirlist_get: path too long (skipped): %s", p_buf);
	    continue;
	}
	
	if (dirlist_use_lstat)
	    err = lstat(p_buf, &dlp->dev[dlp->dec].s);
	else
	    err = stat(p_buf, &dlp->dev[dlp->dec].s);

	if (err)
	{
	    syslog(LOG_WARNING, "dirlist_get: stat(%s) failed: %m\n", p_buf);
	    continue;
	}
	
	dlp->dec++;
    }
    
    closedir(dp);
    dlp->dev[dlp->dec].d = NULL;

    qsort(dlp->dev, dlp->dec, sizeof(dlp->dev[0]), dirent_compare);

    return dlp;
}


void
dirlist_free(DIRLIST *dlp)
{
    int i;


    if (dlp == NULL)
	return;
    
    for (i = 0; i < dlp->dec; i++)
	a_free(dlp->dev[i].d);
    a_free(dlp->dev);
    a_free(dlp);
}
