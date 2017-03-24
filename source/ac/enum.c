/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/
/* enum.c */


#include "ac.h"

bool DoArgsIgnoreTime(cnt, nms, extracolor)
int cnt; char *nms[]; bool extracolor; {
  int i;
  bool result = true;
  
  for (i = 0; i < cnt; i++) {
    if (!DoFile(nms[i], extracolor))
      result = false;
    }
  return result;
  }

