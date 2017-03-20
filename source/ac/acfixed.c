/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/
/* fixed.c */


#include "ac.h"

#define fixedScale ((float)(FixOne))

void acfixtopflt(x, pf)
  Fixed x; float *pf;

#ifdef sun3
 { *pf = (float) x; fixscale(pf); }
#else /* sun4 */
 {*pf = (float) x / fixedScale;}
#endif

Fixed acpflttofix(pf)
  float *pf;
  {
  float f = *pf;
  if (f >= FixedPosInf / fixedScale) return FixedPosInf;
  if (f <= FixedNegInf / fixedScale) return FixedNegInf;
  return (Fixed) (f * fixedScale);
  }

