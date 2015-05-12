/*
** pqueue.c - FIFO queue management routines.
**
** Copyright (c) 1997-2002 Peter Eriksson <pen@lysator.liu.se>
**
** This program is free software; you can redistribute it and/or
** modify it as you wish - as long as you don't claim that you wrote
** it.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "pqueue.h"

extern int debug;


int
pqueue_init(PQUEUE *qp,
	   int qsize)
{
    if (debug)
	fprintf(stderr, "pqueue_init(..., %d): Start\n", qsize);
    
    qp->buf = calloc(sizeof(void *), qsize);
    if (qp->buf == NULL)
    {
	if (debug)
	    fprintf(stderr, "pqueue_init(..., %d): End (calloc failed)\n", qsize);
	
	return -1;
    }

    qp->qsize = qsize;
    qp->occupied = 0;
    qp->nextin = 0;
    qp->nextout = 0;
    qp->closed = 0;

    pthread_mutex_init(&qp->mtx, NULL);
    pthread_cond_init(&qp->more, NULL);
    pthread_cond_init(&qp->less, NULL);

    if (debug)
	fprintf(stderr, "pqueue_init(..., %d): End (OK)\n", qsize);
    
    return 0;
}



void
pqueue_close(PQUEUE *qp)
{
    pthread_mutex_lock(&qp->mtx);

    qp->closed = 1;

    pthread_mutex_unlock(&qp->mtx);
    pthread_cond_broadcast(&qp->more);
}


int
pqueue_put(PQUEUE *qp,
	  void *item)
{
    if (debug > 2)
	fprintf(stderr, "pqueue_put(%p, %p): Start\n", (void *) qp, item);
    
    pthread_mutex_lock(&qp->mtx);

    if (qp->closed)
    {
	if (debug > 2)
	    fprintf(stderr, "pqueue_put(): End (ret=0)\n");
	
	return 0;
    }
    
    while (qp->occupied >= qp->qsize)
	pthread_cond_wait(&qp->less, &qp->mtx);

    qp->buf[qp->nextin++] = item;

    qp->nextin %= qp->qsize;
    qp->occupied++;

    pthread_mutex_unlock(&qp->mtx);
    pthread_cond_signal(&qp->more);

    if (debug > 2)
	fprintf(stderr, "pqueue_put(): End (ret=1)\n");
    
    return 1;
}



int
pqueue_get(PQUEUE *qp,
	   void **item)
{
    int got = 0;


    if (debug > 1)
	fprintf(stderr, "pqueue_get(%p): Start\n", (void *) qp);
    
    pthread_mutex_lock(&qp->mtx);
    
    while (qp->occupied <= 0 && !qp->closed)
	pthread_cond_wait(&qp->more, &qp->mtx);

    if (debug > 1)
	fprintf(stderr, "pqueue_get(%p, ...): after cond_wait\n", (void *) qp);
    
    if (qp->occupied > 0)
    {
	*item = qp->buf[qp->nextout++];
	qp->nextout %= qp->qsize;
	qp->occupied--;
	got = 1;

	pthread_mutex_unlock(&qp->mtx);
	pthread_cond_signal(&qp->less);
    }
    else
	pthread_mutex_unlock(&qp->mtx);

    if (debug > 2)
	fprintf(stderr, "pqueue_get(...): End (got=%d)\n", got);
    
    return got;
}



void
pqueue_destroy(PQUEUE *qp)
{
    pthread_mutex_destroy(&qp->mtx);
    pthread_cond_destroy(&qp->more);
    pthread_cond_destroy(&qp->less);
    free(qp->buf);
}
