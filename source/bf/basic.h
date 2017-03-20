/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/
/* basic.h */

#ifndef BASIC_H
#define BASIC_H
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include "pubtypes.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <sys/types.h>
typedef bool boolean;

#define FALSE false
#define TRUE true

/* macro definitions */
#define NUMMIN(a, b) ((a) <= (b) ? (a) : (b))
#define NUMMAX(a, b) ((a) >= (b) ? (a) : (b))
#ifdef ABS
#undef ABS
#endif
#define ABS(a) ((a) >= 0 ? (a) : -(a))
/* Round the same way as PS. i.e. -6.5 ==> -6.0 */
#define LROUND(a) ((a > 0) ? (int32_t)(a + 0.5) : ((a + (int32_t)(-a)) == -0.5) ? (int32_t) a : (int32_t)(a - 0.5))
#define	 SCALEDRTOL(a, s) (a<0 ? (int32_t) ((a*s) - 0.5) : (int32_t) ((a*s) + 0.5))


typedef int indx;		/* for indexes that could be either short or
				   long - let the compiler decide */

#define RAWPSDIR "pschars"
#define ILLDIR "ill"
#define BEZDIR "bez"
#define UNSCALEDBEZ "bez.unscaled"
#define TMPDIR "._tmp"
#ifdef MAXPATHLEN
#undef MAXPATHLEN
#endif
#define MAXPATHLEN 1024		/* max path name len for a dir or folder
				   (includes 1 byte for null terminator) */

/* defines for LogMsg code param */
#define OK 0
#define NONFATALERROR 1
#define FATALERROR 2

/* defines for LogMsg level param */
#define INFO 0
#define WARNING 1
#define LOGERROR 2

#define MAXMSGLEN 500		/* maximum message length */
extern char globmsg[MAXMSGLEN + 1];	/* used to format the string passed to LogMsg */

extern void LogMsg(
    char *, int16_t, int16_t, boolean
);

extern int16_t WarnCount(
    void
);

extern void ResetWarnCount(
    void
);

#endif /*BASIC_H*/
