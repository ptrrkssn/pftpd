/*
** ftpcmd.c
**
** Copyright (c) 1994-2000 Peter Eriksson <pen@lysator.liu.se>
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
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>

#include "pftpd.h"

#include "plib/aalloc.h"
#include "plib/safeio.h"
#include "plib/safestr.h"
#include "plib/support.h"
#include "plib/str2.h"


#define MAX_PATHNAMELEN 2048

int readwrite_flag = 0;
int max_errors = 5;
char *comments_to = "the system administrator";
char *server_banner = "Peter's Anonymous";
char *message_file = ".message";
char *welcome_file = "/etc/welcome.txt";



struct cmdtab_s
{
    char *cmd;
    int (*handler)(FTPCLIENT *fp, char *arg);
    FTPSTATE state;
};


/* Sigh. SGI's cc doesn't allow this to be static */
extern struct cmdtab_s cmdtab[];


static int
cmd_noop(FTPCLIENT *fp,
	 char *arg)
{
    return 200;
}


#if 0
static int
cmd_notimpl(FTPCLIENT *fp,
	    char *arg)
{
    return 502;
}
#endif


static int
cmd_help(FTPCLIENT *fp,
	 char *arg)
{
    int i;


    if (arg)
    {
	fd_printf(fp->fd, "214 No help available for command %s.\n", arg);
	return 0;
    }
    
    fd_puts(fp->fd, "214-The following commands are recognized.");
    
    for (i = 0; cmdtab[i].cmd; i++)
    {
	if ((i & 7) == 0)
	    fd_putc(fp->fd, '\n');
	
	fd_printf(fp->fd, "%7s ", cmdtab[i].cmd);
    }

    fd_printf(fp->fd, "\n214 Direct comments to %s.\n", comments_to);
    
    return 0;
}



static int
cmd_site(FTPCLIENT *fp,
	 char *arg)
{
    char *scmd, *sarg, *cp;
    mode_t old_umask;
    

    scmd = s_strtok_r(arg, " \t", &cp);
    sarg = s_strtok_r(NULL, "", &cp);


    if (s_strcasecmp(scmd, "help") == 0)
    {
	fd_puts(fp->fd, "214-The following SITE commands are recognized:\n");
	fd_puts(fp->fd, "   UMASK");
	fd_printf(fp->fd, "\n214 Direct comments to %s.\n", comments_to);
    }
    
    else if (s_strcasecmp(scmd, "umask") == 0)
    {
	if (sarg == NULL)
	{
	    fd_printf(fp->fd, "200 Current UMASK is %03o.\n",
		      fp->cr_umask);
	}
	else
	{
	    int val;
	    
	    if (!readwrite_flag)
		return 553;
	    
	    old_umask = fp->cr_umask;
	    if (sscanf(sarg, "%o", &val) != 1)
		return 500;
	    fp->cr_umask = (mode_t) val;
	    fd_printf(fp->fd, "200 UMASK set to %03o (was %03o).\n",
		      fp->cr_umask, old_umask);
	}
    }

    else
	return 500;

    return 0;
}



static int
cmd_user(FTPCLIENT *fp,
	 char *arg)
{
    fp->state = ftp_any;	/* not logged in or anything */

    if (arg == NULL)
	return 501;

    a_free(fp->user);
    fp->user = a_strdup(arg, "FTPCLIENT user");
	
    if (s_strcasecmp(arg, "anonymous") == 0 ||
	s_strcasecmp(arg, "ftp") == 0)
    {
	fd_puts(fp->fd,
		"331 Guest login ok; use your e-mail address as password.\n");
	
	fp->state = ftp_passreqd;	/* await PASS command */
	return 0;
    }

    return 530;
}



static void
send_message(FTPCLIENT *fp,
	     const char *path,
	     int code)
{
    char vbuf[2048], rbuf[2048];
    char *vpath = path_mk(fp, path, vbuf, sizeof(vbuf));
    char *rpath = path_v2r(vpath, rbuf, sizeof(rbuf));
    struct stat sb;
    int fd, c, s_flag;
    FDBUF *fb;
    
    
    if (rpath == NULL)
	return;
    
    if (stat(rpath, &sb) < 0)
	return;

    if (!S_ISREG(sb.st_mode))
	return;

    fd = s_open(rpath, O_RDONLY);
    if (fd < 0)
	return;

    fb = fd_create(fd, FDF_CRLF);

    s_flag = 1;
    while ((c = fd_getc(fb)) != EOF)
    {
	if (s_flag)
	{
	    fd_printf(fp->fd, "%d-", code);
	    s_flag = 0;
	}

	fd_putc(fp->fd, c);
	if (c == '\n')
	    s_flag = 1;
    }

    if (!s_flag)
	fd_putc(fp->fd, '\n');
    
    fd_destroy(fb);
    s_close(fd);
}



static int
cmd_pass(FTPCLIENT *fp,
	 char *arg)
{
    int anon;

    
    if (arg == NULL)
	return 501;
 
    anon = (s_strcasecmp(fp->user, "anonymous") == 0 ||
	    s_strcasecmp(fp->user, "ftp") == 0);

    
    /* authenticate if we're not doing an anonymous login */
    if (!anon)
    {
	fp->state = ftp_any;
	return 530;
    }

    a_free(fp->pass);
    fp->pass = a_strdup(anon ? arg : "*", "FTPCLIENT pass");
    fp->state = ftp_loggedin;

    send_message(fp, welcome_file, 230);
    send_message(fp, message_file, 230);
    return 230;
}



