/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"

#define maxFixes (100)
static Fixed HFixYs[maxFixes], HFixDYs[maxFixes];
static Fixed VFixXs[maxFixes], VFixDXs[maxFixes];
static int32_t HFixCount, VFixCount;
static Fixed bPrev, tPrev;

void
InitFix(int32_t reason)
{
    switch (reason) {
        case STARTUP:
        case RESTART:
            HFixCount = VFixCount = 0;
            bPrev = tPrev = FixedPosInf;
    }
}

static void
RecordHFix(Fixed y, Fixed dy)
{
    HFixYs[HFixCount] = y;
    HFixDYs[HFixCount] = dy;
    HFixCount++;
}

static void
RecordVFix(Fixed x, Fixed dx)
{
    VFixXs[VFixCount] = x;
    VFixDXs[VFixCount] = dx;
    VFixCount++;
}

static void
RecordForFix(bool vert, Fixed w, Fixed minW, Fixed b, Fixed t)
{
    Fixed mn, mx, delta;
    if (b < t) {
        mn = b;
        mx = t;
    } else {
        mn = t;
        mx = b;
    }
    if (!vert && HFixCount + 4 < maxFixes && gAutoHFix) {
        Fixed fixdy = w - minW;
        if (abs(fixdy) <= FixOne) {
            RecordHFix(mn, fixdy);
            RecordHFix(mx - fixdy, fixdy);
        } else {
            delta = FixHalfMul(fixdy);
            RecordHFix(mn, delta);
            RecordHFix(mn + fixdy, -delta);
            RecordHFix(mx, -delta);
            RecordHFix(mx - fixdy, delta);
        }
    } else if (vert && VFixCount + 4 < maxFixes && gAutoVFix) {
        Fixed fixdx = w - minW;
        if (abs(fixdx) <= FixOne) {
            RecordVFix(mn, fixdx);
            RecordVFix(mx - fixdx, fixdx);
        } else {
            delta = FixHalfMul(fixdx);
            RecordVFix(mn, delta);
            RecordVFix(mn + fixdx, -delta);
            RecordVFix(mx, -delta);
            RecordVFix(mx - fixdx, delta);
        }
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
            ReportBandNearMiss(bottom ? "below" : "above", loc, blues[i]);
        }
        bottom = !bottom;
    }
}

bool
FindLineSeg(Fixed loc, PClrSeg sL)
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
CheckTfmVal(PClrSeg hSegList, Fixed* bandList, int32_t length)
{
    PClrSeg sList = hSegList;

    while (sList != NULL) {
        Fixed tfmval = itfmy(sList->sLoc);
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

void
CheckVal(PClrVal val, bool vert)
{
    Fixed* stems;
    int32_t numstems, i;
    Fixed minDiff, minW, b, t, w;
    if (vert) {
        stems = gVStems;
        numstems = gNumVStems;
        b = itfmx(val->vLoc1);
        t = itfmx(val->vLoc2);
    } else {
        stems = gHStems;
        numstems = gNumHStems;
        b = itfmy(val->vLoc1);
        t = itfmy(val->vLoc2);
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
    if ((vert && gAutoVFix) || (!vert && gAutoHFix))
        RecordForFix(vert, w, minW, b, t);
}

void
CheckVals(PClrVal vlst, bool vert)
{
    while (vlst != NULL) {
        CheckVal(vlst, vert);
        vlst = vlst->vNxt;
    }
}

static void
FixH(PPathElt e, Fixed fixy, Fixed fixdy)
{
    PPathElt prev, nxt;
    RMovePoint(0, fixdy, cpStart, e);
    RMovePoint(0, fixdy, cpEnd, e);
    prev = e->prev;
    if (prev != NULL && prev->type == CURVETO && prev->y2 == fixy)
        RMovePoint(0, fixdy, cpCurve2, prev);
    if (e->type == CLOSEPATH)
        e = GetDest(e);
    nxt = e->next;
    if (nxt != NULL && nxt->type == CURVETO && nxt->y1 == fixy)
        RMovePoint(0, fixdy, cpCurve1, nxt);
}

static void
FixHs(Fixed fixy, Fixed fixdy)
{ /* y dy in user space */
    PPathElt e;
    Fixed xlst = 0, ylst = 0, xinit = 0, yinit = 0;
    fixy = tfmy(fixy);
    fixdy = dtfmy(fixdy);
    e = gPathStart;
    while (e != NULL) {
        switch (e->type) {
            case MOVETO:
                xlst = xinit = e->x;
                ylst = yinit = e->y;
                break;
            case LINETO:
                if (e->y == fixy && ylst == fixy)
                    FixH(e, fixy, fixdy);
                xlst = e->x;
                ylst = e->y;
                break;
            case CURVETO:
                xlst = e->x3;
                ylst = e->y3;
                break;
            case CLOSEPATH:
                if (yinit == fixy && ylst == fixy && xinit != xlst)
                    FixH(e, fixy, fixdy);
                break;
            default: {
                LogMsg(LOGERROR, NONFATALERROR,
                       "Illegal operator in path list in %s.\n", gGlyphName);
            }
        }
        e = e->next;
    }
}

static void
FixV(PPathElt e, Fixed fixx, Fixed fixdx)
{
    PPathElt prev, nxt;
    RMovePoint(fixdx, 0, cpStart, e);
    RMovePoint(fixdx, 0, cpEnd, e);
    prev = e->prev;
    if (prev != NULL && prev->type == CURVETO && prev->x2 == fixx)
        RMovePoint(fixdx, 0, cpCurve2, prev);
    if (e->type == CLOSEPATH)
        e = GetDest(e);
    nxt = e->next;
    if (nxt != NULL && nxt->type == CURVETO && nxt->x1 == fixx)
        RMovePoint(fixdx, 0, cpCurve1, nxt);
}

static void
FixVs(Fixed fixx, Fixed fixdx)
{ /* x dx in user space */
    PPathElt e;
    Fixed xlst = 0, ylst = 0, xinit = 0, yinit = 0;
    fixx = tfmx(fixx);
    fixdx = dtfmx(fixdx);
    e = gPathStart;
    while (e != NULL) {
        switch (e->type) {
            case MOVETO:
                xlst = xinit = e->x;
                ylst = yinit = e->y;
                break;
            case LINETO:
                if (e->x == fixx && xlst == fixx)
                    FixV(e, fixx, fixdx);
                xlst = e->x;
                ylst = e->y;
                break;
            case CURVETO:
                xlst = e->x3;
                ylst = e->y3;
                break;
            case CLOSEPATH:
                if (xinit == fixx && xlst == fixx && yinit != ylst)
                    FixV(e, fixx, fixdx);
                break;
            default: {
                LogMsg(LOGERROR, NONFATALERROR,
                       "Illegal operator in point list in %s.\n", gGlyphName);
            }
        }
        e = e->next;
    }
}

bool
DoFixes(void)
{
    bool didfixes = false;
    int32_t i;
    if (HFixCount > 0 && gAutoHFix) {
        PrintMessage("Fixing horizontal near misses.");
        didfixes = true;
        for (i = 0; i < HFixCount; i++)
            FixHs(HFixYs[i], HFixDYs[i]);
    }
    if (VFixCount > 0 && gAutoVFix) {
        PrintMessage("Fixing vertical near misses.");
        didfixes = true;
        for (i = 0; i < VFixCount; i++)
            FixVs(VFixXs[i], VFixDXs[i]);
    }
    return didfixes;
}
