/*
** safestr.c - Buffer overrun safe string functions
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

#define PLIB_IN_SAFESTR_C

#include "plib/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <syslog.h>

#include "plib/threads.h"
#include "plib/safeio.h"
#include "plib/safestr.h"





int
s_vsnprintf(char *buf,
	    size_t bufsize,
	    const char *format,
	    va_list ap)
{
    int retcode;


    if (bufsize < 1)
    {
	syslog(LOG_ERR, "s_snprintf(..., %d, ...): illegal bufsize",
	       bufsize);
	s_abort();
    }
    
    buf[bufsize-1] = '\0';
#ifdef HAVE_VSNPRINTF
    retcode = vsnprintf(buf, bufsize, format, ap);
#else
#ifdef SPRINTF_RETVAL_POINTER
    /* XXX: The reason we check for sprintf()'s return type and not
       vsprintf() (which we should) is that SunOS 4 doesn't declare
       vsprintf() in any header files, but it does have the same return
       value as sprintf(). So expect a compiler warning here unless
       we use the (uncesserary) '(char *)' cast - *Sigh* */
    {
	char *cp = (char *) vsprintf(buf, format, ap);
	if (cp == NULL)
	    retcode = -1;
	else
	    retcode = strlen(cp);
    }
#else
    retcode = vsprintf(buf, format, ap);
#endif
#endif
    if (retcode > 0 && (buf[bufsize-1] != '\0' ||
			retcode > bufsize-1))
    {
	syslog(LOG_ERR, "s_snprintf(..., %d, ...) = %d: buffer overrun\n",
	       bufsize, retcode);
	
	s_abort();
    }

    return retcode;
}



int
s_snprintf(char *buf,
	    size_t bufsize,
	    const char *format,
	    ...)
{
    va_list ap;
    int retcode;
    

    va_start(ap, format);
    retcode = s_vsnprintf(buf, bufsize, format, ap);
    va_end(ap);

    return retcode;
}


/*
** A slightly safer strtok_r() function
*/
char *
s_strtok_r(char *s, const char *d, char **bp)
{
    char *cp;

    
    if (d == NULL || bp == NULL)
	return NULL;
    
    if (s == NULL)
	s = *bp;

    if (s == NULL)
	return NULL;
    
    s += strspn(s, d);
    if (*s == '\0')
	return NULL;

    cp = s;
    s = strpbrk(cp, d);
    if (s == NULL)
	*bp = strchr(cp, 0);
    else
    {
	*s++ = '\0';
	*bp = s;
    }
    
    return cp;
}


int
s_strcasecmp(const char *s1, const char *s2)
{
    int i;

    while ((i = (toupper((unsigned char) *s1) - toupper((unsigned char) *s2))) == 0 && *s1)
    {
	++s1;
	++s2;
    }

    return i;
}