static int
cmd_port(FTPCLIENT *fp,
	 char *arg)
{
    int h1, h2, h3, h4, p1, p2;
    UINT8 *p;
    int code = 0;

    
    if (arg == NULL)
	return 501;

    if (sscanf(arg, " %d , %d , %d , %d , %d , %d",
	       &h1, &h2, &h3, &h4, &p1, &p2) != 6)
	return 501;

#ifdef AF_INET6
    if (SGFAM(fp->cp->rsin) == AF_INET6)
    {
	/* XXX: FIXME: Some better way. Pleeeese */
	
	char buf[256];

	s_snprintf(buf, sizeof(buf), "[::ffff:%d.%d.%d.%d]:%d\n",
		 h1, h2, h3, h4, (p1<<8)+p2);
	
	if (str2addr(buf, &fp->port) < 0)
	{
	    syslog(LOG_WARNING,
		   "%s:%d - port proxy attempt rejected (invalid arg)",
		   s_inet_ntox(&fp->cp->rsin, buf, sizeof(buf)),
		   ntohs(SGPORT(fp->cp->rsin)));
	    
	    memset(&fp->port, 0, sizeof(fp->port));
	    
	    /* XXX: Fixme */
	    fd_puts(fp->fd,
		    "504 PORT command not accepted for security reasons.\n");
	    return 0;
	}
    }
    else
#endif
    {
	SGINIT(fp->port);
	SGFAM(fp->port) = AF_INET;
	
	p = (UINT8 *) SGADDRP(fp->port);
	*p++ = h1;
	*p++ = h2;
	*p++ = h3;
	*p++ = h4;
	
	p = (UINT8 *) &SGPORT(fp->port);
	*p++ = p1;
	*p++ = p2;
    }
    
    if (SGSIZE(fp->port) != SGSIZE(fp->cp->rsin) ||
	(code = memcmp(SGADDRP(fp->port),
		       SGADDRP(fp->cp->rsin),
		       SGSIZE(fp->port)) != 0))
    {
	char buf1[256];
	

	syslog(LOG_WARNING, "%s:%d - port proxy attempt rejected (invalid addr)",
	       s_inet_ntox(&fp->cp->rsin, buf1, sizeof(buf1)),
	       ntohs(SGPORT(fp->cp->rsin)));

	syslog(LOG_WARNING, "%d, %d, %d - %s:%d\n",
	       SGSIZE(fp->port), SGSIZE(fp->cp->rsin), code,
	       s_inet_ntox(&fp->port, buf1, sizeof(buf1)),
	       ntohs(SGPORT(fp->port)));
	
	memset(&fp->port, 0, sizeof(fp->port));

	/* XXX: Fixme */
	fd_puts(fp->fd,
		"504 PORT command not accepted for security reasons.\n");
	return 0;
    }

    return 200;
}



static int
cmd_list(FTPCLIENT *fp,
	 char *arg)
{
    return ftplist_send(fp, arg, LS_LONG|LS_FTPDATA); /* LS_ALL */
}

static int
cmd_nlst(FTPCLIENT *fp,
	 char *arg)
{
    return ftplist_send(fp, arg, LS_FTPDATA);
}


static int
cmd_cwd(FTPCLIENT *fp,
	char *arg)
{
    char vpathbuf[2048];
    char rpathbuf[2048];
    char *vpath = path_mk(fp, arg, vpathbuf, sizeof(vpathbuf));
    char *rpath = path_v2r(vpath, rpathbuf, sizeof(rpathbuf));
    struct stat sb;


    if (rpath == NULL)
	return 501;

    if (debug)
    {
	fprintf(stderr, "cmd_cwd: vpath=%s, rpath=%s\n",
		vpath, rpath);
    }
    
    if (stat(rpath, &sb) < 0)
    {
	fd_printf(fp->fd, "550 %s: %s.\n", vpath, strerror(errno));
	return 0;
    }

    if (!S_ISDIR(sb.st_mode))
    {
	fd_printf(fp->fd, "550 %s: Not a directory.\n", vpath);
	return 0;
    }

    a_free(fp->cwd);
    fp->cwd = a_strdup(vpath, "FTPCLIENT cwd");

    send_message(fp, message_file, 250);
    return 250;
}



static int
cmd_cdup(FTPCLIENT *fp,
	 char *arg)
{
    if (arg)
	return 501;

    return cmd_cwd(fp, "..");
}



static int
cmd_pwd(FTPCLIENT *fp,
		   char *arg)
{
    if (arg)
	return 501;

    fd_printf(fp->fd, "257 \"%s\" is the current directory.\n", fp->cwd);
    
    return 0;
}



static int
cmd_syst(FTPCLIENT *fp,
	 char *arg)
{
    if (arg)
	return 501;
    
    fd_puts(fp->fd, "215 UNIX Type: L8\n");
    return 0;
}



static int
cmd_type(FTPCLIENT *fp,
		    char *arg)
{
    if (arg == NULL)
	return 501;
    
    if (s_strcasecmp(arg, "I") == 0)
    {
	fp->type = ftp_binary;
	fd_puts(fp->fd, "200 Type set to I.\n");
	return 0;
    }
    
    if (s_strcasecmp(arg, "A") == 0)
    {
	fp->type = ftp_ascii;
	fd_puts(fp->fd, "200 Type set to A.\n");
	return 0;
    }

    fd_printf(fp->fd, "504 Type %s not supported.\n", arg);
    return 0;
}



static int
cmd_stru(FTPCLIENT *fp,
	 char *arg)
{
    if (arg == NULL)
	return 501;
    
    if (s_strcasecmp(arg, "F") == 0)
    {
	fd_puts(fp->fd, "200 STRU F ok.\n");
	return 0;
    }

    fd_puts(fp->fd, "504 Unimplemented STRU type.\n");
    return 0;
}



static int
cmd_mode(FTPCLIENT *fp,
	 char *arg)
{
    if (arg == NULL)
	return 501;
    
    if (s_strcasecmp(arg, "S") == 0)
    {
	fd_puts(fp->fd, "200 MODE S ok.\n");
	return 0;
    }

    fd_puts(fp->fd, "504 Unimplemented MODE type.\n");
    return 0;
}


