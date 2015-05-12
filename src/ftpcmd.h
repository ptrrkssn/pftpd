/*
** ftpcmd.h - Types, definitions and declarations
**
** Copyright (c) 1999 Peter Eriksson <pen@lysator.liu.se>
**
** This program is free software; you can redistribute it and/or
** modify it as you wish - as long as you don't claim that you wrote
** it.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#ifndef FTPCMD_H
#define FTPCMD_H

#include "plib/server.h"
#include "plib/fdbuf.h"

#include "ftpdata.h"

typedef enum
{
    ftp_any,
    ftp_passreqd,
    ftp_loggedin
} FTPSTATE;

typedef enum
{
    ftp_ascii = 'A',
    ftp_binary = 'I'
} FTPTYPE;

struct FTPDATA;

typedef struct FTPCLIENT
{
    CLIENT *cp;
    FDBUF *fd;
    FTPSTATE state;

    char *user;
    char *pass;
    
    int pasv;
    struct sockaddr_gen port;

    FTPDATA *data;
    
    off_t data_start;

    mode_t cr_umask;
    mode_t cr_mode;
    char *cwd;
    char *rnfr;
    
    int errors;
    FTPTYPE type;
} FTPCLIENT;


extern int readwrite_flag;
extern int max_errors;
extern char *comments_to;
extern char *server_banner;


extern FTPCLIENT *
ftpcmd_create(CLIENT *cp);


extern int
ftpcmd_parse(FTPCLIENT *fp,
	     char *buf);

extern void
ftpcmd_destroy(FTPCLIENT *fp);


extern void
ftpcmd_send_banner(FTPCLIENT *fp);


#endif
