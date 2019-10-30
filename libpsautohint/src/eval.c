/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"
#include "bbox.h"

#define MAXF (1 << 15)
static void
AdjustVal(Fixed* pv, Fixed l1, Fixed l2, Fixed dist, Fixed d, bool hFlg)
{
    float v, q, r1, r2, rd;
    /* DEBUG 8 BIT. To get the saem result as the old auothint, had to change
     from FixedOne to FixedTwo. Since the returned weight is proportional to the
     square of l1 and l2,
     these need to be clamped to twice the old clamped value, else when the
     clamped values are used, the weight comes out as 1/4 of the original value.
     */
    if (dist < FixTwo)
        dist = FixTwo;
    if (l1 < FixTwo)
        l1 = FixTwo;
    if (l2 < FixTwo)
        l2 = FixTwo;
    if (abs(l1) < MAXF)
        r1 = (float)(l1 * l1);
    else {
        r1 = (float)l1;
        r1 = r1 * r1;
    }
    if (abs(l2) < MAXF)
        r2 = (float)(l2 * l2);
    else {
        r2 = (float)l2;
        r2 = r2 * r2;
    }
    if (abs(dist) < MAXF)
        q = (float)(dist * dist);
    else {
        q = (float)dist;
        q = q * q;
    }
    v = (float)((1000.0f * r1 * r2) / (q * q));
    if (d <= (hFlg ? gHBigDist : gVBigDist))
        goto done;
    acfixtopflt(d, &rd);
    q = (hFlg ? gHBigDistR : gVBigDistR) / rd; /* 0 < q < 1.0 */
    if (q <= 0.5f) {
        v = 0.0;
        goto done;
    }
    q *= q;
    q *= q;
    q *= q;    /* raise q to 8th power */
    v = v * q; /* if d is twice bigDist, value goes down by factor of 256 */
done:
    if (v > gMaxVal)
        v = gMaxVal;
    else if (v > 0.0f && v < gMinVal)
        v = gMinVal;
    *pv = acpflttofix(&v);
}

static Fixed
CalcOverlapDist(Fixed d, Fixed overlaplen, Fixed minlen)
{
    float r = (float)d, ro = (float)overlaplen, rm = (float)minlen;
    r = r * ((float)(1.0f + 0.4f * (1.0f - ro / rm)));
    d = (Fixed)r;
    return d;
}

#define GapDist(d)                                                             \
    (((d) < FixInt(127)) ? FTrunc(((d) * (d)) / 40)                            \
                         : ((int32_t)(((double)(d)) * (d) / (40 * 256))))
/* if d is >= 127.0 Fixed, then d*d will overflow the signed int 16 bit value.
 */
/* DEBUG 8 BIT. No idea why d*d was divided by 20, but we need to divide it by 2
 more to get a dist that is only 2* the old autohint value.
 With the 8.8 fixed coordinate system, we still overflow a int32_t with
 d*(d/40), so rather than casting this to a int32_t and then doing >>8, we need
 to divide by 256, then cast to int32_t.
 I also fail to understand why the original used FTrunc, which right shifts by
 256. For the current coordinate space, which has a fractional part of 8 bits,
 you do need to divide by 256 after doing a simple int multiply, but the
 previous coordinate space
    has a 7 bit Fixed fraction, and should be dividing by 128. I suspect that
 there was a yet earlier version which used a 8 bit fraction, and this is a bug.
 */