static int
cmd_stat(FTPCLIENT *fp,
	 char *arg)
{
    int anon;

    
    if (arg == NULL)
    {
	fd_printf(fp->fd, "211-%s FTP server status:\n", server_banner);
	fd_printf(fp->fd, "     Version %s at %s %s\n",
		  server_version,
		  __DATE__, __TIME__);
	anon = (s_strcasecmp(fp->user, "anonymous") == 0 ||
		s_strcasecmp(fp->user, "ftp") == 0);
	if (anon)
	    fd_puts(fp->fd, "     Logged in anonymously\n");
	else
	    fd_printf(fp->fd, "     Logged in as %s\n", fp->user);
	fd_puts(fp->fd, "     ");
	if (fp->type == ftp_binary)
	    fd_puts(fp->fd, "TYPE: Image; ");
	else
	    fd_puts(fp->fd, "TYPE: ASCII, FORM: Nonprint; ");
	fd_puts(fp->fd, "STRUcture: File; transfer MODE: Stream\n");

	/* XXX: Handle STAT command in parallell with data transfer */
	fd_puts(fp->fd, "     No data connection\n");
	return 211;
    }
    else
    {
	char vbuf[2048], rbuf[2048];
	char *vpath, *rpath;
	struct stat sb;
	
	
	if (arg == NULL)
	    arg = ".";
	
	vpath = path_mk(fp, arg, vbuf, sizeof(vbuf));
	rpath = path_v2r(vpath, rbuf, sizeof(rbuf));
	
	if (rpath == NULL)
	    return 501;
	
	if (stat(rpath, &sb) < 0)
	{
	    fd_printf(fp->fd, "550 %s: %s.\n", vpath, strerror(errno));
	    return 0;
	}
	
	if (!S_ISDIR(sb.st_mode))
	{
	    fd_printf(fp->fd, "550 %s: Not a directory.\n", vpath);
	    return 0;
	}

	fd_printf(fp->fd, "213-Status of %s:\n", vpath);
	
	if (ftplist_send(fp, rpath, LS_LONG|LS_ALL))
	{
	    fd_printf(fp->fd, "550 %s: %s.\n", vpath, strerror(errno));
	    return 0;
	}
	return 213;
    }
}

static int
ascii_size(const char *path)
{
    int fd, size, c;
    FDBUF *fp;

    
    fd = s_open(path, O_RDONLY);
    if (fd < 0)
	return -1;
    
    fp = fd_create(fd, FDF_CRLF);

    size = 0;
    while ((c = fd_getc(fp)) != EOF)
    {
	++size;
	if (c == '\n')
	    ++size;
    }

    fd_destroy(fp);
    s_close(fd);
    
    return size;
}



static int
cmd_size(FTPCLIENT *fp,
	 char *arg)
{
    char vbuf[2048], rbuf[2048];
    char *vpath, *rpath;
    struct stat sb;


    vpath = path_mk(fp, arg, vbuf, sizeof(vbuf));
    rpath = path_v2r(vpath, rbuf, sizeof(rbuf));
    
    if (rpath == NULL)
	return 501;

    if (stat(rpath, &sb) < 0)
    {
	fd_printf(fp->fd, "550 %s: %s.\n", vpath, strerror(errno));
	return 0;
    }

    if (!S_ISREG(sb.st_mode))
    {
	fd_printf(fp->fd, "550 %s: Not a file.\n", vpath);
	return 0;
    }

    if (fp->type == ftp_binary)
    {
	fd_printf(fp->fd, "213 %d\n", sb.st_size);
    }
    else
    {
	int size = ascii_size(rpath);

	if (size < 0)
	{
	    fd_printf(fp->fd, "550 %s: Unable to calculated size.\n",
		      vpath);
	    return 0;
	}
	
	fd_printf(fp->fd, "213 %d\n", size);
    }
    return 0;
}



static int
cmd_eprt(FTPCLIENT *fp,
	 char *arg)
{
    char delim[2], *tokp;
    char buf[256];
    int net_prt, net_fam, code = 0;
    
    
    if (fp->pasv == -2) /* XXX: What is a valid error code? */
	return 501;

    if (fp->pasv != -1)
	s_close(fp->pasv);

    if (arg == NULL)
	return 501;


    delim[0] = *arg++;
    delim[1] = '\0';

    arg = s_strtok_r(arg, delim, &tokp);

    if (sscanf(arg, "%u", &net_prt) != 1)
	return 501;

    arg = s_strtok_r(NULL, delim, &tokp);
    
    switch (net_prt)
    {
      case 1:
	net_fam = AF_INET;
	break;

#ifdef AF_INET6
      case 2:
	net_fam = AF_INET6;
	break;
#endif
      default:
	fd_puts(fp->fd, "522 Network protocol not supported, use (1,2)\n");
	return 0;
    }

    if (str2addr(arg, &fp->port) < 0)
    {
	syslog(LOG_WARNING,
	       "%s:%d - port proxy attempt rejected (invalid arg)",
	       s_inet_ntox(&fp->cp->rsin, buf, sizeof(buf)),
	       ntohs(SGPORT(fp->cp->rsin)));
	
	memset(&fp->port, 0, sizeof(fp->port));
	
	/* XXX: Fixme */
	fd_puts(fp->fd,
		"504 PORT command not accepted for security reasons.\n");
	return 0;
    }
    
    if (SGSIZE(fp->port) != SGSIZE(fp->cp->rsin) ||
	(code = memcmp(SGADDRP(fp->port),
		       SGADDRP(fp->cp->rsin),
		       SGSIZE(fp->port)) != 0))
    {
	char buf1[256];
	

	syslog(LOG_WARNING,
	       "%s:%d - port proxy attempt rejected (invalid addr)",
	       s_inet_ntox(&fp->cp->rsin, buf1, sizeof(buf1)),
	       ntohs(SGPORT(fp->cp->rsin)));

	syslog(LOG_WARNING, "%d, %d, %d\n",
	       SGSIZE(fp->port), SGSIZE(fp->cp->rsin), code);
	
	memset(&fp->port, 0, sizeof(fp->port));

	/* XXX: Fixme */
	fd_puts(fp->fd,
		"504 PORT command not accepted for security reasons.\n");
	return 0;
    }

    return 200;
}


