/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/
/* This source file should contain any procedure(s) that are called
   from AC.  It should not contain calls to procedures implemented
   in object files that are not bound into AC. */ 

#include "basic.h"
#include "bftoac.h"
#include "charlistpriv.h"

int32_t clsize = 0;
struct cl_elem *charlist = NULL;

extern bool ReadCharFileNames (fname, start)
char *fname;
bool *start;
{
  static indx i;
  if (*start)
  {
    i = 0;
    *start = false;
  }
  while (i < clsize)
  {
    if (charlist[i].derived || charlist[i].composite)
    {
      i++;
      continue;
    }
    strcpy (fname, charlist[i].filename);
    i++;
    return true;
  }
  return false;
}

