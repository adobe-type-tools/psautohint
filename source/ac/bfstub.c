/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/

/* bfstub.c - to resolve otherwise unresolved external references.  These need to
   be defined because of AC's dependency on filookup, but not on fiblues. */
#include "ac.h"

/* Procedures defined in bf/fiblues.c - BuildFont */
bool checkbandspacing()
{
	return true;
}

bool checkbandwidths(overshootptsize, undershoot)
float overshootptsize;
bool undershoot;
{
	return true;
}

int build_bands()
{
	return true;
}

void SetMasterDir(dirindx)
indx dirindx;
{
}

void WriteBlendedArray(barray, totalvals, subarraysz, str, dirCount)
int *barray;
char *str;
int totalvals, subarraysz;
int16_t dirCount;
{
}

bool multiplemaster = 0; /* AC is never run at the MM level */