static int
bind_pasv_socket(struct sockaddr_gen *sinp)
{
    int fd, slen;
    static int one = 1;
    
    
    fd = socket(SGFAM(*sinp), SOCK_STREAM, 0);
    if (fd < 0)
    {
	syslog(LOG_ERR, "bind_pasv_socket: socket: %m");
	return -1;
    }

    SGPORT(*sinp) = 0;
    slen = SGSOCKSIZE(*sinp);
    
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *) &one, sizeof(one)))
	syslog(LOG_WARNING, "setsockopt(SO_REUSEADDR) failed (ignored): %m");
    
    if (s_bind(fd, (struct sockaddr *) sinp, slen) < 0)
    {
	syslog(LOG_ERR, "bind_pasv_socket: bind: %m");
	s_close(fd);
	return -1;
    }

    if (getsockname(fd, (struct sockaddr *) sinp, &slen) < 0)
    {
	syslog(LOG_ERR, "bind_pasv_socket: getsockname: %m");
	s_close(fd);
	return -1;
    }

    if (listen(fd, 1) < 0)
    {
	syslog(LOG_ERR, "bind_pasv_socket: listen: %m");
	s_close(fd);
	return -1;
    }

    return fd;
}



static int
cmd_epsv(FTPCLIENT *fp,
	 char *arg)
{
    struct sockaddr_gen sin;
    int net_prt = 0;
    int net_fam;

    
    if (fp->pasv != -1)
	s_close(fp->pasv);

    if (arg && arg[0])
    {
	if (s_strcasecmp(arg, "all") == 0)
	{
	    fp->pasv = -2;
	    return 200;
	}
	else
	    if (sscanf(arg, "%u", &net_prt) != 1)
		return 501;
    }

    switch (net_prt)
    {
      case 0:
	net_fam = SGFAM(fp->cp->rsin);
	break;

      case 1:
	net_fam = AF_INET;
	break;
#ifdef AF_INET6
      case 2:
	net_fam = AF_INET6;
	break;
#endif
      default:
	fd_puts(fp->fd, "522 Network protocol not supported, use (1,2)\n");
	return 0;
    }

    sin = fp->cp->lsin;

    if (SGFAM(sin) != net_fam) 
    {
	fd_puts(fp->fd, "425 Can't open passive connection.\n");
	return 0;
    }
    
    fp->pasv = bind_pasv_socket(&sin);
    if (fp->pasv < 0)
    {
	fd_puts(fp->fd, "425 Can't open passive connection.\n");
	return 0;
    }
	
    fd_printf(fp->fd, "229 Entering Extended Passive Mode (|||%d|)\n",
	      ntohs(SGPORT(sin)));

    return 0;
}


static int
cmd_pasv(FTPCLIENT *fp,
	 char *arg)
{
    struct sockaddr_gen sin;
    UINT32 addr;
    UINT16 port;
    
    
    if (arg != NULL)
	return 501;

    if (fp->pasv != -1)
    {
	if (debug)
	    fprintf(stderr, "cmd_pasv(): Closing old PASV socket\n");
	s_close(fp->pasv);
    }

    
    if (SGFAM(fp->cp->lsin) == AF_INET 
#ifdef HAVE_IPV6
	|| (SGFAM(fp->cp->lsin) == AF_INET6 && IN6_IS_ADDR_V4MAPPED((struct in6_addr *) SGADDRP(fp->cp->lsin)))
#endif
	)
    {
	sin = fp->cp->lsin;
    }
    
    else
    {
	/* Invalid address family */
	fd_puts(fp->fd, "425 Can't open passive connection.\n");
	return 0;
    }
    
    fp->pasv = bind_pasv_socket(&sin);
    if (fp->pasv < 0)
    {
	if (debug)
	    fprintf(stderr, "bind_pasv_socket(): failed: %s\n", strerror(errno));
	
	fd_puts(fp->fd, "425 Can't open passive connection (bind failed).\n");
	return 0;
    }

    
    if (debug)
    {
	char buf1[256];

	
	fprintf(stderr, "ftpcmd_pasv():\n");
	fprintf(stderr, "\tsin_family = %d\n", SGFAM(sin));
	fprintf(stderr, "\tsin_addr   = %s\n",
		s_inet_ntox(&sin, buf1, sizeof(buf1)));
	fprintf(stderr, "\tsin_port   = %d\n",
		ntohs(SGPORT(sin)));
    }

    if (SGFAM(sin) == AF_INET)
    {
	addr = * (UINT32 *) SGADDRP(sin);
    }
#ifdef HAVE_IPV6
    else if (SGFAM(sin) == AF_INET6)
    {
	/* XXX: IN6_V4MAPPED_TO_IPADDR() is probably only defined on Solaris! */
	IN6_V4MAPPED_TO_IPADDR((struct in6_addr *) SGADDRP(fp->cp->lsin), addr);
    }
#endif
    else
    {
	syslog(LOG_ERR, "invalid socket family (%d): Should not happen",
	       SGFAM(sin));
	fd_puts(fp->fd,
		"425 Can't open passive connection (bind failed).\n");
	return 0;
    }

	
    port = ntohs(SGPORT(sin));
    
    fd_printf(fp->fd, "227 Entering passive mode (%u,%u,%u,%u,%u,%u)\n",
	      (addr >> 24) & 0xFF,
	      (addr >> 16) & 0xFF,
	      (addr >>  8) & 0xFF,
	      (addr      ) & 0xFF,
	      (port >>  8) & 0xFF,
	      (port      ) & 0xFF);
	      
    return 0;
}


