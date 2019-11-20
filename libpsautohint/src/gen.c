/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include <math.h>

#include "ac.h"
#include "bbox.h"

static SegLnkLst *Hlnks, *Vlnks;
static int32_t cpFrom, cpTo;

void
InitGen(int32_t reason)
{
    int32_t i;
    switch (reason) {
        case STARTUP:
        case RESTART:
            for (i = 0; i < 4; i++)
                gSegLists[i] = NULL;
            Hlnks = Vlnks = NULL;
    }
}

static void
LinkSegment(PathElt* e, bool Hflg, HintSeg* seg)
{
    SegLnk* newlnk;
    SegLnkLst *newlst, *globlst;
    newlnk = (SegLnk*)Alloc(sizeof(SegLnk));
    newlnk->seg = seg;
    newlst = (SegLnkLst*)Alloc(sizeof(SegLnkLst));
    globlst = (SegLnkLst*)Alloc(sizeof(SegLnkLst));
    globlst->lnk = newlnk;
    newlst->lnk = newlnk;
    if (Hflg) {
        newlst->next = e->Hs;
        e->Hs = newlst;
        globlst->next = Hlnks;
        Hlnks = globlst;
    } else {
        newlst->next = e->Vs;
        e->Vs = newlst;
        globlst->next = Vlnks;
        Vlnks = globlst;
    }
}

static void
CopySegmentLink(PathElt* e1, PathElt* e2, bool Hflg)
{
    /* copy reference to first link from e1 to e2 */
    SegLnkLst* newlst;
    newlst = (SegLnkLst*)Alloc(sizeof(SegLnkLst));
    if (Hflg) {
        newlst->lnk = e1->Hs->lnk;
        newlst->next = e2->Hs;
        e2->Hs = newlst;
    } else {
        newlst->lnk = e1->Vs->lnk;
        newlst->next = e2->Vs;
        e2->Vs = newlst;
    }
}

static void
AddSegment(Fixed from, Fixed to, Fixed loc, int32_t lftLstNm, int32_t rghtLstNm,
           PathElt* e1, PathElt* e2, bool Hflg, int32_t typ)
{
    HintSeg *seg, *segList, *prevSeg;
    int32_t segNm;
    seg = (HintSeg*)Alloc(sizeof(HintSeg));
    seg->sLoc = loc;
    if (from > to) {
        seg->sMax = from;
        seg->sMin = to;
    } else {
        seg->sMax = to;
        seg->sMin = from;
    }
    seg->sBonus = gBonus;
    seg->sType = (int16_t)typ;
    if (e1 != NULL) {
        if (e1->type == CLOSEPATH)
            e1 = GetDest(e1);
        LinkSegment(e1, Hflg, seg);
        seg->sElt = e1;
    }
    if (e2 != NULL) {
        if (e2->type == CLOSEPATH)
            e2 = GetDest(e2);
        if (e1 != NULL)
            CopySegmentLink(e1, e2, Hflg);
        if (e1 == NULL || e2 == e1->prev)
            seg->sElt = e2;
    }
    segNm = (from > to) ? lftLstNm : rghtLstNm;
    segList = gSegLists[segNm];
    prevSeg = NULL;
    while (true) {             /* keep list in increasing order by sLoc */
        if (segList == NULL) { /* at end of list */
            if (prevSeg == NULL) {
                gSegLists[segNm] = seg;
                break;
            }
            prevSeg->sNxt = seg;
            break;
        }
        if (segList->sLoc >= loc) { /* insert before this one */
            if (prevSeg == NULL)
                gSegLists[segNm] = seg;
            else
                prevSeg->sNxt = seg;
            seg->sNxt = segList;
            break;
        }
        prevSeg = segList;
        segList = segList->sNxt;
    }
}

void
AddVSegment(Fixed from, Fixed to, Fixed loc, PathElt* p1, PathElt* p2,
            int32_t typ, int32_t i)
{
    LogMsg(LOGDEBUG, OK, "add vseg %g %g to %g %g %d", FixToDbl(loc),
           FixToDbl(-from), FixToDbl(loc), FixToDbl(-to), i);

    AddSegment(from, to, loc, 1, 0, p1, p2, false, typ);
}

void
AddHSegment(Fixed from, Fixed to, Fixed loc, PathElt* p1, PathElt* p2,
            int32_t typ, int32_t i)
{
    LogMsg(LOGDEBUG, OK, "add hseg %g %g to %g %g %d", FixToDbl(from),
           FixToDbl(-loc), FixToDbl(to), FixToDbl(-loc), i);

    AddSegment(from, to, loc, 2, 3, p1, p2, true, typ);
}

static Fixed
CPFrom(Fixed cp2, Fixed cp3)
{
    Fixed val =
      2 * (((cp3 - cp2) * cpFrom) /
           200); /*DEBUG 8 BIT: hack to get same rounding as old version */
    val += cp2;

    DEBUG_ROUND(val)
    return val; /* DEBUG 8 BIT to match results with 7 bit fractions */
}

