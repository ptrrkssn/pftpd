/*
** threads.h - Pthread emulation header file
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

#ifndef PLIB_THREADS_H
#define PLIB_THREADS_H

#include "plib/config.h"

#if HAVE_PTHREAD_H
#  include <pthread.h>

/* IBM AIX almost-pthreads */
#  ifndef PTHREAD_CREATE_JOINABLE
#    define PTHREAD_CREATE_JOINABLE PTHREAD_CREATE_UNDETACHED
#  endif

#  if HAVE_LIBCMA
#     include "plib/cma_thr.h"
#  endif

#elif HAVE_CMA
#  include "plib/hp_pthread.h"
#  include "plib/cma_thr.h"

#elif HAVE_LIBTHREAD
#  include "plib/ui_thr.h"

#else
#  include "plib/no_thr.h"
#endif

#endif