static void
EvalHPair(HintSeg* botSeg, HintSeg* topSeg, Fixed* pspc, Fixed* pv)
{
    Fixed brght, blft, bloc, tloc, trght, tlft;
    Fixed mndist, dist, dy;
    bool inBotBand, inTopBand;
    *pspc = 0;
    brght = botSeg->sMax;
    blft = botSeg->sMin;
    trght = topSeg->sMax;
    tlft = topSeg->sMin;
    bloc = botSeg->sLoc;
    tloc = topSeg->sLoc;
    dy = abs(bloc - tloc);
    if (dy < gMinDist) {
        *pv = 0;
        return;
    }
    inBotBand = InBlueBand(bloc, gLenBotBands, gBotBands);
    inTopBand = InBlueBand(tloc, gLenTopBands, gTopBands);
    if (inBotBand && inTopBand) { /* delete these */
        *pv = 0;
        return;
    }
    if (inBotBand || inTopBand) /* up the priority of these */
        *pspc = FixInt(2);
    /* left is always < right */
    if ((tlft <= brght) && (trght >= blft)) { /* overlap */
        Fixed overlaplen = NUMMIN(trght, brght) - NUMMAX(tlft, blft);
        Fixed minlen = NUMMIN(trght - tlft, brght - blft);
        if (minlen == overlaplen)
            dist = dy;
        else
            dist = CalcOverlapDist(dy, overlaplen, minlen);
    } else { /* no overlap; take closer ends */
        Fixed dx;
        Fixed ldst = abs(tlft - brght);
        Fixed rdst = abs(trght - blft);
        dx = NUMMIN(ldst, rdst);
        dist = GapDist(dx);
        /* extra penalty for nonoverlap changed from 7/5 to 12/5 for
         * Perpetua/Regular/ n, r ,m and other lowercase serifs; undid change
         * for Berthold/AkzidenzGrotesk 9/16/91; this did not make Perpetua any
         * worse. */
        dist += (7 * dy) / 5;
        DEBUG_ROUND(dist) /* DEBUG 8 BIT */
        if (dx > dy)
            dist *= dx / dy;
    }
    mndist = FixTwoMul(gMinDist);
    dist = NUMMAX(dist, mndist);
    if (gNumHStems > 0) {
        int i;
        Fixed w = abs(dy);
        for (i = 0; i < gNumHStems; i++)
            if (w == gHStems[i]) {
                *pspc += FixOne;
                break;
            }
    }
    AdjustVal(pv, brght - blft, trght - tlft, dist, dy, true);
}

static void
HStemMiss(HintSeg* botSeg, HintSeg* topSeg)
{
    Fixed brght, blft, bloc, tloc, trght, tlft;
    Fixed mndist, dist, dy;
    Fixed b, t, minDiff, minW, w;
    int i;
    if (gNumHStems == 0)
        return;
    brght = botSeg->sMax;
    blft = botSeg->sMin;
    trght = topSeg->sMax;
    tlft = topSeg->sMin;
    bloc = botSeg->sLoc;
    tloc = topSeg->sLoc;
    dy = abs(bloc - tloc);
    if (dy < gMinDist)
        return;
    /* left is always < right */
    if ((tlft <= brght) && (trght >= blft)) { /* overlap */
        Fixed overlaplen = NUMMIN(trght, brght) - NUMMAX(tlft, blft);
        Fixed minlen = NUMMIN(trght - tlft, brght - blft);
        if (minlen == overlaplen)
            dist = dy;
        else
            dist = CalcOverlapDist(dy, overlaplen, minlen);
    } else
        return;
    mndist = FixTwoMul(gMinDist);
    if (dist < mndist)
        return;
    minDiff = FixInt(1000);
    minW = 0;
    b = -bloc;
    t = -tloc;
    w = t - b;
    /* don't check ghost bands for near misses */
    if (((w = t - b) == botGhst) || (w == topGhst))
        return;
    w = abs(w);
    for (i = 0; i < gNumHStems; i++) {
        Fixed sw = gHStems[i];
        Fixed diff = abs(sw - w);
        if (diff == 0)
            return;
        if (diff < minDiff) {
            minDiff = diff;
            minW = sw;
        }
    }
    if (minDiff > FixInt(2))
        return;
    ReportStemNearMiss(false, w, minW, b, t,
                       (botSeg->sType == sCURVE) || (topSeg->sType == sCURVE));
}

