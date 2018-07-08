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

static PClrVal Vrejects, Hrejects;

void
InitPick(int32_t reason)
{
    switch (reason) {
        case STARTUP:
        case RESTART:
            Vrejects = Hrejects = NULL;
    }
}

#define LtPruneB(val) ((val) < FixOne && ((val) << 10) < gPruneB)

static bool
ConsiderPicking(Fixed bestSpc, Fixed bestVal, PClrVal colorList,
                Fixed prevBestVal)
{
    if (bestSpc > 0)
        return true;
    if (colorList == NULL)
        return bestVal >= gPruneD;
    if (bestVal > gPruneA)
        return true;
    if (LtPruneB(bestVal))
        return false;
    return (bestVal < FixedPosInf / gPruneC)
             ? (prevBestVal <= bestVal * gPruneC)
             : (prevBestVal / gPruneC <= bestVal);
}

void
PickVVals(PClrVal valList)
{
    PClrVal colorList, rejectList, vlist, nxt;
    Fixed bestVal = 0, prevBestVal;
    colorList = rejectList = NULL;
    prevBestVal = 0;
    while (true) {
        PClrVal prev, bestPrev, best;
        Fixed lft, rght;
        vlist = valList;
        prev = bestPrev = best = NULL;
        while (vlist != NULL) {
            if ((best == NULL || CompareValues(vlist, best, spcBonus, 0)) &&
                ConsiderPicking(vlist->vSpc, vlist->vVal, colorList,
                                prevBestVal)) {
                best = vlist;
                bestPrev = prev;
                bestVal = vlist->vVal;
            }
            prev = vlist;
            vlist = vlist->vNxt;
        }
        if (best == NULL)
            break; /* no more */
        if (bestPrev == NULL)
            valList = best->vNxt;
        else
            bestPrev->vNxt = best->vNxt;
        /* have now removed best from valList */
        best->vNxt = colorList; /* add best to front of list */
        colorList = best;
        prevBestVal = bestVal;
        lft = best->vLoc1 - gBandMargin;
        rght = best->vLoc2 + gBandMargin;
        /* remove segments from valList that overlap lft..rght */
        vlist = valList;
        prev = NULL;
        while (vlist != NULL) {
            Fixed vlft = vlist->vLoc1;
            Fixed vrght = vlist->vLoc2;
            if ((vlft <= rght) && (vrght >= lft)) {
                nxt = vlist->vNxt;
                vlist->vNxt = rejectList;
                rejectList = vlist;
                vlist = nxt;
                if (prev == NULL)
                    valList = vlist;
                else
                    prev->vNxt = vlist;
            } else {
                prev = vlist;
                vlist = vlist->vNxt;
            }
        }
    }
    vlist = valList; /* move rest of valList to rejectList */
    while (vlist != NULL) {
        nxt = vlist->vNxt;
        vlist->vNxt = rejectList;
        rejectList = vlist;
        vlist = nxt;
    }
    if (colorList == NULL)
        ClrVBnds();
    gVColoring = colorList;
    Vrejects = rejectList;
}

static bool
InSerifBand(Fixed y0, Fixed y1, int32_t n, Fixed* p)
{
    int32_t i;
    if (n <= 0)
        return false;
    y0 = itfmy(y0);
    y1 = itfmy(y1);
    if (y0 > y1) {
        Fixed tmp = y1;
        y1 = y0;
        y0 = tmp;
    }
    for (i = 0; i < n; i += 2)
        if (p[i] <= y0 && p[i + 1] >= y1)
            return true;
    return false;
}

static bool
ConsiderValForSeg(PClrVal val, PClrSeg seg, Fixed loc, int32_t nb, Fixed* b,
                  int32_t ns, Fixed* s, bool primary)
{
    if (primary && val->vSpc > 0.0)
        return true;
    if (InBlueBand(loc, nb, b))
        return true;
    if (val->vSpc <= 0.0 && InSerifBand(seg->sMax, seg->sMin, ns, s))
        return false;
    if (LtPruneB(val->vVal))
        return false;
    return true;
}

