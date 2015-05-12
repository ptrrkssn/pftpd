/*
** str2.c - String to Foo conversion routines.
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
#include <ctype.h>
#include <string.h>

#include <pwd.h>

#include <grp.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "plib/safestr.h"
#include "plib/support.h"
#include "plib/str2.h"
#include "plib/aalloc.h"
#include "plib/sockaddr.h"


static int
is_int(const char *p)
{
    while (isspace((unsigned char) *p))
	++p;
	   
    if (*p == '-')
	++p;

    while (isdigit((unsigned char) *p))
	++p;

    while (isspace((unsigned char) *p))
	++p;

    return (*p == '\0');
}


int
str2int(const char *buf, int *out)
{
    if (!is_int(buf))
	return -1;
    
    if (sscanf(buf, " %d ", out) != 1)
	return -1;

    return 0;
}

int
str2str(const char *buf,
	char **out)
{
    int sep;
    const char *start, *cp;

    
    while (isspace((unsigned char) *buf))
	++buf;

    switch (*buf)
    {
      case '\0':
	*out = NULL;
	return 0;

      case '\'':
      case '"':
	sep = *buf;
	start = ++buf;
	while (*buf != '\0' && *buf != sep)
	    ++buf;
	if (*buf == '\0')
	    return -1;
	*out = a_strndup(start, buf-start,"<string>");
	return 0;

      default:
	cp = buf;
	while (*cp != '\0')
	    ++cp;
	--cp;
	while (cp > buf && isspace((unsigned char) *cp))
	    --cp;
	++cp;
	*out = a_strndup(buf, cp-buf, "<string>");
	return 0;
    }
}


int
str2bool(const char *buf,
	 int *out)
{
    while (isspace((unsigned char) *buf))
	++buf;

    if (s_strcasecmp(buf, "true") == 0 ||
	s_strcasecmp(buf, "on") == 0 ||
	s_strcasecmp(buf, "enable") == 0 ||
	s_strcasecmp(buf, "enabled") == 0 ||
	s_strcasecmp(buf, "yes") == 0)
    {
	return (*out = 1);
    }
    
    if (s_strcasecmp(buf, "false") == 0 ||
	s_strcasecmp(buf, "off") == 0 ||
	s_strcasecmp(buf, "disable") == 0 ||
	s_strcasecmp(buf, "disabled") == 0 ||
	s_strcasecmp(buf, "no") == 0)
    {
	return (*out = 0);
    }

    return -1;
}


int
str2port(const char *str, int *out)
{
    struct servent *sp;


    if (is_int(str))
    {
	*out = atoi(str);
	return 0;
    }
    
    sp = getservbyname(str, "tcp");
    if (sp == NULL)
	return -1;


    *out = ntohs(sp->s_port);
    return 0;
}


/* XXX: Todo: Support hostnames and not just numbers */
int
str2addr(const char *str,
	 struct sockaddr_gen *sg)
{
    char *buf, *cp, *pp = NULL;
    

    SGINIT(*sg);
    
    buf = a_strdup(str, "<address string>");
    cp = buf;
    
#ifdef HAVE_INET_PTON
    if (*cp == '[')
    {
	/* IPv6 ala RFC 2732 */
	
	++cp;
	pp = strchr(cp, ']');
	if (pp == NULL)
	    return -1;

	*pp++ = '\0';
	if (*pp == ':')
	    ++pp;
	else
	    pp = NULL;

	SGFAM(*sg) = AF_INET6;
    }
    else
    {
	/* IPv4 */
	pp = strrchr(cp, ':');
	if (pp)
	    *pp++ = '\0';
    
	SGFAM(*sg) = AF_INET;
    }

    if (inet_pton(SGFAM(*sg), cp, SGADDRP(*sg)) != 1)
    {
	a_free(buf);
	return -1;
    }
    
#else

    /* Locate port part */
    pp = strrchr(cp, ':');
    if (pp)
	*pp++ = '\0';
    
    SGFAM(*sg) = AF_INET;
    *(unsigned long *) SGADDRP(*sg) = inet_addr(cp);
    
#endif
    
    if (pp)
	SGPORT(*sg) = htons(atoi(pp));

    a_free(buf);
    return 0;
}





int
str2uid(const char *str, uid_t *uid, gid_t *gid)
{
    struct passwd pb, *pp;
    char buf[1024];

    
    if (is_int(str))
    {
        /* FIXME: Handle overflow?  */
	*uid = atol(str);

	pp = NULL;
	if (gid)
	{
	    (void) s_getpwuid_r(*uid, &pb, buf, sizeof(buf), &pp);
	    if (pp != NULL)
		*gid = pp->pw_gid;
	}
	
	return 0;
    }

    pp = NULL;
    (void) s_getpwnam_r(str, &pb, buf, sizeof(buf), &pp);
    if (pp == NULL)
	return -1;

    *uid = pp->pw_uid;
    if (gid)
	*gid = pp->pw_gid;

    return 0;
}


int
str2gid(const char *str, gid_t *gid)
{
    struct group gb, *gp;
    char buf[1024];

    
    if (is_int(str))
    {
        /* FIXME: Handle overflow?  */
	*gid = atol(str);
	return 0;
    }

    gp = NULL;
    (void) s_getgrnam_r(str, &gb, buf, sizeof(buf), &gp);
    if (gp == NULL)
	return -1;

    *gid = gp->gr_gid;
    return 0;
}
