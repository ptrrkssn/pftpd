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

#include "config.h"

#include <string.h>
#include <stdlib.h>

#include "plib/strl.h"


#ifndef HAVE_STRLCPY
size_t
strlcpy(char *dst,
	const char *src,
	size_t dstsize)
{
    size_t srclen, tocopy;


    srclen = strlen(src);

    if (dstsize == 0)
	return srclen;
    
    tocopy = (srclen >= dstsize ? dstsize-1 : srclen);
    if (tocopy > 0)
	memcpy(dst, src, tocopy);
    dst[tocopy] = '\0';
    
    return srclen;
}
#endif


#ifndef HAVE_STRLCAT
size_t
strlcat(char *dst,
	const char *src,
	size_t dstsize)
{
    size_t dstlen, srclen, tocopy;

    
    dstlen = strlen(dst);
    srclen = strlen(src);

    if (dstsize == 0)
	return dstlen+srclen;
    
    tocopy = (dstlen+srclen >= dstsize ? dstsize-1-dstlen : srclen);
    if (tocopy > 0)
	memcpy(dst+dstlen, src, tocopy);
    dst[dstlen+tocopy] = '\0';
    
    return dstlen+srclen;
}
#endif


#ifndef HAVE_STRLNCPY
/*
** Extract at most "maxsrclen" characters from the "src" string
** and copy to the "dst" string.
*/
size_t
strlncpy(char *dst,
	 const char *src,
	 size_t dstsize,
	 size_t maxsrclen)
{
    size_t srclen, tocopy;


    srclen = strlen(src);
    if (srclen > maxsrclen)
	srclen = maxsrclen;
    
    if (dstsize == 0)
	return srclen;
    
    tocopy = (srclen >= dstsize ? dstsize-1 : srclen);
    if (tocopy > 0)
	memcpy(dst, src, tocopy);
    dst[tocopy] = '\0';
    
    return srclen;
}
#endif


#ifndef HAVE_STRLNCAT
/*
** Extract at most "maxsrclen" characters from the "src" string
** and append to the "dst" string. 
*/
size_t
strlncat(char *dst,
	 const char *src,
	 size_t dstsize,
	 size_t maxsrclen)
{
    size_t dstlen, srclen, tocopy;

    
    dstlen = strlen(dst);
    srclen = strlen(src);
    if (srclen > maxsrclen)
	srclen = maxsrclen;
    
    if (dstsize == 0)
	return dstlen+srclen;

    tocopy = (dstlen+srclen >= dstsize ? dstsize-1-dstlen : srclen);
    if (tocopy > 0)
	memcpy(dst+dstlen, src, tocopy);
    dst[dstlen+tocopy] = '\0';
    
    return dstlen+srclen;
}
#endif

