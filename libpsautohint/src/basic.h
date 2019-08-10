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

#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef int32_t               Fixed;
typedef int indx;		/* for indexes that could be either short or
				   long - let the compiler decide */

/* macro definitions */
#define NUMMIN(a, b) ((a) <= (b) ? (a) : (b))
#define NUMMAX(a, b) ((a) >= (b) ? (a) : (b))

/* Round the same way as PS. i.e. -6.5 ==> -6.0 */
#define LROUND(a) ((a > 0) ? (int32_t)(a + .5f) : ((a + (int32_t)(-a)) == -.5f) ? (int32_t) a : (int32_t)(a - .5f))

#ifndef MAXINT
#define MAXINT                   32767
#endif

#endif /*BASIC_H*/
