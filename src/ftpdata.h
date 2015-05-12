/*
** ftpdata.h
**
** Copyright (c) 1999-2002 Peter Eriksson <pen@lysator.liu.se>
**
** This program is free software; you can redistribute it and/or
** modify it as you wish - as long as you don't claim that you wrote
** it.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#ifndef FTPDATA_H
#define FTPDATA_H

#include "plib/threads.h"

struct FTPCLIENT;

typedef struct FTPDATA
{
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
    int fd;
    int state;
    pthread_t tid;
    int (*handler)(struct FTPCLIENT *fcp, void *vp);
    void *vp;
} FTPDATA;


typedef struct FTPDATA_RES
{
    int code;
} FTPDATA_RES;


/* Initialize global variables */
extern void
ftpdata_init(void);


/* Initiate a FTPDATA session */
extern FTPDATA *
ftpdata_start(struct FTPCLIENT *fp,
	      const char *reason,
	      int (*handler)(struct FTPCLIENT *fp, void *vp),
	      void *vp);

/* Get state of FTPDATA session */
extern int
ftpdata_state(FTPDATA *fdp);

/* Wait for a started FTPDATA session to finish */
extern void
ftpdata_wait(FTPDATA *fp);

/* Abort a started FTPDATA session */
extern void
ftpdata_abort(FTPDATA *fp);

/* Dispose of FTPDATA structure */
extern void
ftpdata_destroy(FTPDATA *fp);

#endif
