/*
** sockaddr.h - Generic sockaddr_gen to handle multiple protocols
**
** Copyright (c) 1999 Casper Dik <Casper.Dik@Holland.Sun.COM>
** Copyright (c) 2000 Peter Eriksson <pen@lysator.liu.se>
**
** This program is free software; you can redistribute it and/or
** modify it as you wish - as long as you don't claim that you wrote
** it.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#ifndef PLIB_SOCKADDR_H
#define PLIB_SOCKADDR_H

#include "plib/config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef HAVE_IPV6


/* XXX: Uuuhhh.. Maybe. Check this macros for portability! */
#ifndef IN6_V4MAPPED_TO_IPADDR
#define	IN6_V4MAPPED_TO_IPADDR(v6, v4) \
	(* ((unsigned long *) &(v4)) = ((unsigned long *) (v6))[3])
#endif


struct sockaddr_gen
{
    union {
	struct sockaddr         _sg_sa;
	struct sockaddr_in      _sg_sin;
	struct sockaddr_in6     _sg_sin6;
    } sg_addr;
};

#define sg_sa           sg_addr._sg_sa
#define sg_sin          sg_addr._sg_sin
#define sg_family       sg_sa.sa_family
#define sg_sin6         sg_addr._sg_sin6

#define SGFAM(sag)	((sag).sg_family)
#define SGSIZE(sag)	(SGFAM(sag) == AF_INET6 ? \
			    sizeof (struct in6_addr) : \
			    sizeof (struct in_addr))
#define SGSOCKSIZE(sag)	(SGFAM(sag) == AF_INET6 ? \
			    sizeof (struct sockaddr_in6) : \
			    sizeof (struct sockaddr_in))
#define SGPORT(sag)	(*(SGFAM(sag) == AF_INET6 ? \
                            &(sag).sg_sin6.sin6_port : \
			    &(sag).sg_sin.sin_port))
#define SGADDRP(sag)	(SGFAM(sag) == AF_INET6 ? \
                            (char *) &(sag).sg_sin6.sin6_addr : \
			    (char *) &(sag).sg_sin.sin_addr)

#define SGINIT(sag)	(memset(&(sag), 0, sizeof((sag))), \
			((sag).sg_family = AF_INET6))


/* Some helper macros found on Solaris 2 */
#ifndef IN6_V4MAPPED_TO_INADDR
#define	IN6_V4MAPPED_TO_INADDR(v6, v4) \
	((v4)->s_addr = ((uint32_t *) (v6))[3])
#endif

#ifndef IN6_V4MAPPED_TO_IPADDR
#define	IN6_V4MAPPED_TO_IPADDR(v6, v4) \
	((v4) = ((uint32_t *) (v6))[3])
#endif

#else /* !HAVE_IPV6 */

#define	sockaddr_gen	sockaddr_in

#define SGFAM(sag)	((sag).sin_family)
#define SGSIZE(sag)	sizeof(struct in_addr)
#define SGSOCKSIZE(sag)	sizeof(struct sockaddr_in)
#define SGPORT(sag)	((sag).sin_port)
#define SGADDRP(sag)	((char *) &(sag).sin_addr)

#define SGINIT(sag)	(memset(&(sag), 0, sizeof((sag))), \
			((sag).sin_family = AF_INET))

#endif /* HAVE_IPV6 */
#endif
