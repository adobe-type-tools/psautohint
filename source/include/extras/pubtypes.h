/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************
 * SCCS Id:    @(#)pubtypes.h	1.4
 * Changed:    7/25/95 18:37:56
 ***********************************************************************/

#ifndef _PUBTYPES_H
#define _PUBTYPES_H


/***********************************************************************/
/* THIS FILE IS PROVIDED FOR BACKWARD COMPATIBILITY ONLY!!!            */
/* Its use for new programs is not recommended because it contains     */
/* constructs that have been demonstrated to cause integration         */
/* problems.                                                           */
/***********************************************************************/

/***********************************************************************/
/* Numeric definitions                                                 */
/***********************************************************************/

#include <stdint.h>

typedef int32_t               Fixed;
typedef int32_t               integer;
#define MAXinteger            INT32_MAX
#define MINinteger            INT32_MIN

/***********************************************************************/
/* Other definitions                                                   */
/***********************************************************************/

                                    /***********************************/
                                    /* Inline Functions                */
                                    /***********************************/
#ifndef MIN
#define MIN(a,b)        ((a)<(b)?(a):(b))
#endif
#ifndef MAX
#define MAX(a,b)        ((a)>(b)?(a):(b))
#endif

                                    /***********************************/
                                    /* Declarative Sugar               */
                                    /***********************************/
#ifndef true
#define true            1
#define false           0
#endif 

                                    /***********************************/
                                    /* Reals and Fixed point           */
                                    /***********************************/
typedef float           real;
typedef double          longreal;

#endif/*_PUBTYPES_H*/
