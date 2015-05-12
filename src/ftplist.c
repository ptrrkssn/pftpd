/*
** ftplist.c - File listing functionality
**
** Copyright (c) 1994-2000 Peter Eriksson <pen@lysator.liu.se>
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
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_MKDEV_H
#include <sys/mkdev.h>
#endif

#include "pftpd.h"

#include "plib/aalloc.h"
#include "plib/safestr.h"
#include "plib/support.h"
#include "plib/dirlist.h"
#include "plib/strmatch.h"


#ifndef major
#define major(d) (((d)>>8) & 0xFF)
#define minor(d) (((d)   ) & 0xFF)
#endif


int hide_uidgid = 0;


static char *
mode2str(int mode,
	 char *buf,
	 int size)
{
    if (size < 11)
	return NULL;

    switch (S_IFMT & mode)
    {
      case S_IFIFO:
	buf[0] = 'p';
	break;
	
      case S_IFCHR:
	buf[0] = 'c';
	break;
	
      case S_IFDIR:
	buf[0] = 'd';
	break;
	
      case S_IFBLK:
	buf[0] = 'b';
	break;
	
      case S_IFREG:
	buf[0] = '-';
	break;
	
      case S_IFLNK:
	buf[0] = 'l';
	break;
	
      case S_IFSOCK:
	buf[0] = 's';
	break;

#ifdef S_IFDOOR
      case S_IFDOOR:
	buf[0] = 'D';
	break;
#endif
	
      default:
	buf[0] = '?'; /* Unknown file type */
	break;
    }	
	
    buf[1] = (mode & S_IRUSR) ? 'r' : '-';
    buf[2] = (mode & S_IWUSR) ? 'w' : '-';
    buf[3] = (mode & S_IXUSR) ? 'x' : '-';
    buf[4] = (mode & S_IRGRP) ? 'r' : '-';
    buf[5] = (mode & S_IWGRP) ? 'w' : '-';
    buf[6] = (mode & S_IXGRP) ? 'x' : '-';
    buf[7] = (mode & S_IROTH) ? 'r' : '-';
    buf[8] = (mode & S_IWOTH) ? 'w' : '-';
    buf[9] = (mode & S_IXOTH) ? 'x' : '-';
    buf[10] = '\0';

    return buf;
}


static char *
time2str(time_t ot,
	 char *buf,
	 int size)
{
    time_t ct;
    struct tm tb;
    
    time(&ct);
#ifdef HAVE_THREADS /* XXX: Fixme, should be solved in safeio.c */
    localtime_r(&ot, &tb);
#else
    tb = *localtime(&ot);
#endif
    
    if (ct - ot > 15552000)
	strftime(buf, size, "%b %e  %Y", &tb);
    else
	strftime(buf, size, "%b %e %H:%M", &tb);

    return buf;
}






static void
send_modechar(FDBUF *fp,
	      mode_t mode)
{
    switch (S_IFMT & mode)
    {
      case S_IFDIR:
	fd_putc(fp, '/');
	break;
	
      case S_IFLNK:
	fd_putc(fp, '@');
	break;
	
      case S_IFCHR:
	fd_putc(fp, '%');
	break;
	
      case S_IFBLK:
	fd_putc(fp, '#');
	break;
	
      case S_IFIFO:
	fd_putc(fp, '|');
	break;
	
      case S_IFSOCK:
	fd_putc(fp, '=');
	break;

#ifdef S_IFDOOR
      case S_IFDOOR:
	fd_putc(fp, '>');
	break;
#endif
    }
}


static char *
uid2str(uid_t uid,
	char *buf,
	size_t bufsize)
{
    struct passwd pwb, *pwp;
    char pbuf[2048];
    int clen;
    
    
    if (hide_uidgid)
    {
	clen = strlcpy(buf, "ftp", bufsize);
	if (clen >= bufsize)
	    return NULL;
	return buf;
    }

    if (s_getpwuid_r(uid, &pwb, pbuf, sizeof(pbuf), &pwp) != 0 ||
	pwp == NULL)
    {
	if (s_snprintf(buf, bufsize, "%d", uid) < 0)
	    return NULL;
	
	return buf;
    }

    clen = strlcpy(buf, pwp->pw_name, bufsize);
    if (clen >= bufsize)
	return NULL;

    return buf;
}