static Fixed
CPTo(Fixed cp0, Fixed cp1)
{
    Fixed val =
      2 * (((cp1 - cp0) * cpTo) /
           200); /*DEBUG 8 BIT: hack to get same rounding as old version */
    val += cp0;
    DEBUG_ROUND(val)
    return val; /* DEBUG 8 BIT to match results with 7 bit fractions */
}

static bool
TestBend(Fixed x0, Fixed y0, Fixed x1, Fixed y1, Fixed x2, Fixed y2)
{
    /* return true if bend angle is sharp enough (135 degrees or less) */
    float dx1, dy1, dx2, dy2, dotprod, lensqprod;
    acfixtopflt(x1 - x0, &dx1);
    acfixtopflt(y1 - y0, &dy1);
    acfixtopflt(x2 - x1, &dx2);
    acfixtopflt(y2 - y1, &dy2);
    dotprod = dx1 * dx2 + dy1 * dy2;
    lensqprod = (dx1 * dx1 + dy1 * dy1) * (dx2 * dx2 + dy2 * dy2);
    return roundf((dotprod * dotprod / lensqprod) * 1000) / 1000 <= .5f;
}

#define TestTan(d1, d2) (abs(d1) > (abs(d2) * gBendTan) / 1000)
#define FRound(x) FTrunc(FRnd(x))

static bool
IsCCW(Fixed x0, Fixed y0, Fixed x1, Fixed y1, Fixed x2, Fixed y2)
{
    /* returns true if (x0,y0) -> (x1,y1) -> (x2,y2) is counter clockwise
         in glyph space */
    int32_t dx0, dy0, dx1, dy1;
    bool ccw;
    dx0 = FRound(x1 - x0);
    dy0 = -FRound(y1 - y0);
    dx1 = FRound(x2 - x1);
    dy1 = -FRound(y2 - y1);
    ccw = (dx0 * dy1) >= (dx1 * dy0);
    return ccw;
}

static void
DoHBendsNxt(Fixed x0, Fixed y0, Fixed x1, Fixed y1, PathElt* p)
{
    Fixed x2, y2, x3, y3;
    bool ysame;
    if (y0 == y1)
        return;
    NxtForBend(p, &x2, &y2, &x3, &y3);
    ysame = ProdLt0(y2 - y1, y1 - y0); /* y0 and y2 on same side of y1 */
    if (ysame ||
        (TestTan(x1 - x2, y1 - y2) &&
         (ProdLt0(x2 - x1, x1 - x0) ||
          (IsVertical(x0, y0, x1, y1) && TestBend(x0, y0, x1, y1, x2, y2))))) {
        Fixed strt, end;
        Fixed delta = FixHalfMul(gBendLength);
        bool doboth = false;
        if ((x0 <= x1 && x1 < x2) || (x0 < x1 && x1 <= x2)) {
            /* do nothing */
        } else if ((x2 < x1 && x1 <= x0) || (x2 <= x1 && x1 < x0))
            delta = -delta;
        else if (ysame) {
            bool ccw;
            bool above = y0 < y1;
            ccw = IsCCW(x0, y0, x1, y1, x2, y2);
            if (above != ccw)
                delta = -delta;
        } else
            doboth = true;
        strt = x1 - delta;
        end = x1 + delta;
        AddHSegment(strt, end, y1, p, NULL, sBEND, 0);
        if (doboth)
            AddHSegment(end, strt, y1, p, NULL, sBEND, 1);
    }
}

static void
DoHBendsPrv(Fixed x0, Fixed y0, Fixed x1, Fixed y1, PathElt* p)
{
    Fixed x2, y2;
    bool ysame;
    if (y0 == y1)
        return;
    PrvForBend(p, &x2, &y2);
    ysame = ProdLt0(y2 - y0, y0 - y1);
    if (ysame ||
        (TestTan(x0 - x2, y0 - y2) &&
         (ProdLt0(x2 - x0, x0 - x1) ||
          (IsVertical(x0, y0, x1, y1) && TestBend(x2, y2, x0, y0, x1, y1))))) {
        Fixed strt, end;
        Fixed delta = FixHalfMul(gBendLength);
        bool doboth = false;
        if ((x2 < x0 && x0 <= x1) || (x2 <= x0 && x0 < x1)) {
            /* do nothing */
        } else if ((x1 < x0 && x0 <= x2) || (x1 <= x0 && x0 < x2))
            delta = -delta;
        else if (ysame) {
            bool ccw;
            bool above = y2 < y0;
            ccw = IsCCW(x2, y2, x0, y0, x1, y1);
            if (above != ccw)
                delta = -delta;
        }
        strt = x0 - delta;
        end = x0 + delta;
        AddHSegment(strt, end, y0, p->prev, NULL, sBEND, 2);
        if (doboth)
            AddHSegment(end, strt, y0, p->prev, NULL, sBEND, 3);
    }
}

