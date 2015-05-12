/*
** timeout.h - Generic timeout code
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

#ifndef PLIB_TIMEOUT_H
#define PLIB_TIMEOUT_H

#include <time.h>


typedef struct plib_timeout
{
    struct plib_timeout *next;

    time_t when;

    void (*fun)(void *arg);
    void *arg;
} TIMEOUT;


/* Set up some global initialization */
extern void
timeout_init(void);


/* Create a new timer */
extern TIMEOUT *
timeout_create(int t,
	       void (*fun)(void *arg),
	       void *arg);

/* Reset the timer, if 't' equals zero then disable the timer */
extern int
timeout_reset(TIMEOUT *tp,
	      int t);

/* Disable and destroy the timer */
extern int
timeout_destroy(TIMEOUT *tp);


#endif
