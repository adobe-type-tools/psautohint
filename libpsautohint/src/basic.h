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
#include <math.h>

#if !defined(_MSC_VER) || _MSC_VER >= 1800
#include <stdint.h>
#include <stdbool.h>
#else
#include "winstdint.h"
typedef unsigned char bool;
#define true 1
#define false 0
#endif

#ifdef _MSC_VER
#if _MSC_VER < 1900
#define snprintf(buf, size, ...) _snprintf_s(buf, size, _TRUNCATE, __VA_ARGS__)
#endif /* _MSC_VER < 1900 */

#if _MSC_VER < 1800
double round(double x);
float roundf(float x);
#endif /* _MSC_VER < 1800 */
#endif /* _MSC_VER */

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
