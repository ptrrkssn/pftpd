/*
** timeout.c - Generic timeout code
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

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>

#include "plib/safeio.h"
#include "plib/safestr.h"
#include "plib/threads.h"
#include "plib/aalloc.h"

#include "plib/timeout.h"

#ifdef HAVE_THREADS


int debug;


static struct timeout_cb
{
    pthread_mutex_t mtx;
    pthread_cond_t cv;
    pthread_t tid;
    pthread_attr_t ca_detached;
    int state; /* 0 = not running, 1 = thread running */
    TIMEOUT *top;
} tcb;


static void *
timeout_thread(void *misc)
{
    struct timeout_cb *tcb = (struct timeout_cb *) misc;
    TIMEOUT *tp;
    time_t now;


    if (debug)
	fprintf(stderr, "timeout_thread(): Started\n");
    
    pthread_mutex_lock(&tcb->mtx);
    
    while (tp = tcb->top)
    {
	time(&now);

	while (tp && now >= tp->when)
	{
	    tcb->top = tp->next;
	    pthread_mutex_unlock(&tcb->mtx);
	    
	    if (tp->fun)
		tp->fun(tp->arg);
	    
	    pthread_mutex_lock(&tcb->mtx);
	    tp = tcb->top;
	}
	
	if (tp == NULL)
	    pthread_cond_wait(&tcb->cv, &tcb->mtx);
	else
	{
	    struct timespec when;

	    when.tv_sec = tp->when;
	    when.tv_nsec = 0;
	    
	    pthread_cond_timedwait(&tcb->cv, &tcb->mtx, &when);
	}
    }

    tcb->state = 0;
    pthread_mutex_unlock(&tcb->mtx);
    
    if (debug)
	fprintf(stderr, "timeout_thread(): Terminated\n");
    
    return NULL;
}


void
timeout_init(void)
{
    int ecode;

    if (debug)
	fprintf(stderr, "timeout_init(): Start\n");
    
    pthread_mutex_init(&tcb.mtx, NULL);
    pthread_cond_init(&tcb.cv, NULL);
    pthread_attr_init(&tcb.ca_detached);
    pthread_attr_setdetachstate(&tcb.ca_detached, PTHREAD_CREATE_DETACHED);
    tcb.state = 0;
    tcb.top = NULL;
    
    if (debug)
	fprintf(stderr, "timeout_init(): End\n");
}


TIMEOUT *
timeout_create(int timeout,
	       void (*fun)(void *arg),
	       void *arg)
{
    TIMEOUT *tp;
    

    if (debug)
	fprintf(stderr, "timeout_create(%d, ...): Start\n", timeout);
    
    A_NEW(tp);

    tp->next = NULL;
    tp->when = 0;

    tp->fun = fun;
    tp->arg = arg;

    if (timeout_reset(tp, timeout) != 0)
    {
	timeout_destroy(tp);
	if (debug)
	    fprintf(stderr, "timeout_create(%d, ...): Failed\n", timeout);
	
	return NULL;
    }
    
    if (debug)
	fprintf(stderr, "timeout_create(%d, ...): Done -> %p\n", timeout, (void *) tp);
    
    return tp;
}


int
timeout_reset(TIMEOUT *tp,
	      int timeout)
{
    TIMEOUT **prev, *cur;
    TIMEOUT *new_top, *old_top;
    int code;
    
    
    if (debug)
	fprintf(stderr, "timeout_reset(%p, %d)\n", (void *) tp, timeout);

    pthread_mutex_lock(&tcb.mtx);

    old_top = tcb.top;
    
    if (timeout > 0)
	tp->when = time(NULL)+timeout;
    else
	tp->when = 0;

    /* Locate it in the timeout list, if previously inserted */
    prev = &tcb.top;
    cur = tcb.top;

    while (cur != NULL && cur != tp)
    {
	prev = &cur->next;
	cur = cur->next;
    }

    /* Remove it from the list, if found */
    if (cur == tp)
    {
	*prev = cur->next;
	tp->next = NULL;
    }

    if (timeout > 0)
    {
	/* Reinsert it at the new position */
	prev = &tcb.top;
	cur = tcb.top;
	while (cur != NULL && cur->when < tp->when)
	{
	    prev = &cur->next;
	    cur = cur->next;
	}
	*prev = tp;
	tp->next = cur;

    }

    if (tcb.top && tcb.state == 0)
    {
	if (debug)
	    fprintf(stderr, "timeout_reset: Starting timeout_thread...\n");
	
	tcb.state = 1;
	code = pthread_create(&tcb.tid, &tcb.ca_detached, timeout_thread, (void *) &tcb);
	
	if (code)
	{
	    tcb.state = 0;
	    
	    syslog(LOG_ERR, "timeout_reset: pthread_create(timeout_thread): %s",
		   strerror(code));
	    return -1; /* XXX: abort? */
	}
    }
    
    pthread_mutex_unlock(&tcb.mtx);
    pthread_cond_signal(&tcb.cv);

    if (debug)
	fprintf(stderr, "timeout_reset(%p): Done\n",
		(void *) tp);
    
    return 0;
}


#else /* No threads */

#include <signal.h>

static TIMEOUT *top = NULL;


static RETSIGTYPE
sigalarm_handler(int sig)
{
    time_t now;
    int timeout;
    

    
    s_signal(SIGALRM, sigalarm_handler);

    time(&now);
    
    while (top != NULL && top->when <= now)
    {
	top->fun(top->arg);
	top = top->next;
    }
    
    if (top)
    {
	timeout = top->when-now;
	alarm(timeout);
    }
}


TIMEOUT *
timeout_create(int timeout,
	      void (*fun)(void *arg),
	      void *arg)
{
    TIMEOUT *tp;
    

    A_NEW(tp);
    
    tp->next = NULL;
    tp->when = 0;
    tp->fun = fun;
    tp->arg = arg;

    if (timeout_reset(tp, timeout) != 0)
    {
	timeout_destroy(tp);
	return NULL;
    }
    
    return tp;
}


int
timeout_reset(TIMEOUT *tp,
	      int timeout)
{
    TIMEOUT **prev, *cur;
    time_t now;
    

    time(&now);

    if (timeout > 0)
	tp->when = now+timeout;
    else
	tp->when = 0;

    alarm(0);
    
    /* Locate it in the timeout list */
    prev = &top;
    cur = top;
    while (cur != NULL && cur != tp)
    {
	prev = &cur->next;
	cur = cur->next;
    }

    /* Remove it from the list, if found */
    if (cur == tp)
    {
	*prev = cur->next;
	tp->next = NULL;
    }

    if (timeout > 0)
    {
	/* Reinsert it at the new position */
	prev = &top;
	cur = top;
	while (cur != NULL && cur->when < tp->when)
	{
	    prev = &cur->next;
	    cur = cur->next;
	}
	*prev = tp;
	tp->next = cur;
    }

    if (top)
    {
	time(&now);
	
	timeout = top->when-now;

	if (timeout <= 0)
	    sigalarm_handler(SIGALRM);
	else
	    alarm(timeout);
    }
    
    return 0;
}

void
timeout_init(void)
{
    /* Nothing really */
}

#endif


int
timeout_destroy(TIMEOUT *tp)
{
    if (tp == NULL)
	return -1;

    if (debug)
	fprintf(stderr, "timeout_destroy(%p): Start\n",
		(void *) tp);
    
    timeout_reset(tp, 0);
    a_free(tp);
    
    if (debug)
	fprintf(stderr, "timeout_destroy(%p): Done\n",
		(void *) tp);
    
    return 0;
}