static void
EvalVPair(HintSeg* leftSeg, HintSeg* rightSeg, Fixed* pspc, Fixed* pv)
{
    Fixed ltop, lbot, lloc, rloc, rtop, rbot;
    Fixed mndist, dx, dist;
    Fixed bonus, lbonus, rbonus;
    *pspc = 0;
    ltop = leftSeg->sMax;
    lbot = leftSeg->sMin;
    rtop = rightSeg->sMax;
    rbot = rightSeg->sMin;
    lloc = leftSeg->sLoc;
    rloc = rightSeg->sLoc;
    dx = abs(lloc - rloc);
    if (dx < gMinDist) {
        *pv = 0;
        return;
    }
    /* top is always > bot, independent of YgoesUp */
    if ((ltop >= rbot) && (lbot <= rtop)) { /* overlap */
        Fixed overlaplen = NUMMIN(ltop, rtop) - NUMMAX(lbot, rbot);
        Fixed minlen = NUMMIN(ltop - lbot, rtop - rbot);
        if (minlen == overlaplen)
            dist = dx;
        else
            dist = CalcOverlapDist(dx, overlaplen, minlen);
    } else { /* no overlap; take closer ends */
        Fixed tdst = abs(ltop - rbot);
        Fixed bdst = abs(lbot - rtop);
        Fixed dy = NUMMIN(tdst, bdst);
        dist = (7 * dx) / 5 + GapDist(dy); /* extra penalty for nonoverlap */
        DEBUG_ROUND(dist)                  /* DEBUG 8 BIT */
        if (dy > dx)
            dist *= dy / dx;
    }
    mndist = FixTwoMul(gMinDist);
    dist = NUMMAX(dist, mndist);
    lbonus = leftSeg->sBonus;
    rbonus = rightSeg->sBonus;
    bonus = NUMMIN(lbonus, rbonus);
    *pspc = (bonus > 0) ? FixInt(2) : 0; /* this is for sol-eol characters */
    if (gNumVStems > 0) {
        int i;
        Fixed w = abs(dx);
        for (i = 0; i < gNumVStems; i++)
            if (w == gVStems[i]) {
                *pspc = *pspc + FixOne;
                break;
            }
    }
    AdjustVal(pv, ltop - lbot, rtop - rbot, dist, dx, false);
}

static void
VStemMiss(HintSeg* leftSeg, HintSeg* rightSeg)
{
    Fixed ltop, lbot, lloc, rloc, rtop, rbot;
    Fixed dx, l, r, minDiff, minW, w;
    int i;
    if (gNumVStems == 0)
        return;
    ltop = leftSeg->sMax;
    lbot = leftSeg->sMin;
    rtop = rightSeg->sMax;
    rbot = rightSeg->sMin;
    lloc = leftSeg->sLoc;
    rloc = rightSeg->sLoc;
    dx = abs(lloc - rloc);
    if (dx < gMinDist)
        return;
    /* top is always > bot, independent of YgoesUp */
    if ((ltop < rbot) || (lbot > rtop))  /* does not overlap */
        return;

    l = lloc;
    r = rloc;
    w = abs(r - l);
    minDiff = FixInt(1000);
    minW = 0;
    for (i = 0; i < gNumVStems; i++) {
        Fixed sw = gVStems[i];
        Fixed diff = abs(sw - w);
        if (diff < minDiff) {
            minDiff = diff;
            minW = sw;
        }
        if (minDiff == 0)
            return;
    }
    if (minDiff > FixInt(2))
        return;
    ReportStemNearMiss(true, w, minW, l, r,
                       (leftSeg->sType == sCURVE) ||
                         (rightSeg->sType == sCURVE));
}

static void
InsertVValue(Fixed lft, Fixed rght, Fixed val, Fixed spc, HintSeg* lSeg,
             HintSeg* rSeg)
{
    HintVal *item, *vlist, *vprev;
    item = (HintVal*)Alloc(sizeof(HintVal));
    item->vVal = val;
    item->initVal = val;
    item->vLoc1 = lft;
    item->vLoc2 = rght;
    item->vSpc = spc;
    item->vSeg1 = lSeg;
    item->vSeg2 = rSeg;
    item->vGhst = false;
    vlist = gValList;
    vprev = NULL;
    while (vlist != NULL) {
        if (vlist->vLoc1 >= lft)
            break;
        vprev = vlist;
        vlist = vlist->vNxt;
    }
    while (vlist != NULL && vlist->vLoc1 == lft) {
        if (vlist->vLoc2 >= rght)
            break;
        vprev = vlist;
        vlist = vlist->vNxt;
    }
    if (vprev == NULL)
        gValList = item;
    else
        vprev->vNxt = item;
    item->vNxt = vlist;
    ReportAddVVal(item);
}