static int
cmd_rest(FTPCLIENT *fp,
	 char *arg)
{
    int rest;

    
    if (arg == NULL)
	return 501;

    /* XXX: Make sure there isn't a DATA transfer running already */
    
    rest = atoi(arg);
    if (rest < 0)
	return 501;

    fp->data_start = rest;

    fd_printf(fp->fd,
	      "350 RESTarting at %lu, continue with transfer command\n",
	      fp->data_start);
    return 0;
}


static int
cmd_abor(FTPCLIENT *fcp,
	 char *arg)
{
    ftpdata_destroy(fcp->data);
    fcp->data = NULL;
    
    fd_puts(fcp->fd, "226 Abort successful.\n");
    return 0;
}


static int
send_ascii_file(int client_fd,
		int file_fd,
		off_t start)
{
    FDBUF *cfd;
    FDBUF *ffd;
    int c;
    

    cfd = fd_create(client_fd, FDF_CRLF);
    ffd = fd_create(file_fd, FDF_CRLF);

    c = EOF;
    while (start-- > 0 && (c = fd_getc(ffd)) != EOF)
	;
    if (c == EOF)
    {
	fd_destroy(cfd);
	fd_destroy(ffd);

	if (start == 0)
	    return 0;
	else
	    return -1;
    }

    while ((c = fd_getc(ffd)) != EOF)
    {
	if (fd_putc(cfd, c))
	    break;
    }

    fd_destroy(cfd);
    fd_destroy(ffd);
    return 0;
}




typedef struct
{
    char *vpath;
    int file_fd;
    int to_client;
} FTPDATA_XFER;


static int
xfer_thread(FTPCLIENT *fp,
	    void *vp)
{
    FTPDATA_XFER *dp = (FTPDATA_XFER *) vp;
    int err;
    time_t t1, t2;

    if (debug)
	fprintf(stderr, "xfer_thread: Start (data_start = %lu)\n",
		fp->data_start);

    time(&t1);
    
    if (dp->to_client)
    {
	/* RETR */

	if (fp->type == ftp_binary)
	    err = s_copyfd(fp->data->fd,
			   dp->file_fd,
			   &fp->data_start,
			   0);
	else
	    err = send_ascii_file(fp->data->fd,
				  dp->file_fd,
				  fp->data_start);
    }
    else
    {
	/* STOR or APPE */
	
	if (fp->type == ftp_binary)
	{
	    err = 0;
	    if (fp->data_start)
	    {
		err = lseek(dp->file_fd, fp->data_start, SEEK_SET);
		if (err)
		    goto End;
	    }

	    err = s_copyfd(dp->file_fd,
			   fp->data->fd,
			   NULL,
			   0);
	}
	else
	{
	    if (fp->data_start)
	    {
		/* XXX: Does not support RESTart with STOR/APPE on ASCII
		   yet (requires a fdbuf capable of read/write mode) */
		err = -1;
		syslog(LOG_ERR, "REST not supported with STOR/APPE on ASCII");
		goto End;
	    }
	    
	    err = send_ascii_file(dp->file_fd,
				  fp->data->fd,
				  0);
	}
    }

  End:
    if (err >= 0)
    {
	time(&t2);
	
	xferlog_append(fp,
		       (unsigned long) t2 - (unsigned long) t1,
		       err,
		       dp->vpath,
		       dp->to_client);
    }
		   
    if (debug)
	fprintf(stderr, "xfer_thread: Stop (err=%d, errno=%d)\n", err, errno);

    fp->data_start = 0;
    s_close(dp->file_fd);
    a_free(dp->vpath);
    a_free(dp);
    
    
    return err < 0 ? 426 : 226;
}


static int
cmd_retr(FTPCLIENT *fp,
	 char *arg)
{
    char vbuf[2048], rbuf[2048];
    char *vpath, *rpath;
    struct stat sb;
    FTPDATA_XFER *dbp;
	

    vpath = path_mk(fp, arg, vbuf, sizeof(vbuf));
    rpath = path_v2r(vpath, rbuf, sizeof(rbuf));
    
    if (rpath == NULL)
	return 501;

    if (fp->type != ftp_ascii && fp->type != ftp_binary)
    {
	fd_printf(fp->fd,
		  "504 RETR not implemented for type %c.\n",
		  fp->type);
	return 0;
    }

    dbp = a_malloc(sizeof(*dbp), "FTPDATA_XFER");
    dbp->vpath = a_strdup(vpath, "FTPDATA_XFER vpath");
    dbp->to_client = 1;
    
    dbp->file_fd = s_open(rpath, O_RDONLY);
    if (dbp->file_fd < 0)
    {
	fd_printf(fp->fd, "550 %s: %s.\n", vpath, strerror(errno));
	a_free(dbp->vpath);
	a_free(dbp);
	return 0;
    }

    if (fstat(dbp->file_fd, &sb) < 0)
    {
	fd_printf(fp->fd, "550 %s: %s.\n", vpath, strerror(errno));
	s_close(dbp->file_fd);
	a_free(dbp->vpath);
	a_free(dbp);
	return 0;
    }

    if (!S_ISREG(sb.st_mode))
    {
	fd_printf(fp->fd, "550 %s: Not a file.\n", vpath);
	s_close(dbp->file_fd);
	a_free(dbp->vpath);
	a_free(dbp);
	return 0;
    }

    if (ftpdata_start(fp, vpath, xfer_thread, (void *) dbp) == NULL)
    {
	fd_puts(fp->fd, "425 Can not build data connection.\n");
	s_close(dbp->file_fd);
	a_free(dbp->vpath);
	a_free(dbp);
    }
    
    return 0;
}


