/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "basic.h"

#ifndef BUILDFONT_H
#define BUILDFONT_H

#ifdef EXECUTABLE
#define OUTPUTBUFF stdout
#else
#define OUTPUTBUFF stderr
#endif

/*****************************************************************************/
/* the following are for scanning "PostScript format" input file lines       */
/*****************************************************************************/

#define STREQ(a,b)               (((a)[0] == (b)[0]) && (strcmp((a),(b)) == 0))
#define STRNEQ(a,b)              (((a)[0] != (b)[0]) || (strcmp((a),(b)) != 0))

/*****************************************************************************/
/* Defines character point coordinates.                                      */
/*****************************************************************************/
typedef struct
   {
   int32_t x, y;
   } Cd, *CdPtr;


extern char *
AllocateMem(unsigned int, unsigned int, const char *);

extern char *
ReallocateMem(char *, unsigned int, const char *);

extern void
UnallocateMem(void *ptr);

#endif /*BUILDFONT_H*/