#define LePruneValue(val) ((val) < FixOne && ((val) << 10) <= gPruneValue)

static void
AddVValue(Fixed lft, Fixed rght, Fixed val, Fixed spc, HintSeg* lSeg,
          HintSeg* rSeg)
{
    if (val == 0)
        return;
    if (LePruneValue(val) && spc <= 0)
        return;
    if (lSeg != NULL && lSeg->sType == sBEND && rSeg != NULL &&
        rSeg->sType == sBEND)
        return;
    if (val <= gPruneD && spc <= 0 && lSeg != NULL && rSeg != NULL) {
        if (lSeg->sType == sBEND || rSeg->sType == sBEND ||
            !CheckBBoxes(lSeg->sElt, rSeg->sElt))
            return;
    }
    if (rSeg == NULL)
        return;
    InsertVValue(lft, rght, val, spc, lSeg, rSeg);
}

static void
InsertHValue(Fixed bot, Fixed top, Fixed val, Fixed spc, HintSeg* bSeg,
             HintSeg* tSeg, bool ghst)
{
    HintVal *item, *vlist, *vprev, *vl;
    vlist = gValList;
    vprev = NULL;
    while (vlist != NULL) {
        if (vlist->vLoc2 >= top)
            break;
        vprev = vlist;
        vlist = vlist->vNxt;
    }
    while (vlist != NULL && vlist->vLoc2 == top) {
        if (vlist->vLoc1 >= bot)
            break;
        vprev = vlist;
        vlist = vlist->vNxt;
    }
    /* prune ghost pair that is same as non ghost pair for same segment
 only if val for ghost is less than an existing val with same
 top and bottom segment (vl) */
    vl = vlist;
    while (ghst && vl != NULL && vl->vLoc2 == top && vl->vLoc1 == bot) {
        if (!vl->vGhst && (vl->vSeg1 == bSeg || vl->vSeg2 == tSeg) &&
            vl->vVal > val)
            return;
        vl = vl->vNxt;
    }
    item = (HintVal*)Alloc(sizeof(HintVal));
    item->vVal = val;
    item->initVal = val;
    item->vSpc = spc;
    item->vLoc1 = bot;
    item->vLoc2 = top;
    item->vSeg1 = bSeg;
    item->vSeg2 = tSeg;
    item->vGhst = ghst;
    if (vprev == NULL)
        gValList = item;
    else
        vprev->vNxt = item;
    item->vNxt = vlist;
    ReportAddHVal(item);
}

static void
AddHValue(Fixed bot, Fixed top, Fixed val, Fixed spc, HintSeg* bSeg,
          HintSeg* tSeg)
{
    bool ghst;
    if (val == 0)
        return;
    if (LePruneValue(val) && spc <= 0)
        return;
    if (bSeg->sType == sBEND && tSeg->sType == sBEND)
        return;
    ghst = bSeg->sType == sGHOST || tSeg->sType == sGHOST;
    if (!ghst && val <= gPruneD && spc <= 0) {
        if (bSeg->sType == sBEND || tSeg->sType == sBEND ||
            !CheckBBoxes(bSeg->sElt, tSeg->sElt))
            return;
    }
    InsertHValue(bot, top, val, spc, bSeg, tSeg, ghst);
}

static float
mfabs(float in)
{
    if (in > 0)
        return in;
    return -in;
}

static Fixed
CombVals(Fixed v1, Fixed v2)
{
    int32_t i;
    float r1, r2;
    float x, a, xx = 0;
    acfixtopflt(v1, &r1);
    acfixtopflt(v2, &r2);
    /* home brew sqrt */
    a = r1 * r2;
    x = a;
    for (i = 0; i < 16; i++) {
        xx = ((float)0.5) * (x + a / x);
        if (i >= 8 && mfabs(xx - x) <= mfabs(xx) * 0.0000001f)
            break;
        x = xx;
    }
    r1 += r2 + ((float)2.0) * xx;
    if (r1 > gMaxVal)
        r1 = gMaxVal;
    else if (r1 > 0 && r1 < gMinVal)
        r1 = gMinVal;
    return acpflttofix(&r1);
}

