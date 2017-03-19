/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */

#ifndef _NUMTYPES_H
#define _NUMTYPES_H

/***********************************************************************/
/* The present file assumes:                                           */
/*                                                                     */
/*             char:   8 bits                                          */
/*             short: 16 bits                                          */
/*             long:  32 bits                                          */
/*                                                                     */
/* This assumption is currently true for Sun, PC, and Mac environments.*/
/* When this assumption is no longer true, #ifdef the code accordingly */
/* and change this comment.                                            */
/***********************************************************************/

typedef long                            Fixed;


typedef long            Int32;
#define MAX_INT32       ((Int32)0x7FFFFFFF)
#define MIN_INT32       ((Int32)0x80000000)

typedef unsigned long   Card32;
#define MAX_CARD32      ((Card32)0xFFFFFFFF)

typedef short           Int16;
#define MAX_INT16       ((Int16)0x7FFF)
#define MIN_INT16       ((Int16)0x8000)

typedef unsigned short  Card16;
#define MAX_CARD16      ((Card16)0xFFFF)

typedef unsigned char   Card8;
#define MAX_CARD8       ((Card8)0xFF)

#endif/*_NUMTYPES_H*/
