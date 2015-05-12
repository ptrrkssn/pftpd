/*
** no_thr.h - "Pthreads" for systems without working threads.
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

#ifndef PLIB_NO_THR_H
#define PLIB_NO_THR_H

#include <errno.h>

#ifndef HAVE_PTHREAD_MUTEX_T
typedef int pthread_mutex_t;
typedef int pthread_cond_t;
typedef int pthread_attr_t;
typedef int pthread_t;
typedef struct { int f; } pthread_once_t;
#endif

#define pthread_mutex_init(mp,ap)	(0)
#define pthread_mutex_lock(mp)		(0)
#define pthread_mutex_unlock(mp)	(0)
#define pthread_mutex_destroy(mp)	(0)

#define pthread_cond_init(cp,ap)	(0)
#define pthread_cond_wait(cp,mp)	(0)
#define pthread_cond_signal(cp)		(0)
#define pthread_cond_broadcast(cp)	(0)
#define pthread_cond_destroy(cp)	(0)

#define pthread_attr_init(ap)		(0)

#define PTHREAD_CREATE_DETACHED		1

#define PTHREAD_ONCE_INIT		{0}
#define pthread_once(ov,fun) \
	(*((int *)ov) && ((*((int *)ov) = 1), (fun()), 1))

#define pthread_attr_setdetachstate(ap,state)	(0)


#define pthread_create(tidp,attrp,func,arg)	((*(func))(arg), 0)


#endif
