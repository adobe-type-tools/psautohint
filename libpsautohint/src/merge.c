/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"

#define CLSMRG (PSDist(20))

/* true iff you can go from e1 to e2 without going out of band loc1..loc2
 * if vert is true, then band is vert (test x values)
 * else band is horizontal (test y values)
 * band is expanded by CLSMRG in each direction
 */
static bool
CloseElements(PathElt* e1, PathElt* e2, Fixed loc1, Fixed loc2, bool vert)
{
    Fixed tmp;
    Fixed x, y;
    PathElt* e;
    if (e1 == e2)
        return true;
    if (loc1 < loc2) {
        if ((loc2 - loc1) > 5 * CLSMRG)
            return false;
        loc1 -= CLSMRG;
        loc2 += CLSMRG;
    } else {
        if ((loc1 - loc2) > 5 * CLSMRG)
            return false;
        tmp = loc1;
        loc1 = loc2 - CLSMRG;
        loc2 = tmp + CLSMRG;
    }

    e = e1;
    while (true) {
        if (e == e2)
            return true;
        GetEndPoint(e, &x, &y);
        tmp = vert ? x : y;
        if (tmp > loc2 || tmp < loc1)
            return false;
        if (e->type == CLOSEPATH)
            e = GetDest(e);
        else
            e = e->next;
        if (e == e1)
            return false;
    }
}

bool
CloseSegs(HintSeg* s1, HintSeg* s2, bool vert)
{
    /* true if the elements for these segs are "close" in the path */
    PathElt *e1, *e2;
    Fixed loc1, loc2;
    if ((s1 == NULL) || (s2 == NULL))
        return false;
    if (s1 == s2)
        return true;
    e1 = s1->sElt;
    e2 = s2->sElt;
    if (e1 == NULL || e2 == NULL)
        return true;
    loc1 = s1->sLoc;
    loc2 = s2->sLoc;
    return (CloseElements(e1, e2, loc1, loc2, vert) ||
            CloseElements(e2, e1, loc2, loc1, vert))
             ? true
             : false;
}

void
DoPrune(void)
{
    /* Step through valList to the first item which is not pruned; set
    that to be the head of the list. Then remove from the list
    any subsequent element for which 'pruned' is true.
    */
    HintVal *vL = gValList, *vPrv;
    while (vL != NULL && vL->pruned)
        vL = vL->vNxt;
    gValList = vL;
    if (vL == NULL)
        return;
    vPrv = vL;
    vL = vL->vNxt;
    while (vL != NULL) {
        if (vL->pruned)
            vPrv->vNxt = vL = vL->vNxt;
        else {
            vPrv = vL;
            vL = vL->vNxt;
        }
    }
}

static HintVal*
PruneOne(HintVal* sLst, bool hFlg, HintVal* sL, int32_t i)
{
    /* Simply set the 'pruned' field to True for sLst. */
    if (hFlg)
        ReportPruneHVal(sLst, sL, i);
    else
        ReportPruneVVal(sLst, sL, i);
    sLst->pruned = true;
    return sLst->vNxt;
}

#define PRNDIST (PSDist(10))
#define PRNFCTR (3)
#define MUCHFCTR (50)
#define VERYMUCHFCTR (100)

static bool
PruneLt(Fixed val, Fixed v)
{
    if (v < (FIXED_MAX / 10) && val < (FIXED_MAX / PRNFCTR))
        return (val * PRNFCTR) < (v * 10);
    return (val / 10) < (v / PRNFCTR);
}

static bool
PruneLe(Fixed val, Fixed v)
{
    if (val < (FIXED_MAX / PRNFCTR))
        return v <= (val * PRNFCTR);
    return (v / PRNFCTR) <= val;
}

static bool
PruneGt(Fixed val, Fixed v)
{
    if (val < (FIXED_MAX / PRNFCTR))
        return v > (val * PRNFCTR);
    return (v / PRNFCTR) > val;
}

static bool
PruneMuchGt(Fixed val, Fixed v)
{
    if (val < (FIXED_MAX / MUCHFCTR))
        return v > (val * MUCHFCTR);
    return (v / MUCHFCTR) > val;
}

static bool
PruneVeryMuchGt(Fixed val, Fixed v)
{
    if (val < (FIXED_MAX / VERYMUCHFCTR))
        return v > (val * VERYMUCHFCTR);
    return (v / VERYMUCHFCTR) > val;
}

/* The changes made here and in PruneHVals are to fix a bug in
 MinisterLight/E where the top left point was not getting hinted. */