static void
CombineValues(void)
{ /* works for both H and V */
    HintVal* vlist = gValList;
    while (vlist != NULL) {
        HintVal* v1 = vlist->vNxt;
        Fixed loc1 = vlist->vLoc1;
        Fixed loc2 = vlist->vLoc2;
        Fixed val = vlist->vVal;
        bool match = false;
        while (v1 != NULL && v1->vLoc1 == loc1 && v1->vLoc2 == loc2) {
            if (v1->vGhst)
                val = v1->vVal;
            else
                val = CombVals(val, v1->vVal);
            /* increase value to compensate for length squared effect */
            match = true;
            v1 = v1->vNxt;
        }
        if (match) {
            while (vlist != v1) {
                vlist->vVal = val;
                vlist = vlist->vNxt;
            }
        } else
            vlist = v1;
    }
}

void
EvalV(void)
{
    HintSeg *lList, *rList;
    Fixed lft, rght;
    Fixed val, spc;
    gValList = NULL;
    lList = leftList;
    while (lList != NULL) {
        rList = rightList;
        while (rList != NULL) {
            lft = lList->sLoc;
            rght = rList->sLoc;
            if (lft < rght) {
                EvalVPair(lList, rList, &spc, &val);
                VStemMiss(lList, rList);
                AddVValue(lft, rght, val, spc, lList, rList);
            }
            rList = rList->sNxt;
        }
        lList = lList->sNxt;
    }
    CombineValues();
}

void
EvalH(void)
{
    HintSeg *bList, *tList, *lst, *ghostSeg;
    Fixed lstLoc, tempLoc, cntr;
    Fixed val, spc;
    gValList = NULL;
    bList = botList;
    while (bList != NULL) {
        tList = topList;
        while (tList != NULL) {
            Fixed bot, top;
            bot = bList->sLoc;
            top = tList->sLoc;
            if (bot > top) {
                EvalHPair(bList, tList, &spc, &val);
                HStemMiss(bList, tList);
                AddHValue(bot, top, val, spc, bList, tList);
            }
            tList = tList->sNxt;
        }
        bList = bList->sNxt;
    }
    ghostSeg = (HintSeg*)Alloc(sizeof(HintSeg));
    ghostSeg->sType = sGHOST;
    ghostSeg->sElt = NULL;
    if (gLenBotBands < 2 && gLenTopBands < 2)
        goto done;
    lst = botList;
    while (lst != NULL) {
        lstLoc = lst->sLoc;
        if (InBlueBand(lstLoc, gLenBotBands, gBotBands)) {
            tempLoc = lstLoc - gGhostWidth;
            ghostSeg->sLoc = tempLoc;
            cntr = (lst->sMax + lst->sMin) / 2;
            ghostSeg->sMax = cntr + gGhostLength / 2;
            ghostSeg->sMin = cntr - gGhostLength / 2;
            DEBUG_ROUND(ghostSeg->sMax) /* DEBUG 8 BIT */
            DEBUG_ROUND(ghostSeg->sMin) /* DEBUG 8 BIT */
            spc = FixInt(2);
            val = FixInt(20);
            AddHValue(lstLoc, tempLoc, val, spc, lst, ghostSeg);
        }
        lst = lst->sNxt;
    }
    lst = topList;
    while (lst != NULL) {
        lstLoc = lst->sLoc;
        if (InBlueBand(lstLoc, gLenTopBands, gTopBands)) {
            tempLoc = lstLoc + gGhostWidth;
            ghostSeg->sLoc = tempLoc;
            cntr = (lst->sMin + lst->sMax) / 2;
            ghostSeg->sMax = cntr + gGhostLength / 2;
            ghostSeg->sMin = cntr - gGhostLength / 2;
            spc = FixInt(2);
            val = FixInt(20);
            AddHValue(tempLoc, lstLoc, val, spc, ghostSeg, lst);
        }
        lst = lst->sNxt;
    }
done:
    CombineValues();
}
