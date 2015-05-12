/*
** a_alloc.c - Allocation routines that either succeeds or abort.
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

#define PLIB_IN_ALLOC_C

#include "plib/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>

#include "plib/safeio.h"
#include "plib/threads.h"

#include "plib/aalloc.h"

typedef struct aahdr
{
    struct aahdr *prev;
    struct aahdr *next;
    char fun[64];
    char what[64];
    size_t size;
    unsigned short dummy;
    unsigned short magic;
} AAHDR;

#define AAHDR_MAGIC_1 0x1234
#define AAHDR_MAGIC_2 0x56


static pthread_mutex_t aa_mtx;
static AAHDR *top = NULL;
static int aa_debug = 0;

static inline int
aahdr_valid(AAHDR *ap)
{
    unsigned char *cp;

    
    if (ap == NULL)
	return 0;

    if (ap->magic != AAHDR_MAGIC_1)
	return 0;

    cp = (unsigned char *) (ap+1);

    if (cp[ap->size] != AAHDR_MAGIC_2)
    {
	syslog(LOG_ERR, "aalloc: Buffer overrun for object at %p\n", (ap+1));
	return 0;
    }

    return 1;
}
    

void
a_dump(FILE *fp)
{
    AAHDR *ap;
    unsigned char *p;
    

    fprintf(fp, "a_dump(): Start\n");
    
    pthread_mutex_lock(&aa_mtx);
    
    ap = top;
    while (ap)
    {
	p = (unsigned char *) (ap+1);
	
	fprintf(fp, "%08p:  magic=%04x:%02x, size=%lu, function=%s, object=%s\n",
	       ap, ap->magic, p[ap->size], ap->size,
		ap->fun ? ap->fun : "<null>",
		ap->what ? ap->what : "<null>");
	
	ap = ap->next;
    }
    pthread_mutex_unlock(&aa_mtx);

    fprintf(fp, "a_dump(): Stop\n");
}


void
a_init(void)
{
    aa_debug = (getenv("AA_DEBUG") != NULL);
    
    top = NULL;
    pthread_mutex_init(&aa_mtx, NULL);
}


/*
** A "safe" malloc, that always succeeds (or logs an
** error to syslog and then abort()'s.
*/

static inline void *
safe_malloc(size_t size,
	    const char *fun,
	    const char *what)
{
    int rsize;
    void *p;
    AAHDR *ap;
    unsigned char *cp;
    

    if (aa_debug)
	rsize = size + sizeof(AAHDR) + 1;
    else
	rsize = size;
    
    p = malloc(rsize);
    if (p == NULL)
    {
	syslog(LOG_ERR, "%s: %s: malloc(%lu - real=%lu): %m",
	       fun ? fun : "<unknown function>",
	       what ? what : "<unknown object>",
	       (unsigned long) size,
	       (unsigned long) rsize);
	
	s_abort();
    }

    if (aa_debug)
    {
	ap = (AAHDR *) p;

	pthread_mutex_lock(&aa_mtx);
	
	ap->prev = NULL;
	if (top)
	    top->prev = ap;
	ap->next = top;
	top = ap;
	
	ap->size = size;
	strlcpy(ap->fun, fun, sizeof(ap->fun));
	strlcpy(ap->what, what, sizeof(ap->what));
	ap->magic = AAHDR_MAGIC_1;
	
	cp = (unsigned char *) (ap+1);
	cp[size] = AAHDR_MAGIC_2;
	
	p = (void *) cp;

	pthread_mutex_unlock(&aa_mtx);
    }
    
    return p;
}



void *
a_malloc(size_t size, const char *what)
{
    void *p;

    
    p = safe_malloc(size, "a_malloc", what);
    memset(p, 0, size);
    
    return p;
}


void *
a_realloc(void *oldp, size_t nsize, const char *what)
{
    void *p;

    
    if (!oldp)
	return safe_malloc(nsize, "a_realloc", what);

    if (aa_debug)
    {
	AAHDR *ap;

	ap = (AAHDR *) oldp;
	--ap;

	if (!aahdr_valid(ap))
	{
	    syslog(LOG_ERR, "a_realloc: INVALID pointer");
	    s_abort();
	}
	
	p = safe_malloc(nsize, "a_realloc", what);
	memcpy(p, oldp, ap->size > nsize ? nsize : ap->size);
	a_free(oldp);

	return p;
    }

    p = (void *) realloc(oldp, nsize);
    if (p == NULL)
    {
	syslog(LOG_ERR, "a_realloc: %s: realloc(...,%lu): %m",
	       what ? what : "<unknown object>",
	       (unsigned long) nsize);
	s_abort();
    }

    return p;
}


void
a_free(void *p)
{
    AAHDR *ap;

    
    if (p == NULL)
	return;


    if (aa_debug)
    {
	ap = (AAHDR *) p;
	--ap;

	if (!aahdr_valid(ap))
	{
	    syslog(LOG_ERR, "a_free: INVALID pointer");
	    s_abort();
	}

	pthread_mutex_lock(&aa_mtx);
	if (ap->prev)
	    ap->prev->next = ap->next;
	if (ap->next)
	    ap->next->prev = ap->prev;
	if (top == ap)
	    top = ap->next;
	pthread_mutex_unlock(&aa_mtx);
    }
    
    free(p);
}


char *
a_strndup(const char *s,
	  size_t len,
	  const char *what)
{
    char *ns;
    
    
    if (s == NULL)
	return NULL;

    ns = (char *) safe_malloc(len+1, "a_strndup", what);
    
    memcpy(ns, s, len);
    ns[len] = '\0';
    
    return ns;
}


char *
a_strdup(const char *s,
	 const char *what)
{
    char *ns;
    int len;

    
    if (s == NULL)
	return NULL;

    len = strlen(s);
    ns = (char *) safe_malloc(len+1, "a_strdup", what);
    
    memcpy(ns, s, len+1);
    
    return ns;
}