static int
cmd_stor(FTPCLIENT *fp,
	 char *arg)
{
    char vbuf[2048], rbuf[2048];
    char *vpath, *rpath;
    struct stat sb;
    FTPDATA_XFER *dbp;
	

    /* Only allow this command if server is started in Read/Write mode */
    if (!readwrite_flag)
	return 502;
    
    vpath = path_mk(fp, arg, vbuf, sizeof(vbuf));
    rpath = path_v2r(vpath, rbuf, sizeof(rbuf));
    
    if (rpath == NULL)
	return 501;

    if (fp->type != ftp_ascii && fp->type != ftp_binary)
    {
	fd_printf(fp->fd,
		  "504 STOR not implemented for type %c.\n",
		  fp->type);
	return 0;
    }

    dbp = a_malloc(sizeof(*dbp), "FTPDATA_XFER");
    dbp->vpath = a_strdup(vpath, "FTPDATA_XFER vpath");
    dbp->to_client = 0;
    
    dbp->file_fd = s_open(rpath, O_WRONLY|O_CREAT, 0666 & ~fp->cr_umask);
    if (dbp->file_fd < 0)
    {
	fd_printf(fp->fd, "550 %s: %s.\n", vpath, strerror(errno));
	a_free(dbp->vpath);
	a_free(dbp);
	return 0;
    }

    if (fstat(dbp->file_fd, &sb) < 0)
    {
	fd_printf(fp->fd, "550 %s: %s.\n", vpath, strerror(errno));
	s_close(dbp->file_fd);
	a_free(dbp->vpath);
	a_free(dbp);
	return 0;
    }

    if (!S_ISREG(sb.st_mode))
    {
	fd_printf(fp->fd, "550 %s: Not a file.\n", vpath);
	s_close(dbp->file_fd);
	a_free(dbp->vpath);
	a_free(dbp);
	return 0;
    }

    if (ftpdata_start(fp, vpath, xfer_thread, (void *) dbp) == NULL)
    {
	fd_puts(fp->fd, "425 Can not build data connection.\n");
	s_close(dbp->file_fd);
	a_free(dbp->vpath);
	a_free(dbp);
    }
    
    return 0;
}
    


static int
cmd_appe(FTPCLIENT *fp,
	 char *arg)
{
    char vbuf[2048], rbuf[2048];
    char *vpath, *rpath;
    struct stat sb;
    FTPDATA_XFER *dbp;
	

    /* Only allow this command if server is started in Read/Write mode */
    if (!readwrite_flag)
	return 502;
    
    vpath = path_mk(fp, arg, vbuf, sizeof(vbuf));
    rpath = path_v2r(vpath, rbuf, sizeof(rbuf));
    
    if (rpath == NULL)
	return 501;

    if (fp->type != ftp_ascii && fp->type != ftp_binary)
    {
	fd_printf(fp->fd,
		  "504 STOR not implemented for type %c.\n",
		  fp->type);
	return 0;
    }

    dbp = a_malloc(sizeof(*dbp), "FTPDATA_XFER");
    dbp->vpath = a_strdup(vpath, "FTPDATA_XFER vpath");
    dbp->to_client = 0;
    
    dbp->file_fd = s_open(rpath, O_WRONLY|O_APPEND);
    if (dbp->file_fd < 0)
    {
	fd_printf(fp->fd, "550 %s: %s.\n", vpath, strerror(errno));
	a_free(dbp);
	return 0;
    }

    if (fstat(dbp->file_fd, &sb) < 0)
    {
	fd_printf(fp->fd, "550 %s: %s.\n", vpath, strerror(errno));
	s_close(dbp->file_fd);
	a_free(dbp);
	return 0;
    }

    if (!S_ISREG(sb.st_mode))
    {
	fd_printf(fp->fd, "550 %s: Not a file.\n", vpath);
	s_close(dbp->file_fd);
	a_free(dbp);
	return 0;
    }

    if (ftpdata_start(fp, vpath, xfer_thread, (void *) dbp) == NULL)
    {
	fd_puts(fp->fd, "425 Can not build data connection.\n");
	s_close(dbp->file_fd);
	a_free(dbp);
    }
    
    return 0;
}
    


static int
cmd_dele(FTPCLIENT *fp,
	 char *arg)
{
    char vbuf[2048], rbuf[2048];
    char *vpath, *rpath;


    /* Only allow this command if server is started in Read/Write mode */
    if (!readwrite_flag)
	return 502;
    
    vpath = path_mk(fp, arg, vbuf, sizeof(vbuf));
    rpath = path_v2r(vpath, rbuf, sizeof(rbuf));
    
    if (rpath == NULL)
	return 501;

    if (unlink(rpath))
    {
	fd_printf(fp->fd, "550 %s: %s.\n", vpath, strerror(errno));
	return 0;
    }
    
    return 250;
}

static int
cmd_mdtm(FTPCLIENT *fp,
	 char *arg)
{
    char vbuf[2048], rbuf[2048];
    char *vpath, *rpath;
    struct stat sb;
    struct tm tmb;
    

    vpath = path_mk(fp, arg, vbuf, sizeof(vbuf));
    rpath = path_v2r(vpath, rbuf, sizeof(rbuf));
    
    if (rpath == NULL)
	return 501;

    if (stat(rpath, &sb))
    {
	fd_printf(fp->fd, "550 %s: %s.\n", vpath, strerror(errno));
	return 0;
    }

    if (localtime_r(&sb.st_mtime, &tmb) == NULL ||
	strftime(vbuf, sizeof(vbuf), "%Y%m%d%H%M%S", &tmb) == 0)
    {
	fd_printf(fp->fd, "550 %s: invalid mtime value (%lu).\n",
		  (unsigned long) sb.st_mtime);
	return 0;
    }
    
    fd_printf(fp->fd, "213 %s\n", vbuf);
    return 0;
}


