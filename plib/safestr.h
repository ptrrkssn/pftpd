/*
** safestr.h - Buffer overrun safe string functions
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

#ifndef PLIB_SAFESTR_H
#define PLIB_SAFESTR_H

#include <stdarg.h>
#include <sys/types.h>

#ifdef DEBUG
#ifndef PLIB_IN_SAFESTR_C
#define strcpy #error
#define strcat #error
#define strtok #error
#define strtok_r #error
#define strcasecmp #error
#define sprintf #error
#define snprintf #error
#define vsprintf #error
#define vsnprintf #error
#endif
#endif

extern char *
s_strtok_r(char *b,
	   const char *s,
	   char **bp);

extern int
s_strcasecmp(const char *s1,
	     const char *s2);

extern int
s_vsnprintf(char *buf,
	    size_t bufsize,
	    const char *format,
	    va_list ap);

extern int
s_snprintf(char *buf,
	   size_t bufsize,
	   const char *format, ...);

#endif