static char *
gid2str(gid_t gid,
	char *buf,
	size_t bufsize)
{
    struct group grb, *grp;
    char gbuf[2048];
    int clen;

    
    if (hide_uidgid)
    {
	clen = strlcpy(buf, "ftp", bufsize);
	if (clen >= bufsize)
	    return NULL;
	return buf;
    }

    if (s_getgrgid_r(gid, &grb, gbuf, sizeof(gbuf), &grp) == 0 &&
	grp != NULL)
    {
	clen = strlcpy(buf, grp->gr_name, bufsize);
	if (clen >= bufsize)
	    return NULL;
	
	return buf;
    }
    
    if (s_snprintf(buf, bufsize, "%d", gid) < 0)
	return NULL;
    
    return buf;
}

#if 0
static int
strwildmatch(const char *string, const char *pattern)
{
    const char *ssubstart = NULL, *psubstart = NULL;
    char sch, pch;

    for (; (sch = *string); string++, pattern++)
    {
	pch = *pattern;
	if (pch == '*')
	{
	    ssubstart = string;
	    do
	    {
		psubstart = pattern;
		pch = *++pattern;
	    }
	    while (pch == '*');
	    if (pch == '\0')
		return 1;
	}

	if (pch == '?')
	    continue;

	if (pch == '\\')
	    pch = *++pattern;

	if (pch == sch)
	    continue;

	if (psubstart == NULL)
	    return 0;

	pattern = psubstart;
	string = ssubstart++;
    }

    while (*pattern == '*')
	pattern++;
    return (*pattern == '\0');
}
#endif

static int
send_file_listing(FDBUF *fp,
		  const char *rpath,
		  int flags,
		  char *match)
{
    DIRLIST *dlp;
    struct dirent *dep;
    struct stat *sp;
    int i;
    

    dlp = dirlist_get(rpath);
    if (dlp == NULL)
	return -1;

    for (i = 0; i < dlp->dec; i++)
    {
	dep = dlp->dev[i].d;
	sp = &dlp->dev[i].s;
	
	if (flags & LS_LONG)
	{
	    /* Verbose listing */
	    char modebuf[12];
	    char timebuf[13];
	    char pathbuf[2048];
	    char ubuf[128];
	    char gbuf[128];

	    if (dep->d_name[0] == '.' &&
		!(strcmp(dep->d_name, ".") == 0 || strcmp(dep->d_name, "..") == 0) &&
		!(flags & LS_ALL))
		continue;

	    if ((flags & LS_SKIPDOTDOT) && strcmp(dep->d_name, "..") == 0)
		continue;

	    if (match && !strmatch(dep->d_name, match))
		continue;
	    
	    fd_printf(fp, "%10s %3d %-8s %-8s ",
		      mode2str(sp->st_mode, modebuf, sizeof(modebuf)),
		      sp->st_nlink,
		      uid2str(sp->st_uid, ubuf, sizeof(ubuf)),
		      gid2str(sp->st_gid, gbuf, sizeof(gbuf)));
	    
	    switch (S_IFMT & sp->st_mode)
	    {
	      case S_IFCHR:
	      case S_IFBLK:
		fd_printf(fp, "%3d,%3d",
			  major(sp->st_rdev),
			  minor(sp->st_rdev));
		break;
		
	      default:
		fd_printf(fp, "%7d", sp->st_size);
	    }
	    
	    fd_printf(fp, " %12s %s",
		      time2str(sp->st_mtime, timebuf, sizeof(timebuf)),
		      dep->d_name);

	    if ((flags & LS_FTYPE) && !S_ISLNK(sp->st_mode))
		send_modechar(fp, sp->st_mode);

	    if (S_ISLNK(sp->st_mode))
	    {
		char lbuf[2048];
		int lbuflen;


		
		strlcpy(pathbuf, rpath, sizeof(pathbuf));
		strlcat(pathbuf, "/", sizeof(pathbuf));
		if (strlcat(pathbuf, dep->d_name, sizeof(pathbuf)) <
		    sizeof(pathbuf))
		{
		    lbuflen = readlink(pathbuf, lbuf, sizeof(lbuf));
		    if (lbuflen < 0)
			lbuflen = 0;
		    lbuf[lbuflen] = '\0';
		    
		    /* XXX: Check for absolute symlinks pointing
		       above server_root_dir when not using chroot? */
		    
		    fd_printf(fp, " -> %s", lbuf);
		}
		else
		    fd_puts(fp, " ->");
	    }
	    fd_putc(fp, '\n');
	}
	else
	{
	    /* Brief listing */
	    
	    if (dep->d_name[0] == '.' &&
		!(strcmp(dep->d_name, ".") == 0 || strcmp(dep->d_name, "..") == 0) &&
		!(flags & LS_ALL))
		continue;

	    if ((flags & LS_SKIPDOTDOT) && strcmp(dep->d_name, "..") == 0)
		continue;

	    if (match && !strmatch(dep->d_name, match))
		continue;
	    
	    fd_puts(fp, dep->d_name);
	    if (flags & LS_FTYPE)
		send_modechar(fp, sp->st_mode);
	    
	    fd_putc(fp, '\n');
	}
    }
	
    dirlist_free(dlp);
    return 0;
}





