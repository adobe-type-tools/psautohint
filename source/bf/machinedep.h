/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/
/* machinedep.h - defines machine dependent procedures used by
   buildfont.

history:

Judy Lee: Mon Jul 18 14:13:03 1988
End Edit History
*/

#include "basic.h"

#ifndef MACHINEDEP_H
#define MACHINEDEP_H

extern void set_errorproc( int (*)(int16_t) );

void FlushLogFiles();
void OpenLogFiles();

#if defined(_MSC_VER) && ( _MSC_VER < 1800)
float roundf(float x);
#endif

#endif /*MACHINEDEP_H*/
