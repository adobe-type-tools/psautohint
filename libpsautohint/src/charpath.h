/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#ifndef CHARPATH_H
#define CHARPATH_H

#include "ac.h"

typedef struct _t_hintelt {
  struct _t_hintelt *next;
  int16_t type; /* RB, RY, RM, RV */
  Fixed leftorbot, rightortop;
  int32_t pathix1, pathix2;
  } HintElt, *PHintElt;
  
typedef struct _t_charpathelt {
  int16_t type; /* RMT, RDT, RCT, CP */
  /* the following fields must be cleared in charpathpriv.c/CheckPath */
  bool isFlex:1, sol:1, eol:1, remove:1;
  int unused:12;
  PHintElt hints;
  Fixed x, y, x1, y1, x2, y2, x3, y3; /* absolute coordinates */
  int32_t rx, ry, rx1, ry1, rx2, ry2, rx3, ry3;  /* relative coordinates */
  } CharPathElt, *PCharPathElt;

typedef struct _t_pathlist {
  PCharPathElt path;
  PHintElt mainhints;
  int32_t sb;
  int16_t width;
} PathList, *PPathList;

extern int32_t gPathEntries;  /* number of elements in a character path */
extern bool gAddHints;  /* whether to include hints in the font */

PCharPathElt AppendCharPathElement(int);

void ResetMaxPathEntries(void);

void SetCurrPathList(PPathList);

void SetHintsElt(int16_t, CdPtr, int32_t, int32_t, bool);

void SetNoHints(void);

#endif /*CHARPATH_H*/

