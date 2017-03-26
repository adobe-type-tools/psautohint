/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"

#define FIXEDSCALE ((float)(FixOne))

void
acfixtopflt(Fixed x, float* pf)
{
    *pf = (float)x / FIXEDSCALE;
}

Fixed
acpflttofix(float* pf)
{
    float f = *pf;
    if (f >= FixedPosInf / FIXEDSCALE)
        return FixedPosInf;
    if (f <= FixedNegInf / FIXEDSCALE)
        return FixedNegInf;
    return (Fixed)(f * FIXEDSCALE);
}