static void
DoVBendsNxt(Fixed x0, Fixed y0, Fixed x1, Fixed y1, PathElt* p)
{
    Fixed x2, y2, x3, y3;
    bool xsame;
    if (x0 == x1)
        return;
    NxtForBend(p, &x2, &y2, &x3, &y3);
    xsame = ProdLt0(x2 - x1, x1 - x0);
    if (xsame ||
        (TestTan(y1 - y2, x1 - x2) &&
         (ProdLt0(y2 - y1, y1 - y0) || (IsHorizontal(x0, y0, x1, y1) &&
                                        TestBend(x0, y0, x1, y1, x2, y2))))) {
        Fixed strt, end;
        Fixed delta = FixHalfMul(gBendLength);
        bool doboth = false;
        if ((y0 <= y1 && y1 < y2) || (y0 < y1 && y1 <= y2)) {
            /* do nothing */
        } else if ((y2 < y1 && y1 <= y0) || (y2 <= y1 && y1 < y0))
            delta = -delta;
        else if (xsame) {
            bool right = x0 > x1;
            bool ccw = IsCCW(x0, y0, x1, y1, x2, y2);
            if (right != ccw)
                delta = -delta;
            delta = -delta;
        } else
            doboth = true;
        strt = y1 - delta;
        end = y1 + delta;
        AddVSegment(strt, end, x1, p, NULL, sBEND, 0);
        if (doboth)
            AddVSegment(end, strt, x1, p, NULL, sBEND, 1);
    }
}

static void
DoVBendsPrv(Fixed x0, Fixed y0, Fixed x1, Fixed y1, PathElt* p)
{
    Fixed x2, y2;
    bool xsame;
    if (x0 == x1)
        return;
    PrvForBend(p, &x2, &y2);
    xsame = ProdLt0(x2 - x0, x0 - x1);
    if (xsame ||
        (TestTan(y0 - y2, x0 - x2) &&
         (ProdLt0(y2 - y0, y0 - y1) || (IsHorizontal(x0, y0, x1, y1) &&
                                        TestBend(x2, y2, x0, y0, x1, y1))))) {
        Fixed strt, end;
        Fixed delta = FixHalfMul(gBendLength);
        bool doboth = false;
        if ((y2 < y0 && y0 <= y1) || (y2 <= y0 && y0 < y1)) {
            /* do nothing */
        } else if ((y1 < y0 && y0 <= y2) || (y1 <= y0 && y0 < y2))
            delta = -delta;
        else if (xsame) {
            bool right = x0 > x1;
            bool ccw = IsCCW(x2, y2, x0, y0, x1, y1);
            if (right != ccw)
                delta = -delta;
            delta = -delta;
        }
        strt = y0 - delta;
        end = y0 + delta;
        AddVSegment(strt, end, x0, p->prev, NULL, sBEND, 2);
        if (doboth)
            AddVSegment(end, strt, x0, p->prev, NULL, sBEND, 3);
    }
}

static void
MergeLnkSegs(HintSeg* seg1, HintSeg* seg2, SegLnkLst* lst)
{
    /* replace lnk refs to seg1 by seg2 */
    while (lst != NULL) {
        SegLnk* lnk = lst->lnk;
        if (lnk->seg == seg1)
            lnk->seg = seg2;
        lst = lst->next;
    }
}

static void
MergeHSegs(HintSeg* seg1, HintSeg* seg2)
{
    MergeLnkSegs(seg1, seg2, Hlnks);
}

static void
MergeVSegs(HintSeg* seg1, HintSeg* seg2)
{
    MergeLnkSegs(seg1, seg2, Vlnks);
}

static void
ReportRemSeg(int32_t l, HintSeg* lst)
{
    Fixed from = 0, to = 0, loc = 0;
    /* this assumes !YgoesUp */
    switch (l) {
        case 1:
        case 2:
            from = lst->sMax;
            to = lst->sMin;
            break;
        case 0:
        case 3:
            from = lst->sMin;
            to = lst->sMax;
            break;
    }
    loc = lst->sLoc;
    switch (l) {
        case 0:
        case 1:
            LogMsg(LOGDEBUG, OK, "rem vseg %g %g to %g %g", FixToDbl(loc),
                   FixToDbl(-from), FixToDbl(loc), FixToDbl(-to));
            break;
        case 2:
        case 3:
            LogMsg(LOGDEBUG, OK, "rem hseg %g %g to %g %g", FixToDbl(from),
                   FixToDbl(-loc), FixToDbl(to), FixToDbl(-loc));
            break;
    }
}

