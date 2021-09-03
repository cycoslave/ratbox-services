/* $Id: stdinc.h 23616 2007-02-13 22:08:37Z leeh $ */
#ifndef INCLUDED_stdinc_h
#define INCLUDED_stdinc_h

#include "setup.h"

#include <stdio.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <limits.h>

#ifdef HAVE_ERRNO_H
#include <errno.h>
#else
extern int errno;
#endif

#include <string.h>
#include <strings.h>
#include <ctype.h>

#include <time.h>
#include <sys/time.h>

#include <assert.h>

#include "config.h"
#include "tools.h"

#ifdef strdupa
#define LOCAL_COPY(s) strdupa(s) 
#else
#if defined(__INTEL_COMPILER) || defined(__GNUC__)
# define LOCAL_COPY(s) __extension__({ char *_s = alloca(strlen(s) + 1); strcpy(_s, s); _s; })
#else
# define LOCAL_COPY(s) strcpy(alloca(strlen(s) + 1), s) /* XXX Is that allowed? */
#endif /* defined(__INTEL_COMPILER) || defined(__GNUC__) */
#endif /* strdupa */

#if defined(__GNUC__) || defined(__INTEL_COMPILER)
#define PRINTFLIKE(fmtarg, firstvararg) \
	__attribute__((__format__ (__printf__, fmtarg, firstvararg)))
#define SCANFLIKE(fmtarg, firstvararg) \
	__attribute__((__format__ (__scanf__, fmtarg, firstvararg)))
#else
#define PRINTFLIKE(fmtarg, firstvararg)
#define SCANFLIKE(fmtarg, firstvararg)
#endif /* defined(__INTEL_COMPILER) || defined(__GNUC__) */

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#endif