void
PruneVVals(void)
{
    HintVal *sLst, *sL;
    HintSeg *seg1, *seg2, *sg1, *sg2;
    Fixed lft, rht, l, r, prndist;
    Fixed val, v;
    bool flg, otherLft, otherRht;
    sLst = gValList;
    prndist = PRNDIST;
    while (sLst != NULL) {
        flg = true;
        otherLft = otherRht = false;
        val = sLst->vVal;
        lft = sLst->vLoc1;
        rht = sLst->vLoc2;
        seg1 = sLst->vSeg1;
        seg2 = sLst->vSeg2;
        sL = gValList;
        while (sL != NULL) {
            v = sL->vVal;
            sg1 = sL->vSeg1;
            sg2 = sL->vSeg2;
            l = sL->vLoc1;
            r = sL->vLoc2;
            if ((l == lft && r == rht) || PruneLe(val, v))
                goto NxtSL;
            if (rht + prndist >= r && lft - prndist <= l &&
                (val < FixInt(100) && PruneMuchGt(val, v)
                   ? (CloseSegs(seg1, sg1, true) || CloseSegs(seg2, sg2, true))
                   : (CloseSegs(seg1, sg1, true) &&
                      CloseSegs(seg2, sg2, true)))) {
                sLst = PruneOne(sLst, false, sL, 1);
                flg = false;
                break;
            }
            if (seg1 != NULL && seg2 != NULL) {
                if (abs(l - lft) < FixOne) {
                    if (!otherLft && PruneLt(val, v) &&
                        abs(l - r) < abs(lft - rht) &&
                        CloseSegs(seg1, sg1, true))
                        otherLft = true;
                    if (seg2->sType == sBEND && CloseSegs(seg1, sg1, true)) {
                        sLst = PruneOne(sLst, false, sL, 2);
                        flg = false;
                        break;
                    }
                }
                if (abs(r - rht) < FixOne) {
                    if (!otherRht && PruneLt(val, v) &&
                        abs(l - r) < abs(lft - rht) &&
                        CloseSegs(seg2, sg2, true))
                        otherRht = true;
                    if (seg1->sType == sBEND && CloseSegs(seg2, sg2, true)) {
                        sLst = PruneOne(sLst, false, sL, 3);
                        flg = false;
                        break;
                    }
                }
                if (otherLft && otherRht) {
                    sLst = PruneOne(sLst, false, sL, 4);
                    flg = false;
                    break;
                }
            }
        NxtSL:
            sL = sL->vNxt;
        }
        if (flg) {
            sLst = sLst->vNxt;
        }
    }
    DoPrune();
}