/* Filters out bogus bend segments. */
static void
RemExtraBends(int32_t l0, int32_t l1)
{
    HintSeg* lst0 = gSegLists[l0];
    HintSeg* prv = NULL;
    while (lst0 != NULL) {
        HintSeg* nxt = lst0->sNxt;
        Fixed loc0 = lst0->sLoc;
        HintSeg* lst = gSegLists[l1];
        HintSeg* p = NULL;
        while (lst != NULL) {
            HintSeg* n = lst->sNxt;
            Fixed loc = lst->sLoc;
            if (loc > loc0)
                break; /* list in increasing order by sLoc */
            if (loc == loc0 && lst->sMin < lst0->sMax &&
                lst->sMax > lst0->sMin) {
                if (lst0->sType == sBEND && lst->sType != sBEND &&
                    lst->sType != sGHOST &&
                    (lst->sMax - lst->sMin) > (lst0->sMax - lst0->sMin) * 3) {
                    /* delete lst0 */
                    if (prv == NULL)
                        gSegLists[l0] = nxt;
                    else
                        prv->sNxt = nxt;
                    ReportRemSeg(l0, lst0);
                    lst0 = prv;
                    break;
                }
                if (lst->sType == sBEND && lst0->sType != sBEND &&
                    lst0->sType != sGHOST &&
                    (lst0->sMax - lst0->sMin) > (lst->sMax - lst->sMin) * 3) {
                    /* delete lst */
                    if (p == NULL)
                        gSegLists[l1] = n;
                    else
                        p->sNxt = n;
                    ReportRemSeg(l1, lst);
                    lst = p;
                }
            }
            p = lst;
            lst = n;
        }
        prv = lst0;
        lst0 = nxt;
    }
}

static void
CompactList(int32_t i, void (*nm)(HintSeg*, HintSeg*))
{
    HintSeg* lst = gSegLists[i];
    HintSeg* prv = NULL;
    while (lst != NULL) {
        bool flg;
        HintSeg* nxt = lst->sNxt;
        HintSeg* nxtprv = lst;
        while (true) {
            Fixed lstmin, lstmax, nxtmin, nxtmax;
            if ((nxt == NULL) || (nxt->sLoc > lst->sLoc)) {
                flg = true;
                break;
            }
            lstmin = lst->sMin;
            lstmax = lst->sMax;
            nxtmin = nxt->sMin;
            nxtmax = nxt->sMax;
            if (lstmax >= nxtmin && lstmin <= nxtmax) {
                /* do not worry about YgoesUp since "sMax" is really max in
                 device space, not in glyph space */
                if (abs(lstmax - lstmin) > abs(nxtmax - nxtmin)) {
                    /* merge into lst and remove nxt */
                    (*nm)(nxt, lst);
                    lst->sMin = NUMMIN(lstmin, nxtmin);
                    lst->sMax = NUMMAX(lstmax, nxtmax);
                    lst->sBonus = NUMMAX(lst->sBonus, nxt->sBonus);
                    nxtprv->sNxt = nxt->sNxt;
                } else { /* merge into nxt and remove lst */
                    (*nm)(lst, nxt);
                    nxt->sMin = NUMMIN(lstmin, nxtmin);
                    nxt->sMax = NUMMAX(lstmax, nxtmax);
                    nxt->sBonus = NUMMAX(lst->sBonus, nxt->sBonus);
                    lst = lst->sNxt;
                    if (prv == NULL)
                        gSegLists[i] = lst;
                    else
                        prv->sNxt = lst;
                }
                flg = false;
                break;
            }
            nxtprv = nxt;
            nxt = nxt->sNxt;
        }
        if (flg) {
            prv = lst;
            lst = lst->sNxt;
        }
    }
}

static Fixed
PickVSpot(Fixed x0, Fixed y0, Fixed x1, Fixed y1, Fixed px1, Fixed py1,
          Fixed px2, Fixed py2, Fixed prvx, Fixed prvy, Fixed nxtx, Fixed nxty)
{
    Fixed a1, a2;
    if (x0 == px1 && x1 != px2)
        return x0;
    if (x0 != px1 && x1 == px2)
        return x1;
    if (x0 == prvx && x1 != nxtx)
        return x0;
    if (x0 != prvx && x1 == nxtx)
        return x1;
    a1 = abs(py1 - y0);
    a2 = abs(py2 - y1);
    if (a1 > a2)
        return x0;
    a1 = abs(py2 - y1);
    a2 = abs(py1 - y0);
    if (a1 > a2)
        return x1;
    if (x0 == prvx && x1 == nxtx) {
        a1 = abs(y0 - prvy);
        a2 = abs(y1 - nxty);
        if (a1 > a2)
            return x0;
        return x1;
    }
    return FixHalfMul(x0 + x1);
}

static Fixed
AdjDist(Fixed d, Fixed q)
{
    Fixed val;
    if (q == FixOne) {
        DEBUG_ROUND(d) /* DEBUG 8 BIT */
        return d;
    }
    val = (d * q) >> 8;
    DEBUG_ROUND(val) /* DEBUG 8 BIT */
    return val;
}

/* serifs of ITCGaramond Ultra have points that are not quite horizontal
 e.g., in H: (53,51)(74,52)(116,54)
 the following was added to let these through */
