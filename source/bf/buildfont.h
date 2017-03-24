/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */

#include "basic.h"

#ifndef BUILDFONT_H
#define BUILDFONT_H

#ifdef EXECUTABLE
#define OUTPUTBUFF stdout
#else
#define OUTPUTBUFF stderr
#endif

#define FONTSTKLIMIT             22
                                    /*****************************************/
                                    /* font interpreter stack limit - Note   */
                                    /* that the actual limit is 24, but      */
                                    /* because the # of parameters and       */
                                    /* callothersubr # are also pushed on    */
                                    /* the stack, the effective value is 22. */
                                    /*****************************************/

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


extern bool scalinghints;

/*****************************************************************************/
/* Sets the global variable charsetDir.                                      */
/*****************************************************************************/
extern void
set_charsetdir(char *);

/*****************************************************************************/
/* Returns the total number of input directories.                            */
/*****************************************************************************/
extern int16_t
GetTotalInputDirs(void);

/*****************************************************************************/
/* Deallocates memory and deletes temporary files.                           */
/*****************************************************************************/
extern int
cleanup(int16_t);

extern char *
AllocateMem(unsigned int, unsigned int, const char *);

extern char *
ReallocateMem(char *, unsigned int, const char *);

extern void
UnallocateMem(void *ptr);

#endif /*BUILDFONT_H*/