typedef struct
{
    char *rpath;
    char *vpath;
    char *match;
    int flags;
} FTPDATA_LIST;


static int
list_thread(FTPCLIENT *fp,
	    void *vp)
{
    FTPDATA_LIST *lp = (FTPDATA_LIST *) vp;
    FDBUF *data_fdp;
    int rc;

    if (debug)
	fprintf(stderr, "list_thread: start\n");
    
    data_fdp = fd_create(fp->data->fd, FDF_CRLF);

    if (send_file_listing(data_fdp, lp->rpath, lp->flags, lp->match))
    {
	fd_printf(fp->fd, "550: %s: %s.\n", lp->vpath, strerror(errno));
	rc = 0;
    }
    else
	rc = 226;
    
    fd_destroy(data_fdp);
    a_free(lp->rpath);
    a_free(lp->vpath);
    a_free(lp->match);
    a_free(lp);
    
    if (debug)
	fprintf(stderr, "list_thread: stop\n");
    
    return rc;

}

static int
do_listing(FTPCLIENT *fp,
	   const char *arg,
	   int ls_flags)
{
    char vbuf[2048], rbuf[2048];
    char *vpath, *rpath, *lp, *match = NULL;
    struct stat sb;
    FTPDATA_LIST *flp;

    
    if (arg == NULL)
	arg = ".";

    while (*arg == '-')
    {
	++arg;
	while (*arg && *arg != ' ' && *arg != '\t')
	    switch (*arg++)
	    {
	      case 'F':
		ls_flags |= LS_FTYPE;
		break;
		
	      case 'a':
		ls_flags |= LS_ALL;
		break;

	      case 'l':
		ls_flags |= LS_LONG;
		break;

	      case 'R':
		return 501;
	    }

	while (*arg && (*arg == ' ' || *arg == '\t'))
	    ++arg;
    }
    
    vpath = path_mk(fp, arg, vbuf, sizeof(vbuf));
    
    lp = strrchr(vpath, '/');
    if (!lp)
    {
	syslog(LOG_CRIT, "internal error: no / in vpath: %s\n", vpath);
	return 501;
    }

    if (strchr(lp, '*') || strchr(lp, '?') ||
	strchr(lp, '\\') || strchr(lp, '['))
    {
	*lp++ = '\0';
	match = lp;
    }
    
    rpath = path_v2r(vpath, rbuf, sizeof(rbuf));

    if (debug > 1)
	fprintf(stderr, "vpath=%s, rpath=%s, match=%s\n",
		vpath ? vpath : "<null>",
		rpath ? rpath : "<null>",
		match ? match : "<null>");
    
    if (rpath == NULL)
	return 501;

    if (stat(rpath, &sb) < 0)
    {
	fd_printf(fp->fd, "550 %s: %s.\n", vpath, strerror(errno));
	return 0;
    }

#if 0
    if (!S_ISDIR(sb.st_mode))
    {
	fd_printf(fp->fd, "550 %s: Not a directory.\n", vpath);
	return 0;
    }
#endif
    
    flp = a_malloc(sizeof(*flp), "FTPDATA_LIST");
    flp->vpath = a_strdup(vpath, "FTPDATA_LIST vpath");
    flp->rpath = a_strdup(rpath, "FTPDATA_LIST rpath");
    flp->match = a_strdup(match, "FTPDATA_LIST match");
    flp->flags = ls_flags |
	((strcmp(vpath, "/") == 0 || *vpath == '\0') ? LS_SKIPDOTDOT : 0);
    
    if (ftpdata_start(fp, "file listing", list_thread, (void *) flp) == NULL)
    {
	fd_puts(fp->fd, "425 Can not build data connection.\n");
	
	a_free(flp->rpath);
	a_free(flp->vpath);
	a_free(flp->match);
	a_free(flp);
    }
    
    return 0;
}



int
ftplist_send(FTPCLIENT *fp,
	     const char *path,
	     int flags)
{
    if (flags & LS_FTPDATA)
	return do_listing(fp, path, flags & ~LS_FTPDATA);
    else
	return send_file_listing(fp->fd, path, flags, NULL); /* Match? */
}