static bool
TstFlat(Fixed dmn, Fixed dmx)
{
    if (dmn < 0)
        dmn = -dmn;
    if (dmx < 0)
        dmx = -dmx;
    return (dmx >= PSDist(50) && dmn <= PSDist(4));
}

static bool
NxtHorz(Fixed x, Fixed y, PathElt* p)
{
    Fixed x2, y2, x3, y3;
    NxtForBend(p, &x2, &y2, &x3, &y3);
    return TstFlat(y2 - y, x2 - x);
}

static bool
PrvHorz(Fixed x, Fixed y, PathElt* p)
{
    Fixed x2, y2;
    PrvForBend(p, &x2, &y2);
    return TstFlat(y2 - y, x2 - x);
}

static bool
NxtVert(Fixed x, Fixed y, PathElt* p)
{
    Fixed x2, y2, x3, y3;
    NxtForBend(p, &x2, &y2, &x3, &y3);
    return TstFlat(x2 - x, y2 - y);
}

static bool
PrvVert(Fixed x, Fixed y, PathElt* p)
{
    Fixed x2, y2;
    PrvForBend(p, &x2, &y2);
    return TstFlat(x2 - x, y2 - y);
}

/* PrvSameDir and NxtSameDir were added to check the direction of a
 path and not add a band if the point is not at an extreme and is
 going in the same direction as the previous path. */
static bool
TstSameDir(Fixed x0, Fixed y0, Fixed x1, Fixed y1, Fixed x2, Fixed y2)
{
    if (ProdLt0(y0 - y1, y1 - y2) || ProdLt0(x0 - x1, x1 - x2))
        return false;
    return !TestBend(x0, y0, x1, y1, x2, y2);
}

static bool
PrvSameDir(Fixed x0, Fixed y0, Fixed x1, Fixed y1, PathElt* p)
{
    Fixed x2, y2;
    p = PrvForBend(p, &x2, &y2);
    if (p != NULL && p->type == CURVETO && p->prev != NULL)
        GetEndPoint(p->prev, &x2, &y2);
    return TstSameDir(x0, y0, x1, y1, x2, y2);
}

static bool
NxtSameDir(Fixed x0, Fixed y0, Fixed x1, Fixed y1, PathElt* p)
{
    Fixed x2, y2, x3, y3;
    p = NxtForBend(p, &x2, &y2, &x3, &y3);
    if (p != NULL && p->type == CURVETO) {
        x2 = p->x3;
        y2 = p->y3;
    }
    return TstSameDir(x0, y0, x1, y1, x2, y2);
}