#define Fix16 (FixOne << 4)
void
PruneHVals(void)
{
    HintVal *sLst, *sL;
    HintSeg *seg1, *seg2, *sg1, *sg2;
    Fixed bot, top, t, b;
    Fixed val, v, prndist;
    bool flg, otherTop, otherBot, topInBlue, botInBlue, ghst;
    sLst = gValList;
    prndist = PRNDIST;
    while (sLst != NULL) {
        flg = true;
        otherTop = otherBot = false;
        seg1 = sLst->vSeg1;
        seg2 = sLst->vSeg2; /* seg1 is bottom, seg2 is top */
        ghst = sLst->vGhst;
        val = sLst->vVal;
        bot = sLst->vLoc1;
        top = sLst->vLoc2;
        topInBlue = InBlueBand(top, gLenTopBands, gTopBands);
        botInBlue = InBlueBand(bot, gLenBotBands, gBotBands);
        sL = gValList;
        while (sL != NULL) {
            if ((sL->pruned) && (gDoAligns || !gDoStems))
                goto NxtSL;

            sg1 = sL->vSeg1;
            sg2 = sL->vSeg2; /* sg1 is b, sg2 is t */
            v = sL->vVal;
            if (!ghst && sL->vGhst && !PruneVeryMuchGt(val, v))
                goto NxtSL; /* Do not bother checking if we should prune, if
                               slSt is not ghost hint, sL is ghost hint,
                                         and not (sL->vVal is  more than 50*
                               bigger than sLst->vVal.
                                         Basically, we prefer non-ghost hints
                               over ghost unless vVal is really low. */
            b = sL->vLoc1;
            t = sL->vLoc2;
            if (t == top && b == bot)
                goto NxtSL; /* Don't compare two valList elements that have the
                               same top and bot. */

            if (/* Prune sLst if the following are all true */
                PruneGt(val, v) && /*  v is more than 3* val */
                (top - prndist <= t &&
                 bot + prndist >=
                   b) && /* The sL hint is within the sLst hint */

                (val < FixInt(100) && PruneMuchGt(val, v)
                   ? (CloseSegs(seg1, sg1, false) ||
                      CloseSegs(seg2, sg2, false))
                   : (CloseSegs(seg1, sg1, false) &&
                      CloseSegs(seg2, sg2, false))) && /* val is less than 100,
                                                          and the segments are
                                                          close to each other.*/

                (val < Fix16 ||
                 /* needs to be greater than FixOne << 3 for
                  HelveticaNeue 95 Black G has val == 2.125
                  Poetica/ItalicOne H has val == .66  */
                 ((!topInBlue || top == t) &&
                  (!botInBlue || bot == b))) /* either val is small ( < Fixed
                                                16) or, for both bot and top,
                                                the value is the same as SL,
                                                and not in a blue zone. */

            ) {
                sLst = PruneOne(sLst, true, sL, 5);
                flg = false;
                break;
            }

            if (seg1 == NULL || seg2 == NULL)
                goto NxtSL; /* If the sLst is aghost hint, skip  */

            if (abs(b - bot) < FixOne) {
                /* If the bottoms of the stems are within 1 unit */

                if (PruneGt(val, v) && /* If v is more than 3* val) */
                    !topInBlue && seg2->sType == sBEND &&
                    CloseSegs(seg1, sg1, false) /* and the tops are close */
                ) {
                    sLst = PruneOne(sLst, true, sL, 6);
                    flg = false;
                    break;
                }

                if (!otherBot && PruneLt(val, v) &&
                    abs(t - b) < abs(top - bot)) {
                    if (CloseSegs(seg1, sg1, false))
                        otherBot = true;
                }
            }

            if (abs(t - top) < FixOne) {
                /* If the tops of the stems are within 1 unit */
                if (PruneGt(val, v) && /* If v is more than 3* val) */
                    !botInBlue && seg2->sType == sBEND &&
                    CloseSegs(seg1, sg1, false)) /* and the tops are close */
                {
                    sLst = PruneOne(sLst, true, sL, 7);
                    flg = false;
                    break;
                }

                if (!otherTop && PruneLt(val, v) &&
                    abs(t - b) < abs(top - bot)) {
                    if (CloseSegs(seg2, sg2, false))
                        otherTop = true;
                }
            }

            if (otherBot && otherTop) {
                /* if v less than  val by a factor of 3, and the sl stem width
                 is less than the sLst stem width,
                 and the tops and bottoms are close */
                sLst = PruneOne(sLst, true, sL, 8);
                flg = false;
                break;
            }
        NxtSL:
            sL = sL->vNxt;
        }
        if (flg) {
            sLst = sLst->vNxt;
        }
    }
    DoPrune();
}

static void
FindBestVals(HintVal* vL)
{
    Fixed bV, bS;
    Fixed t, b;
    HintVal *vL2, *vPrv, *bstV;
    for (; vL != NULL; vL = vL->vNxt) {
        if (vL->vBst != NULL)
            continue; /* already assigned */
        bV = vL->vVal;
        bS = vL->vSpc;
        bstV = vL;
        b = vL->vLoc1;
        t = vL->vLoc2;
        vL2 = vL->vNxt;
        vPrv = vL;
        for (; vL2 != NULL; vL2 = vL2->vNxt) {
            if (vL2->vBst != NULL || vL2->vLoc1 != b || vL2->vLoc2 != t)
                continue;
            if ((vL2->vSpc == bS && vL2->vVal > bV) || (vL2->vSpc > bS)) {
                bS = vL2->vSpc;
                bV = vL2->vVal;
                bstV = vL2;
            }
            vL2->vBst = vPrv;
            vPrv = vL2;
        }
        while (vPrv != NULL) {
            vL2 = vPrv->vBst;
            vPrv->vBst = bstV;
            vPrv = vL2;
        }
    }
}

/* The following changes were made to fix a problem in Ryumin-Light and
 possibly other fonts as well.  The old version causes bogus hinting
 and extra newhints. */