static PClrVal
FndBstVal(PClrSeg seg, bool seg1Flg, PClrVal cList, PClrVal rList, int32_t nb,
          Fixed* b, int32_t ns, Fixed* s, bool locFlg, bool hFlg)
{
    Fixed loc, vloc;
    PClrVal best, vList;
    PClrSeg vseg;
    best = NULL;
    loc = seg->sLoc;
    vList = cList;
    while (true) {
        PClrVal initLst = vList;
        while (vList != NULL) {
            if (seg1Flg) {
                vseg = vList->vSeg1;
                vloc = vList->vLoc1;
            } else {
                vseg = vList->vSeg2;
                vloc = vList->vLoc2;
            }
            if (abs(loc - vloc) <= gMaxMerge &&
                (locFlg ? !vList->vGhst
                        : (vseg == seg || CloseSegs(seg, vseg, !hFlg))) &&
                (best == NULL ||
                 (vList->vVal == best->vVal && vList->vSpc == best->vSpc &&
                  vList->initVal > best->initVal) ||
                 CompareValues(vList, best, spcBonus, 3)) &&
                /* last arg is "ghostshift" that penalizes ghost values */
                /* ghost values are set to 20 */
                /* so ghostshift of 3 means prefer nonghost if its
                 value is > (20 >> 3) */
                ConsiderValForSeg(vList, seg, loc, nb, b, ns, s, true))
                best = vList;
            vList = vList->vNxt;
        }
        if (initLst == rList)
            break;
        vList = rList;
    }
    ReportFndBstVal(seg, best, hFlg);
    return best;
}

#define FixSixteenth (0x10)
static PClrVal
FindBestValForSeg(PClrSeg seg, bool seg1Flg, PClrVal cList, PClrVal rList,
                  int32_t nb, Fixed* b, int32_t ns, Fixed* s, bool hFlg)
{
    PClrVal best, nonghst, ghst = NULL;
    best = FndBstVal(seg, seg1Flg, cList, rList, nb, b, ns, s, false, hFlg);
    if (best != NULL && best->vGhst) {
        nonghst =
          FndBstVal(seg, seg1Flg, cList, rList, nb, b, ns, s, true, hFlg);
        /* If nonghst hints are "better" use it instead of ghost band. */
        if (nonghst != NULL && nonghst->vVal >= FixInt(2)) {
            /* threshold must be greater than 1.004 for ITC Garamond Ultra "q"
             */
            ghst = best;
            best = nonghst;
        }
    }
    if (best != NULL) {
        if (best->vVal < FixSixteenth &&
            (ghst == NULL || ghst->vVal < FixSixteenth))
            best = NULL;
        /* threshold must be > .035 for Monotype/Plantin/Bold Thorn
         and < .08 for Bookman2/Italic asterisk */
        else
            best->pruned = false;
    }
    return best;
}

static bool
MembValList(PClrVal val, PClrVal vList)
{
    while (vList != NULL) {
        if (val == vList)
            return true;
        vList = vList->vNxt;
    }
    return false;
}

static PClrVal
PrevVal(PClrVal val, PClrVal vList)
{
    PClrVal prev;
    if (val == vList)
        return NULL;
    prev = vList;
    while (true) {
        vList = vList->vNxt;
        if (vList == NULL) {
            LogMsg(LOGERROR, NONFATALERROR, "Malformed value list in %s.\n",
                   gGlyphName);
        }

        if (vList == val)
            return prev;
        prev = vList;
    }
}

static void
FindRealVal(PClrVal vlist, Fixed top, Fixed bot, PClrSeg* pseg1, PClrSeg* pseg2)
{
    while (vlist != NULL) {
        if (vlist->vLoc2 == top && vlist->vLoc1 == bot && !vlist->vGhst) {
            *pseg1 = vlist->vSeg1;
            *pseg2 = vlist->vSeg2;
            return;
        }
        vlist = vlist->vNxt;
    }
}

