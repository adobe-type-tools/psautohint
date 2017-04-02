/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#ifndef BASIC_H
#define BASIC_H
#include <stdlib.h>
#include <ctype.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <sys/types.h>

#if !defined(_MSC_VER) || _MSC_VER >= 1800
#include <stdint.h>
#include <stdbool.h>
#else
#include "winstdint.h"
typedef unsigned char bool;
#define true 1
#define false 0
#endif

#if defined(_MSC_VER) && _MSC_VER < 1900
#define snprintf(buf, size, ...) _snprintf_s(buf, size, _TRUNCATE, __VA_ARGS__)
#endif

typedef int32_t               Fixed;
typedef int indx;		/* for indexes that could be either short or
				   long - let the compiler decide */

/* macro definitions */
#define NUMMIN(a, b) ((a) <= (b) ? (a) : (b))
#define NUMMAX(a, b) ((a) >= (b) ? (a) : (b))

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
    char *, int16_t, int16_t
);

#endif /*BASIC_H*/