static void
ReplaceVals(Fixed oldB, Fixed oldT, Fixed newB, Fixed newT, HintVal* newBst,
            bool vert)
{
    HintVal* vL;
    for (vL = gValList; vL != NULL; vL = vL->vNxt) {
        if (vL->vLoc1 != oldB || vL->vLoc2 != oldT || vL->merge)
            continue;
        if (vert)
            ReportMergeVVal(oldB, oldT, newB, newT, vL->vVal, vL->vSpc,
                            newBst->vVal, newBst->vSpc);
        else
            ReportMergeHVal(oldB, oldT, newB, newT, vL->vVal, vL->vSpc,
                            newBst->vVal, newBst->vSpc);
        vL->vLoc1 = newB;
        vL->vLoc2 = newT;
        vL->vVal = newBst->vVal;
        vL->vSpc = newBst->vSpc;
        vL->vBst = newBst;
        vL->merge = true;
    }
}

void
MergeVals(bool vert)
{
    HintVal *vLst, *vL;
    HintVal *bstV, *bV;
    HintSeg *seg1, *seg2, *sg1, *sg2;
    Fixed bot, top, b, t;
    Fixed val, v, spc, s;
    bool ghst;
    FindBestVals(gValList);
    /* We want to get rid of wider hstems in favor or overlapping smaller hstems
     * only if we are NOT reporting all possible alignment zones. */
    if (gAddStemExtremesCB == NULL)
        return;

    for (vL = gValList; vL != NULL; vL = vL->vNxt)
        vL->merge = false;
    while (true) {
        /* pick best from valList with merge field still set to false */
        vLst = gValList;
        vL = NULL;
        while (vLst != NULL) {
            if (vLst->merge) {
                /* do nothing */
            } else if (vL == NULL ||
                       CompareValues(vLst->vBst, vL->vBst, SFACTOR, 0))
                vL = vLst;
            vLst = vLst->vNxt;
        }
        if (vL == NULL)
            break;
        vL->merge = true;
        ghst = vL->vGhst;
        b = vL->vLoc1;
        t = vL->vLoc2;
        sg1 = vL->vSeg1; /* left or bottom */
        sg2 = vL->vSeg2; /* right or top */
        vLst = gValList;
        bV = vL->vBst;
        v = bV->vVal;
        s = bV->vSpc;
        while (vLst != NULL) { /* consider replacing vLst by vL */
            if (vLst->merge || ghst != vLst->vGhst)
                goto NxtVL;
            bot = vLst->vLoc1;
            top = vLst->vLoc2;
            if (bot == b && top == t)
                goto NxtVL;
            bstV = vLst->vBst;
            val = bstV->vVal;
            spc = bstV->vSpc;
            if ((top == t && CloseSegs(sg2, vLst->vSeg2, vert) &&
                 (vert || (!InBlueBand(t, gLenTopBands, gTopBands) &&
                           !InBlueBand(bot, gLenBotBands, gBotBands) &&
                           !InBlueBand(b, gLenBotBands, gBotBands)))) ||
                (bot == b && CloseSegs(sg1, vLst->vSeg1, vert) &&
                 (vert || (!InBlueBand(b, gLenBotBands, gBotBands) &&
                           !InBlueBand(t, gLenTopBands, gTopBands) &&
                           !InBlueBand(top, gLenTopBands, gTopBands)))) ||
                (abs(top - t) <= gMaxMerge && abs(bot - b) <= gMaxMerge &&
                 (vert ||
                  (t == top || !InBlueBand(top, gLenTopBands, gTopBands))) &&
                 (vert ||
                  (b == bot || !InBlueBand(bot, gLenBotBands, gBotBands))))) {
                if (s == spc && val == v && !vert) {
                    if (InBlueBand(t, gLenTopBands, gTopBands)) {
                        if (t < top)
                            goto replace;
                    } else if (InBlueBand(b, gLenBotBands, gBotBands)) {
                        if (b > bot)
                            goto replace;
                    }
                } else
                    goto replace;
            } else if (s == spc && sg1 != NULL && sg2 != NULL) {
                seg1 = vLst->vSeg1;
                seg2 = vLst->vSeg2;
                if (seg1 != NULL && seg2 != NULL) {
                    if (abs(bot - b) <= FixOne &&
                        abs(top - t) <= gMaxBendMerge) {
                        if (seg2->sType == sBEND &&
                            (vert || !InBlueBand(top, gLenTopBands, gTopBands)))
                            goto replace;
                    } else if (abs(top - t) <= FixOne &&
                               abs(bot - b) <= gMaxBendMerge) {
                        if (v > val && seg1->sType == sBEND &&
                            (vert || !InBlueBand(bot, gLenBotBands, gBotBands)))
                            goto replace;
                    }
                }
            }
            goto NxtVL;
        replace:
            ReplaceVals(bot, top, b, t, bV, vert);
        NxtVL:
            vLst = vLst->vNxt;
        }
        vL = vL->vNxt;
    }
}
