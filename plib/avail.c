/*
** avail.c - Data availability control functions.
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

#include "plib/config.h"

#include <stdlib.h>

#include "plib/avail.h"


int
avail_init(AVAIL *ap,
	   int val)
{
    pthread_mutex_init(&ap->mtx, NULL);
    pthread_cond_init(&ap->cv, NULL);
    ap->avail = val;
    return 0;
}



int
avail_signal(AVAIL *ap)
{
    int res = 0;

    
    pthread_mutex_lock(&ap->mtx);
    if (ap->avail > 0)
	res = --ap->avail;
    pthread_mutex_unlock(&ap->mtx);
    if (res == 0)
	pthread_cond_broadcast(&ap->cv);

    return res;
}
     


int
avail_wait(AVAIL *ap)
{
    pthread_mutex_lock(&ap->mtx);
    while (ap->avail > 0)
	pthread_cond_wait(&ap->cv, &ap->mtx);
    pthread_mutex_unlock(&ap->mtx);
    
    return 0;
}