static int
cmd_mkd(FTPCLIENT *fp,
	char *arg)
{
    char vbuf[2048], rbuf[2048];
    char *vpath, *rpath;


    /* Only allow this command if server is started in Read/Write mode */
    if (!readwrite_flag)
	return 502;
    
    vpath = path_mk(fp, arg, vbuf, sizeof(vbuf));
    rpath = path_v2r(vpath, rbuf, sizeof(rbuf));
    
    if (rpath == NULL)
	return 501;

    if (mkdir(rpath, 0777 & ~fp->cr_umask))
    {
	fd_printf(fp->fd, "550 %s: %s.\n", vpath, strerror(errno));
	return 0;
    }
    
    return 250;
}


static int
cmd_rmd(FTPCLIENT *fp,
	char *arg)
{
    char vbuf[2048], rbuf[2048];
    char *vpath, *rpath;


    /* Only allow this command if server is started in Read/Write mode */
    if (!readwrite_flag)
	return 502;
    
    vpath = path_mk(fp, arg, vbuf, sizeof(vbuf));
    rpath = path_v2r(vpath, rbuf, sizeof(rbuf));
    
    if (rpath == NULL)
	return 501;

    if (rmdir(rpath))
    {
	fd_printf(fp->fd, "550 %s: %s.\n", vpath, strerror(errno));
	return 0;
    }
    
    return 250;
}


static int
cmd_rnfr(FTPCLIENT *fp,
	 char *arg)
{
    char vbuf[2048], rbuf[2048];
    char *vpath, *rpath;
    struct stat sb;
    

    /* Only allow this command if server is started in Read/Write mode */
    if (!readwrite_flag)
	return 502;
    
    vpath = path_mk(fp, arg, vbuf, sizeof(vbuf));
    rpath = path_v2r(vpath, rbuf, sizeof(rbuf));
    
    if (rpath == NULL)
	return 501;

    if (lstat(rpath, &sb))
    {
	fd_printf(fp->fd, "550 %s: %s\n", vpath, strerror(errno));
	return 0;
    }
    
    fp->rnfr = a_strdup(rpath, "FTPCLIENT rnfr");
    
    return 250;
}

static int
cmd_xadp(FTPCLIENT *fp,
	 char *arg)
{
    FILE *dfp = fopen("/tmp/aalloc.dump", "w");
    a_dump(dfp);
    fclose(dfp);

    return 501;
}

static int
cmd_rnto(FTPCLIENT *fp,
	 char *arg)
{
    char vbuf[2048], rbuf[2048];
    char *vpath, *rpath;
    

    /* Only allow this command if server is started in Read/Write mode */
    if (!readwrite_flag)
	return 502;

    if (fp->rnfr == NULL)
	return 501;
    
    vpath = path_mk(fp, arg, vbuf, sizeof(vbuf));
    rpath = path_v2r(vpath, rbuf, sizeof(rbuf));
    
    if (rpath == NULL)
	return 501;

    if (rename(fp->rnfr, rpath))
    {
	a_free(fp->rnfr);
	fp->rnfr = NULL;
	fd_printf(fp->fd, "550 %s: %s\n", vpath, strerror(errno));
	return 0;
    }
    
    a_free(fp->rnfr);
    fp->rnfr = NULL;
    return 250;
}




struct cmdtab_s cmdtab[] =
{
    { "NOOP", cmd_noop,	ftp_any },
    { "HELP", cmd_help,	ftp_any },
    { "USER", cmd_user,	ftp_any },
    { "PASS", cmd_pass,	ftp_passreqd },
    { "PORT", cmd_port,	ftp_loggedin },
    { "LIST", cmd_list,	ftp_loggedin },
    { "NLST", cmd_nlst,	ftp_loggedin },
    { "CWD",  cmd_cwd,	ftp_loggedin },
    { "XCWD", cmd_cwd,	ftp_loggedin },
    { "CDUP", cmd_cdup,	ftp_loggedin },
    { "XCUP", cmd_cdup,	ftp_loggedin },
    { "PWD",  cmd_pwd,	ftp_loggedin },
    { "XPWD", cmd_pwd,	ftp_loggedin },
    { "SYST", cmd_syst,	ftp_loggedin },
    { "TYPE", cmd_type,	ftp_loggedin },
    { "SIZE", cmd_size,	ftp_loggedin },
    { "RETR", cmd_retr,	ftp_loggedin },

    { "STOR", cmd_stor,	ftp_loggedin },
    { "DELE", cmd_dele,	ftp_loggedin },
    { "MKD",  cmd_mkd,	ftp_loggedin },
    { "XMKD", cmd_mkd,	ftp_loggedin },
    { "RMD",  cmd_rmd,	ftp_loggedin },
    { "XRMD", cmd_rmd,	ftp_loggedin },
    { "MDTM", cmd_mdtm,	ftp_loggedin },
    
    { "RNFR", cmd_rnfr,	ftp_loggedin },
    { "RNTO", cmd_rnto,	ftp_loggedin },
    { "APPE", cmd_appe,	ftp_loggedin },

    { "SITE", cmd_site, ftp_loggedin },
    
#if 0
    { "ACCT", cmd_acct,	ftp_loggedin },
    { "SMNT", cmd_smnt,	ftp_loggedin },
    { "REIN", cmd_rein,	ftp_loggedin },
#endif
    
    { "PASV", cmd_pasv,	ftp_loggedin },
    { "STRU", cmd_stru,	ftp_loggedin },
    { "MODE", cmd_mode,	ftp_loggedin },
    { "STAT", cmd_stat,	ftp_loggedin },
    { "REST", cmd_rest,	ftp_loggedin },
    { "ABOR", cmd_abor,	ftp_loggedin },

    { "EPRT", cmd_eprt, ftp_loggedin }, /* RFC2428 */
    { "EPSV", cmd_epsv, ftp_loggedin }, /* RFC2428 */
    
#if 0
    { "MLFL", cmd_mlfl,	ftp_loggedin },
    { "MAIL", cmd_mail,	ftp_loggedin },
    { "MSND", cmd_msnd,	ftp_loggedin },
    { "MSOM", cmd_msom,	ftp_loggedin },
    { "MSAM", cmd_msam,	ftp_loggedin },
    { "MRSQ", cmd_mrsq,	ftp_loggedin },
    { "MRCP", cmd_mrcp,	ftp_loggedin },
    { "ALLO", cmd_allo,	ftp_loggedin },
    { "STOU", cmd_stou,	ftp_loggedin },
#endif