void
PickHVals(PClrVal valList)
{
    PClrVal vlist, colorList, rejectList, bestPrev, prev, best, nxt;
    Fixed bestVal, prevBestVal;
    Fixed bot, top, vtop, vbot;
    PClrVal newBst;
    PClrSeg seg1, seg2;
    colorList = rejectList = NULL;
    prevBestVal = 0;
    while (true) {
        vlist = valList;
        prev = bestPrev = best = NULL;
        while (vlist != NULL) {
            if ((best == NULL || CompareValues(vlist, best, spcBonus, 0)) &&
                ConsiderPicking(vlist->vSpc, vlist->vVal, colorList,
                                prevBestVal)) {
                best = vlist;
                bestPrev = prev;
                bestVal = vlist->vVal;
            }
            prev = vlist;
            vlist = vlist->vNxt;
        }
        if (best != NULL) {
            seg1 = best->vSeg1;
            seg2 = best->vSeg2;
            if (best->vGhst) { /* find float segments at same loc as best */
                FindRealVal(valList, best->vLoc2, best->vLoc1, &seg1, &seg2);
            }
            if (seg1->sType == sGHOST) {
                /*newBst = FindBestValForSeg(seg2, false, valList,
                 (PClrVal)NULL, 0, (Fixed *)NIL, 0, (Fixed *)NIL, true);*/
                newBst = seg2->sLnk;
                if (newBst != NULL && newBst != best &&
                    MembValList(newBst, valList)) {
                    best = newBst;
                    bestPrev = PrevVal(best, valList);
                }
            } else if (seg2->sType == sGHOST) {
                /*newBst = FindBestValForSeg(seg1, true, valList,
                 (PClrVal)NULL, 0, (Fixed *)NIL, 0, (Fixed *)NIL, true); */
                newBst = seg2->sLnk;
                if (newBst != NULL && newBst != best &&
                    MembValList(newBst, valList)) {
                    best = newBst;
                    bestPrev = PrevVal(best, valList);
                }
            }
        }
        if (best == NULL)
            goto noMore;
        prevBestVal = bestVal;
        if (bestPrev == NULL)
            valList = best->vNxt;
        else
            bestPrev->vNxt = best->vNxt;
        /* have now removed best from valList */
        best->vNxt = colorList;
        colorList = best; /* add best to front of list */
        bot = best->vLoc1;
        top = best->vLoc2;
        /* The next if statement was added so that ghost bands are given
         0 width for doing the conflict tests for bands too close together.
         This was a problem in Minion/DisplayItalic onequarter and onehalf. */
        if (best->vGhst) { /* collapse width */
            if (best->vSeg1->sType == sGHOST)
                bot = top;
            else
                top = bot;
        }
        if (gYgoesUp) {
            bot -= gBandMargin;
            top += gBandMargin;
        } else {
            bot += gBandMargin;
            top -= gBandMargin;
        }
        /* remove segments from valList that overlap bot..top */
        vlist = valList;
        prev = NULL;
        while (vlist != NULL) {
            vbot = vlist->vLoc1;
            vtop = vlist->vLoc2;
            /* The next if statement was added so that ghost bands are given
             0 width for doing the conflict tests for bands too close together.
             */
            if (vlist->vGhst) { /* collapse width */
                if (vlist->vSeg1->sType == sGHOST)
                    vbot = vtop;
                else
                    vtop = vbot;
            }
            if ((gYgoesUp && (vbot <= top) && (vtop >= bot)) ||
                ((!gYgoesUp && (vbot >= top) && (vtop <= bot)))) {
                nxt = vlist->vNxt;
                vlist->vNxt = rejectList;
                rejectList = vlist;
                vlist = nxt;
                if (prev == NULL)
                    valList = vlist;
                else
                    prev->vNxt = vlist;
            } else {
                prev = vlist;
                vlist = vlist->vNxt;
            }
        }
    }
noMore:
    vlist = valList; /* move rest of valList to rejectList */
    while (vlist != NULL) {
        nxt = vlist->vNxt;
        vlist->vNxt = rejectList;
        rejectList = vlist;
        vlist = nxt;
    }
    if (colorList == NULL)
        ClrHBnds();
    gHColoring = colorList;
    Hrejects = rejectList;
}

static void
FindBestValForSegs(PClrSeg sList, bool seg1Flg, PClrVal cList, PClrVal rList,
                   int32_t nb, Fixed* b, int32_t ns, Fixed* s, bool hFlg)
{
    PClrVal best;
    while (sList != NULL) {
        best =
          FindBestValForSeg(sList, seg1Flg, cList, rList, nb, b, ns, s, hFlg);
        sList->sLnk = best;
        sList = sList->sNxt;
    }
}

static void
SetPruned(void)
{
    PClrVal vL = gValList;
    while (vL != NULL) {
        vL->pruned = true;
        vL = vL->vNxt;
    }
}

void
FindBestHVals(void)
{
    SetPruned();
    FindBestValForSegs(topList, false, gValList, NULL, gLenTopBands, gTopBands,
                       0, (Fixed*)NULL, true);
    FindBestValForSegs(botList, true, gValList, NULL, gLenBotBands, gBotBands,
                       0, (Fixed*)NULL, true);
    DoPrune();
}

void
FindBestVVals(void)
{
    SetPruned();
    FindBestValForSegs(leftList, true, gValList, NULL, 0, (Fixed*)NULL,
                       gNumSerifs, gSerifs, false);
    FindBestValForSegs(rightList, false, gValList, NULL, 0, (Fixed*)NULL,
                       gNumSerifs, gSerifs, false);
    DoPrune();
}