void
GenVPts(int32_t specialGlyphType)
{
    /* specialGlyphType 1 = upper; -1 = lower; 0 = neither */
    PathElt *p, *fl;
    bool isVert, flex1, flex2;
    Fixed flx0, fly0, llx, lly, urx, ury, yavg, yend, ydist, q, q2;
    Fixed prvx, prvy, nxtx, nxty, xx, yy, yd2;
    p = gPathStart;
    flex1 = flex2 = false;
    cpTo = gCPpercent;
    cpFrom = 100 - cpTo;
    flx0 = fly0 = 0;
    fl = NULL;
    while (p != NULL) {
        Fixed x0, y0, x1, y1;
        GetEndPoints(p, &x0, &y0, &x1, &y1);
        if (p->type == CURVETO) {
            Fixed px1, py1, px2, py2;
            isVert = false;
            if (p->isFlex) {
                if (flex1) {
                    if (IsVertical(flx0, fly0, x1, y1))
                        AddVSegment(fly0, y1, x1, fl->prev, p, sLINE, 4);
                    flex1 = false;
                    flex2 = true;
                } else {
                    flex1 = true;
                    flex2 = false;
                    flx0 = x0;
                    fly0 = y0;
                    fl = p;
                }
            } else
                flex1 = flex2 = false;
            px1 = p->x1;
            py1 = p->y1;
            px2 = p->x2;
            py2 = p->y2;
            if (!flex2) {
                if ((q = VertQuo(px1, py1, x0, y0)) ==
                    0) /* first two not vertical */
                    DoVBendsPrv(x0, y0, px1, py1, p);
                else {
                    isVert = true;
                    if (px1 == x0 ||
                        (px2 != x1 && (PrvVert(px1, py1, p) ||
                                       !PrvSameDir(x1, y1, x0, y0, p)))) {
                        if ((q2 = VertQuo(px2, py2, x0, y0)) > 0 &&
                            ProdGe0(py1 - y0, py2 - y0) &&
                            abs(py2 - y0) > abs(py1 - y0)) {
                            ydist = AdjDist(CPTo(py1, py2) - y0, q2);
                            yend = AdjDist(CPTo(y0, py1) - y0, q);
                            if (abs(yend) > abs(ydist))
                                ydist = yend;
                            AddVSegment(y0, y0 + ydist, x0, p->prev, p, sCURVE,
                                        5);
                        } else {
                            ydist = AdjDist(CPTo(y0, py1) - y0, q);
                            AddVSegment(y0, CPTo(y0, py1), x0, p->prev, p,
                                        sCURVE, 6);
                        }
                    }
                }
            }
            if (!flex1) {
                if ((q = VertQuo(px2, py2, x1, y1)) ==
                    0) /* last 2 not vertical */
                    DoVBendsNxt(px2, py2, x1, y1, p);
                else if (px2 == x1 ||
                         (px1 != x0 && (NxtVert(px2, py2, p) ||
                                        !NxtSameDir(x0, y0, x1, y1, p)))) {
                    ydist = AdjDist(y1 - CPFrom(py2, y1), q);
                    isVert = true;
                    q2 = VertQuo(x0, y0, x1, y1);
                    yd2 = (q2 > 0) ? AdjDist(y1 - y0, q2) : 0;
                    if (isVert && q2 > 0 && abs(yd2) > abs(ydist)) {
                        if (x0 == px1 && px1 == px2 && px2 == x1)
                            ReportLinearCurve(p, x0, y0, x1, y1);
                        ydist = FixHalfMul(yd2);
                        yavg = FixHalfMul(y0 + y1);
                        PrvForBend(p, &prvx, &prvy);
                        NxtForBend(p, &nxtx, &nxty, &xx, &yy);
                        AddVSegment(yavg - ydist, yavg + ydist,
                                    PickVSpot(x0, y0, x1, y1, px1, py1, px2,
                                              py2, prvx, prvy, nxtx, nxty),
                                    p, NULL, sCURVE, 7);
                    } else {
                        q2 = VertQuo(px1, py1, x1, y1);
                        if (q2 > 0 && ProdGe0(py1 - y1, py2 - y1) &&
                            abs(py2 - y1) < abs(py1 - y1)) {
                            yend = AdjDist(y1 - CPFrom(py1, py2), q2);
                            if (abs(yend) > abs(ydist))
                                ydist = yend;
                            AddVSegment(y1 - ydist, y1, x1, p, NULL, sCURVE, 8);
                        } else
                            AddVSegment(y1 - ydist, y1, x1, p, NULL, sCURVE, 9);
                    }
                }
            }
            if (!flex1 && !flex2) {
                Fixed minx, maxx;
                maxx = NUMMAX(x0, x1);
                minx = NUMMIN(x0, x1);
                if (px1 - maxx >= FixTwo || px2 - maxx >= FixTwo ||
                    px1 - minx <= FixTwo || px2 - minx <= FixTwo) {
                    FindCurveBBox(x0, y0, px1, py1, px2, py2, x1, y1, &llx,
                                  &lly, &urx, &ury);
                    if (urx - maxx > FixTwo || minx - llx > FixTwo) {
                        Fixed loc, frst, lst;
                        loc = (minx - llx > urx - maxx) ? llx : urx;
                        CheckBBoxEdge(p, true, loc, &frst, &lst);
                        yavg = FixHalfMul(frst + lst);
                        ydist = (frst == lst) ? (y1 - y0) / 10
                                              : FixHalfMul(lst - frst);
                        if (abs(ydist) < gBendLength)
                            ydist = (ydist > 0) ? FixHalfMul(gBendLength)
                                                : FixHalfMul(-gBendLength);
                        AddVSegment(yavg - ydist, yavg + ydist, loc, p, NULL,
                                    sCURVE, 10);
                    }
                }
            }
        } else if (p->type == MOVETO) {
            gBonus = 0;
            if (specialGlyphType == -1) {
                if (IsLower(p))
                    gBonus = FixInt(200);
            } else if (specialGlyphType == 1) {
                if (IsUpper(p))
                    gBonus = FixInt(200);
            }
        } else if (!IsTiny(p)) {
            if ((q = VertQuo(x0, y0, x1, y1)) > 0) {
                if (x0 == x1)
                    AddVSegment(y0, y1, x0, p->prev, p, sLINE, 11);
                else {
                    if (q < FixQuarter)
                        q = FixQuarter;
                    ydist = FixHalfMul(AdjDist(y1 - y0, q));
                    yavg = FixHalfMul(y0 + y1);
                    PrvForBend(p, &prvx, &prvy);
                    NxtForBend(p, &nxtx, &nxty, &xx, &yy);
                    AddVSegment(yavg - ydist, yavg + ydist,
                                PickVSpot(x0, y0, x1, y1, x0, y0, x1, y1, prvx,
                                          prvy, nxtx, nxty),
                                p, NULL, sLINE, 12);
                    if (abs(x0 - x1) <= FixTwo)
                        ReportNonVError(x0, y0, x1, y1);
                }
            } else {
                DoVBendsNxt(x0, y0, x1, y1, p);
                DoVBendsPrv(x0, y0, x1, y1, p);
            }
        }
        p = p->next;
    }
    CompactList(0, MergeVSegs);
    CompactList(1, MergeVSegs);
    RemExtraBends(0, 1);
    leftList = gSegLists[0];
    rightList = gSegLists[1];
}

