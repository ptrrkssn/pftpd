/*
** conf.c - Parse the config file
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

#include "config.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <syslog.h>

#include "pftpd.h"

#include "plib/safestr.h"
#include "plib/str2.h"
#include "plib/support.h"



int
conf_parse(const char *path,
	   int silent)
{
    FILE *fp;
    char buf[1024], *cp, *sp, *arg;
    int line;
    

    fp = fopen(path, "r");
    if (fp == NULL)
    {
	if (!silent)
	    syslog(LOG_ERR, "fopen(\"%s\"): %m", path);
	
	return -1;
    }

    line = 0;
    while (fgets(buf, sizeof(buf), fp))
    {
	++line;
	
	cp = strchr(buf, '#');
	if (cp != NULL)
	    *cp = '\0';

	cp = s_strtok_r(buf, " \t\r\n", &sp);
	if (cp == NULL)
	    continue;

	arg = s_strtok_r(NULL, " \t\r\n", &sp);
	if (arg == NULL || strcmp(arg, "=") != 0)
	{
	    syslog(LOG_ERR, "%s: %d: missing '='", path, line);
	    continue;
	}

	arg = s_strtok_r(NULL, "\r\n", &sp);
	if (arg == NULL)
	{
	    syslog(LOG_ERR, "%s: %d: missing argument", path, line);
	    continue;
	}


	if (debug)
	    fprintf(stderr, "conf_parse(\"%s\"), line #%d: %s = %s\n",
		    path, line, cp, arg ? arg : "");


	/* Global directives */
	
	if (s_strcasecmp(cp, "include") == 0)
	{
	    conf_parse(arg, silent);
	}

	
	
	/* Global variables */
	
	else if (s_strcasecmp(cp, "syslog:facility") == 0)
	{
	    int code = syslog_str2fac(arg);
	    
	    if (code < 0)
		syslog(LOG_ERR, "%s: %d: invalid syslog facility: %s",
		       path, line, arg);
	    else
	    {
		closelog();
		s_openlog(argv0, LOG_PID|LOG_ODELAY, code);
	    }
	}

	
	/* Server variables */
	
	else if (s_strcasecmp(cp, "server:port") == 0)
	{
	    if (str2port(arg, &listen_port) < 0)
		syslog(LOG_ERR, "%s: %d: invalid port: %s",
		       path, line, arg);
	}

	else if (s_strcasecmp(cp, "server:backlog") == 0)
	{
	    if (str2int(arg, &listen_backlog) < 0)
		syslog(LOG_ERR, "%s: %d: invalid number: %s",
		       path, line, arg);
	}

#if 0 /* Enable when we have a str2addr() */
	else if (s_strcasecmp(cp, "server:address") == 0)
	{
	    if (str2addr(arg, &listen_address) < 0)
		syslog(LOG_ERR, "%s: %d: invalid address: %s",
		       path, line, arg);
	}
#endif
	
	else if (s_strcasecmp(cp, "server:user") == 0)
	{
	    if (str2uid(arg, &server_uid, &server_gid) < 0)
		syslog(LOG_ERR, "%s: %d: invalid user: %s",
		       path, line, arg);
	}

	
	else if (s_strcasecmp(cp, "server:group") == 0)
	{
	    if (str2gid(arg, &server_gid) < 0)
		syslog(LOG_ERR, "%s: %d: invalid group: %s",
		       path, line, arg);
	}

	
	else if (s_strcasecmp(cp, "server:pid-file") == 0)
	{
	     if (str2str(arg, &pidfile_path) < 0)
		 syslog(LOG_ERR, "%s: %d: invalid string: %s",
			path, line, arg);
	}

	else if (s_strcasecmp(cp, "server:max-requests") == 0)
	{
	     if (str2int(arg, &requests_max) < 0)
		 syslog(LOG_ERR, "%s: %d: invalid integer: %s",
			path, line, arg);
	}


	/* Protocol variables */
	
	else if (s_strcasecmp(cp, "protocol:timeout") == 0)
	{
	    if (str2int(arg, &request_timeout) < 0)
		syslog(LOG_ERR, "%s: %d: invalid integer: %s",
		       path, line, arg);
	}


	/* Result variables */
	
	else
	    syslog(LOG_ERR, "%s: %d: unknown option: %s", path, line, cp);
    }

    fclose(fp);
    return 0;
}
