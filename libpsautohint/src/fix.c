/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"

static Fixed bPrev, tPrev;

void
InitFix(int32_t reason)
{
    switch (reason) {
        case STARTUP:
        case RESTART:
            bPrev = tPrev = FIXED_MAX;
    }
}

static bool
CheckForInsideBands(Fixed loc, Fixed* blues, int32_t numblues)
{
    int32_t i;
    for (i = 0; i < numblues; i += 2) {
        if (loc >= blues[i] && loc <= blues[i + 1])
            return true;
    }
    return false;
}

#define bFuzz (FixInt(6))
static void
CheckForNearBands(Fixed loc, Fixed* blues, int32_t numblues)
{
    int32_t i;
    bool bottom = true;
    for (i = 0; i < numblues; i++) {
        if ((bottom && loc >= blues[i] - bFuzz && loc < blues[i]) ||
            (!bottom && loc <= blues[i] + bFuzz && loc > blues[i])) {
            LogMsg(
              INFO, OK, "Near miss %s horizontal zone at %g instead of %g.",
              bottom ? "below" : "above", FixToDbl(loc), FixToDbl(blues[i]));
        }
        bottom = !bottom;
    }
}

bool
FindLineSeg(Fixed loc, HintSeg* sL)
{
    while (sL != NULL) {
        if (sL->sLoc == loc && sL->sType == sLINE)
            return true;
        sL = sL->sNxt;
    }
    return false;
}

#if 1
/* Traverses hSegList to check for near misses to
   the horizontal alignment zones. The list contains
   segments that may or may not have hints added. */
void
CheckTfmVal(HintSeg* hSegList, Fixed* bandList, int32_t length)
{
    HintSeg* sList = hSegList;

    while (sList != NULL) {
        Fixed tfmval = -sList->sLoc;
        if ((length >= 2) && !gBandError &&
            !CheckForInsideBands(tfmval, bandList, length))
            CheckForNearBands(tfmval, bandList, length);
        sList = sList->sNxt;
    }
}
#else
void
CheckTfmVal(Fixed b, Fixed t, bool vert)
{
    if (t < b) {
        Fixed tmp;
        tmp = t;
        t = b;
        b = tmp;
    }
    if (!vert && (lenTopBands >= 2 || lenBotBands >= 2) && !bandError &&
        !CheckForInsideBands(t, topBands, lenTopBands) &&
        !CheckForInsideBands(b, botBands, lenBotBands)) {
        CheckForNearBands(t, topBands, lenTopBands);
        CheckForNearBands(b, botBands, lenBotBands);
    }
}
#endif

static void
CheckVal(HintVal* val, bool vert)
{
    Fixed* stems;
    int32_t numstems, i;
    Fixed minDiff, minW, b, t, w;
    if (vert) {
        stems = gVStems;
        numstems = gNumVStems;
        b = val->vLoc1;
        t = val->vLoc2;
    } else {
        stems = gHStems;
        numstems = gNumHStems;
        b = -val->vLoc1;
        t = -val->vLoc2;
    }
    w = abs(t - b);
    minDiff = FixInt(1000);
    minW = 0;
    for (i = 0; i < numstems; i++) {
        Fixed wd = stems[i];
        Fixed diff = abs(wd - w);
        if (diff < minDiff) {
            minDiff = diff;
            minW = wd;
            if (minDiff == 0)
                break;
        }
    }
    if (minDiff == 0 || minDiff > FixInt(2))
        return;
    if (b != bPrev || t != tPrev) {
        bool curve = false;
        if ((vert && (!FindLineSeg(val->vLoc1, leftList) ||
                      !FindLineSeg(val->vLoc2, rightList))) ||
            (!vert && (!FindLineSeg(val->vLoc1, botList) ||
                       !FindLineSeg(val->vLoc2, topList))))
            curve = true;
        if (!val->vGhst)
            ReportStemNearMiss(vert, w, minW, b, t, curve);
    }
    bPrev = b;
    tPrev = t;
}

void
CheckVals(HintVal* vlst, bool vert)
{
    while (vlst != NULL) {
        CheckVal(vlst, vert);
        vlst = vlst->vNxt;
    }
}