bool
InBlueBand(Fixed loc, int32_t n, Fixed* p)
{
    int i;
    Fixed y;
    if (n <= 0)
        return false;
    y = -loc;
    /* Augment the blue band by bluefuzz in each direction.  This will
       result in "near misses" being hinted and so adjusted by the
       PS interpreter. */
    for (i = 0; i < n; i += 2)
        if ((p[i] - gBlueFuzz) <= y && (p[i + 1] + gBlueFuzz) >= y)
            return true;
    return false;
}

static Fixed
PickHSpot(Fixed x0, Fixed y0, Fixed x1, Fixed y1, Fixed xdist, Fixed px1,
          Fixed py1, Fixed px2, Fixed py2, Fixed prvx, Fixed prvy, Fixed nxtx,
          Fixed nxty)
{
    bool topSeg = (xdist < 0) ? true : false;
    bool inBlue0, inBlue1;
    if (topSeg) {
        inBlue0 = InBlueBand(y0, gLenTopBands, gTopBands);
        inBlue1 = InBlueBand(y1, gLenTopBands, gTopBands);
    } else {
        inBlue0 = InBlueBand(y0, gLenBotBands, gBotBands);
        inBlue1 = InBlueBand(y1, gLenBotBands, gBotBands);
    }
    if (inBlue0 && !inBlue1)
        return y0;
    if (inBlue1 && !inBlue0)
        return y1;
    if (y0 == py1 && y1 != py2)
        return y0;
    if (y0 != py1 && y1 == py2)
        return y1;
    if (y0 == prvy && y1 != nxty)
        return y0;
    if (y0 != prvy && y1 == nxty)
        return y1;
    if (inBlue0 && inBlue1) {
        Fixed upper, lower;
        if (y0 > y1) {
            upper = y1;
            lower = y0;
        } else {
            upper = y0;
            lower = y1;
        }
        return topSeg ? upper : lower;
    }
    if (abs(px1 - x0) > abs(px2 - x1))
        return y0;
    if (abs(px2 - x1) > abs(px1 - x0))
        return y1;
    if (y0 == prvy && y1 == nxty) {
        if (abs(x0 - prvx) > abs(x1 - nxtx))
            return y0;
        return y1;
    }
    return FixHalfMul(y0 + y1);
}

