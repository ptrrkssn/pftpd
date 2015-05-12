/*
** dirlist.h - Get a directory listing
**
** Copyright (c) 1999-2000 Peter Eriksson <pen@signum.se>
**
** This program is free software; you can redistribute it and/or
** modify it as you wish - as long as you don't claim that you wrote
** it.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#ifndef PLIB_DIRLIST_H
#define PLIB_DIRLIST_H

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

typedef struct
{
    int dec;
    int des;
    struct dirlist_ent
    {
	struct dirent *d;
	struct stat s;
    } *dev;
} DIRLIST;

extern DIRLIST *
dirlist_get(const char *path);

extern void
dirlist_free(DIRLIST *dlp);

#endif