    { "XADP", cmd_xadp, ftp_any },
    
    { NULL, NULL, ftp_any }
};


int
ftpcmd_parse(FTPCLIENT *fp,
	     char *buf)
{
    FDBUF *fd = fp->fd;
    int i, err;
    FTPSTATE cmd_state;
    char *cmd, *arg, *strp;
	
    
    
    cmd = s_strtok_r(buf, " \t\n\r", &strp);
    if (cmd == NULL)
	goto Fail;
    arg = s_strtok_r(NULL, "\n\r", &strp);
	
    if (s_strcasecmp(cmd, "QUIT") == 0)
    {
	fd_puts(fd, "221 Goodbye.\n");
	fd_flush(fd);
	return -1;
    }

    for (i = 0; cmdtab[i].cmd && s_strcasecmp(cmd, cmdtab[i].cmd); i++)
	;

    /*
     * Ensure that our present state is compatible with the command's
     * requirements
     */
    cmd_state = cmdtab[i].state;
    if ((cmd_state != ftp_any) && (cmd_state != fp->state))
    {
	fp->errors++;
	if (fp->errors > max_errors)
	{
	    fd_puts(fd, "500 Too many errors\n");
	    return 500;
	}
	
	if (fp->state == ftp_passreqd)
	    fd_puts(fd, "530 Not logged in - use the PASS command.\n");
	else if (cmd_state == ftp_loggedin)
	    fd_puts(fd, "530 Not logged in.\n");
	else if (cmd_state == ftp_passreqd)
	    /* PASS needs USER as the preceding command */
	    fd_puts(fd, "503 Please use the USER command first.\n");
	else
	    fd_puts(fd, "503 Bad sequence of commands.\n");

	return 0;
    }
    
    if (cmdtab[i].handler)
    {
	err = cmdtab[i].handler(fp, arg);
	    
	switch (err)
	{
	  case 0:
	    break;
	    
	  case 200:
	    fd_printf(fd, "200 %s command successful.\n", cmd);
	    break;

	  case 211:
	    fd_puts(fd, "211 End of status.\n");
	    break;
	    
	  case 213:
	    fd_puts(fd, "213 End of status.\n");
	    break;
	    
	  case 226:
	    fd_puts(fp->fd, "226 Transfer complete.\n");
	    break;
	    
	  case 230:
	    fd_puts(fd, "230 Login OK.\n");
	    break;

	  case 250:
	    fd_puts(fd, "250 Command successful.\n");
	    break;
	    
	  case 501:
	    fd_puts(fd, "501 Syntax error in parameters.\n");
	    break;
	    
	  case 502:
	    fd_puts(fd, "502 Command not implemented.\n");
	    break;
	    
	  case 530:
	    fd_puts(fd, "530 Login incorrect.\n");
	    break;

	  default:
	    fd_puts(fd, "500 Command not understood\n");
	}
	
	if (err >= 500)
	{
	    fp->errors++;
	    if (fp->errors > max_errors)
	    {
		fd_puts(fd, "500 Too many errors\n");
		return -1;
	    }
	}
	
	return 0;	
    }
    
  Fail:
    fp->errors++;
    if (fp->errors > max_errors)
    {
	fd_puts(fd, "500 Too many errors\n");
	return -1;
    }

    fd_puts(fd, "500 Command not understood\n");
    return 0;
}


FTPCLIENT *
ftpcmd_create(CLIENT *cp)
{
    FTPCLIENT *fp;
    int val;


    fp = a_malloc(sizeof(*fp), "FTPCLIENT");
    memset(fp, 0, sizeof(*fp));

    fp->cp = cp;
    
    fp->fd = fd_create(cp->fd, FDF_CRLF|FDF_LINEBUF|FDF_TELNET);

    val = 1;
    if (setsockopt(cp->fd, SOL_SOCKET,
		   SO_OOBINLINE, (void *) &val, sizeof(val)))
    {
	syslog(LOG_WARNING, "setsockopt(SO_OOBINLINE): %m");
    }
    
#ifdef IPTOS_LOWDELAY
    val = IPTOS_LOWDELAY;
    if (setsockopt(cp->fd, IPPROTO_IP,
		   IP_TOS, (void *) &val, sizeof(val)))
    {
	syslog(LOG_WARNING, "setsockopt(IPTOS_LOWDELAY): %m");
    }
#endif
    
    fp->state = ftp_any;
    fp->user = NULL;
    fp->pass = NULL;

    fp->pasv = -1;

    fp->data = NULL;
    fp->data_start = 0;
    
    fp->cwd = a_strdup("/", "FTPCLIENT cwd");

    fp->cr_umask = 022;
    
    fp->errors = 0;
    fp->type = ftp_binary;
    
    if (debug)
	fprintf(stderr, "ftpcmd_create() -> %p\n", fp);
    
    return fp;
}


void
ftpcmd_destroy(FTPCLIENT *fcp)
{
    if (debug)
	fprintf(stderr, "ftpcmd_destroy(%p)\n", fcp);
    
    if (fcp->data)
	ftpdata_destroy(fcp->data);

    if (fcp->pasv != -1)
	s_close(fcp->pasv);
    
    a_free(fcp->cwd);
    a_free(fcp->pass);
    a_free(fcp->user);
    
    fd_destroy(fcp->fd);
    
    a_free(fcp);
}



void
ftpcmd_send_banner(FTPCLIENT *fp)
{
    fd_printf(fp->fd,
	      "220 %s FTP server (pftpd %s at %s %s) ready.\n",
	      server_banner,
	      server_version,
	      __DATE__, __TIME__);
}
    