void
GenHPts(void)
{
    PathElt *p, *fl;
    bool isHoriz, flex1, flex2;
    Fixed flx0, fly0, llx, lly, urx, ury, xavg, xend, xdist, q, q2;
    Fixed prvx, prvy, nxtx, nxty, xx, yy, xd2;
    p = gPathStart;
    gBonus = 0;
    flx0 = fly0 = 0;
    fl = NULL;
    flex1 = flex2 = false;
    cpTo = gCPpercent;
    cpFrom = 100 - cpTo;
    while (p != NULL) {
        Fixed x0, y0, x1, y1;
        GetEndPoints(p, &x0, &y0, &x1, &y1);
        if (p->type == CURVETO) {
            Fixed px1, py1, px2, py2;
            isHoriz = false;
            if (p->isFlex) {
                if (flex1) {
                    flex1 = false;
                    flex2 = true;
                    if (IsHorizontal(flx0, fly0, x1, y1))
                        AddHSegment(flx0, x1, y1, fl->prev, p, sLINE, 4);
                } else {
                    flex1 = true;
                    flex2 = false;
                    flx0 = x0;
                    fly0 = y0;
                    fl = p;
                }
            } else
                flex1 = flex2 = false;
            px1 = p->x1;
            py1 = p->y1;
            px2 = p->x2;
            py2 = p->y2;
            if (!flex2) {
                if ((q = HorzQuo(px1, py1, x0, y0)) == 0)
                    DoHBendsPrv(x0, y0, px1, py1, p);
                else {
                    isHoriz = true;
                    if (py1 == y0 ||
                        (py2 != y1 && (PrvHorz(px1, py1, p) ||
                                       !PrvSameDir(x1, y1, x0, y0, p)))) {
                        if ((q2 = HorzQuo(px2, py2, x0, y0)) > 0 &&
                            ProdGe0(px1 - x0, px2 - x0) &&
                            abs(px2 - x0) > abs(px1 - x0)) {
                            xdist = AdjDist(CPTo(px1, px2) - x0, q2);
                            xend = AdjDist(CPTo(x0, px1) - x0, q);
                            if (abs(xend) > abs(xdist))
                                xdist = xend;
                            AddHSegment(x0, x0 + xdist, y0, p->prev, p, sCURVE,
                                        5);
                        } else {
                            xdist = AdjDist(CPTo(x0, px1) - x0, q);
                            AddHSegment(x0, x0 + xdist, y0, p->prev, p, sCURVE,
                                        6);
                        }
                    }
                }
            }
            if (!flex1) {
                if ((q = HorzQuo(px2, py2, x1, y1)) == 0)
                    DoHBendsNxt(px2, py2, x1, y1, p);
                else if (py2 == y1 ||
                         (py1 != y0 && (NxtHorz(px2, py2, p) ||
                                        !NxtSameDir(x0, y0, x1, y1, p)))) {
                    xdist = AdjDist(x1 - CPFrom(px2, x1), q);
                    q2 = HorzQuo(x0, y0, x1, y1);
                    isHoriz = true;
                    xd2 = (q2 > 0) ? AdjDist(x1 - x0, q2) : 0;
                    if (isHoriz && q2 > 0 && abs(xd2) > abs(xdist)) {
                        Fixed hspot;
                        if (y0 == py1 && py1 == py2 && py2 == y1)
                            ReportLinearCurve(p, x0, y0, x1, y1);
                        PrvForBend(p, &prvx, &prvy);
                        NxtForBend(p, &nxtx, &nxty, &xx, &yy);
                        xdist = FixHalfMul(xd2);
                        xavg = FixHalfMul(x0 + x1);
                        hspot = PickHSpot(x0, y0, x1, y1, xdist, px1, py1, px2,
                                          py2, prvx, prvy, nxtx, nxty);
                        AddHSegment(xavg - xdist, xavg + xdist, hspot, p, NULL,
                                    sCURVE, 7);
                    } else {
                        q2 = HorzQuo(px1, py1, x1, y1);
                        if (q2 > 0 && ProdGe0(px1 - x1, px2 - x1) &&
                            abs(px2 - x1) < abs(px1 - x1)) {
                            xend = AdjDist(x1 - CPFrom(px1, px2), q2);
                            if (abs(xend) > abs(xdist))
                                xdist = xend;
                            AddHSegment(x1 - xdist, x1, y1, p, NULL, sCURVE, 8);
                        } else
                            AddHSegment(x1 - xdist, x1, y1, p, NULL, sCURVE, 9);
                    }
                }
            }
            if (!flex1 && !flex2) {
                Fixed miny, maxy;
                maxy = NUMMAX(y0, y1);
                miny = NUMMIN(y0, y1);
                if (py1 - maxy >= FixTwo || py2 - maxy >= FixTwo ||
                    py1 - miny <= FixTwo || py2 - miny <= FixTwo) {
                    FindCurveBBox(x0, y0, px1, py1, px2, py2, x1, y1, &llx,
                                  &lly, &urx, &ury);
                    if (ury - maxy > FixTwo || miny - lly > FixTwo) {
                        Fixed loc, frst, lst;
                        loc = (miny - lly > ury - maxy) ? lly : ury;
                        CheckBBoxEdge(p, false, loc, &frst, &lst);
                        xavg = FixHalfMul(frst + lst);
                        xdist = (frst == lst) ? (x1 - x0) / 10
                                              : FixHalfMul(lst - frst);
                        if (abs(xdist) < gBendLength)
                            xdist = (xdist > 0.0) ? FixHalfMul(gBendLength)
                                                  : FixHalfMul(-gBendLength);
                        AddHSegment(xavg - xdist, xavg + xdist, loc, p, NULL,
                                    sCURVE, 10);
                    }
                }
            }
        } else if (p->type != MOVETO && !IsTiny(p)) {
            if ((q = HorzQuo(x0, y0, x1, y1)) > 0) {
                if (y0 == y1)
                    AddHSegment(x0, x1, y0, p->prev, p, sLINE, 11);
                else {
                    if (q < FixQuarter)
                        q = FixQuarter;
                    xdist = FixHalfMul(AdjDist(x1 - x0, q));
                    xavg = FixHalfMul(x0 + x1);
                    PrvForBend(p, &prvx, &prvy);
                    NxtForBend(p, &nxtx, &nxty, &xx, &yy);
                    yy = PickHSpot(x0, y0, x1, y1, xdist, x0, y0, x1, y1, prvx,
                                   prvy, nxtx, nxty);
                    AddHSegment(xavg - xdist, xavg + xdist, yy, p->prev, p,
                                sLINE, 12);
                    if (abs(y0 - y1) <= FixTwo)
                        ReportNonHError(x0, y0, x1, y1);
                }
            } else {
                DoHBendsNxt(x0, y0, x1, y1, p);
                DoHBendsPrv(x0, y0, x1, y1, p);
            }
        }
        p = p->next;
    }
    CompactList(2, MergeHSegs);
    CompactList(3, MergeHSegs);
    RemExtraBends(2, 3);
    topList = gSegLists[2]; /* this is probably unnecessary */
    botList = gSegLists[3];
    CheckTfmVal(topList, gTopBands, gLenTopBands);
    CheckTfmVal(botList, gBotBands, gLenBotBands);
}

void
PreGenPts(void)
{
    Hlnks = Vlnks = NULL;
    gSegLists[0] = NULL;
    gSegLists[1] = NULL;
    gSegLists[2] = NULL;
    gSegLists[3] = NULL;
}
