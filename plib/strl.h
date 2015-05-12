/*
** strl.c - strlXXX functions
**
** Copyright (c) 2000 Peter Eriksson <pen@lysator.liu.se>
**
** This program is free software; you can redistribute it and/or
** modify it as you wish - as long as you don't claim that you wrote
** it.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#ifndef PLIB_STRL_H
#define PLIB_STRL_H

#include "plib/config.h"

#ifndef HAVE_STRLCPY
size_t
strlcpy(char *dst,
	const char *src,
	size_t dstsize);
#endif

#ifndef HAVE_STRLCAT
size_t
strlcat(char *dst,
	const char *src,
	size_t dstsize);
#endif

#ifndef HAVE_STRLNCPY
size_t
strlncpy(char *dst,
	 const char *src,
	 size_t dstsize,
	 size_t maxsrclen);
#endif

#ifndef HAVE_STRLNCAT
size_t
strlncat(char *dst,
	 const char *src,
	 size_t dstsize,
	 size_t maxsrclen);
#endif
     
#endif
