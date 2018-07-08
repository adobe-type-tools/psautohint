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

static bool clrBBox, clrHBounds, clrVBounds, haveHBnds, haveVBnds, mergeMain;

void
InitAuto(int32_t reason)
{
    switch (reason) {
        case STARTUP:
        case RESTART:
            clrBBox = clrHBounds = clrVBounds = haveHBnds = haveVBnds = false;
    }
}

static PPathElt
GetSubPathNxt(PPathElt e)
{
    if (e->type == CLOSEPATH)
        return GetDest(e);
    return e->next;
}

static PPathElt
GetSubPathPrv(PPathElt e)
{
    if (e->type == MOVETO)
        e = GetClosedBy(e);
    return e->prev;
}

static PClrVal
FindClosestVal(PClrVal sLst, Fixed loc)
{
    Fixed dist = FixInt(10000);
    PClrVal best = NULL;
    while (sLst != NULL) {
        Fixed bot, top, d;
        bot = sLst->vLoc1;
        top = sLst->vLoc2;
        if (bot > top) {
            Fixed tmp = bot;
            bot = top;
            top = tmp;
        }
        if (loc >= bot && loc <= top) {
            best = sLst;
            break;
        }
        if (loc < bot)
            d = bot - loc;
        else
            d = loc - top;
        if (d < dist) {
            dist = d;
            best = sLst;
        }
        sLst = sLst->vNxt;
    }
    return best;
}

static void
CpyHClr(PPathElt e)
{
    Fixed x1, y1;
    PClrVal best;
    GetEndPoint(e, &x1, &y1);
    best = FindClosestVal(gHPrimary, y1);
    if (best != NULL)
        AddHPair(best, 'b');
}

static void
CpyVClr(PPathElt e)
{
    Fixed x1, y1;
    PClrVal best;
    GetEndPoint(e, &x1, &y1);
    best = FindClosestVal(gVPrimary, x1);
    if (best != NULL)
        AddVPair(best, 'y');
}

static void
PruneColorSegs(PPathElt e, bool hFlg)
{
    PSegLnkLst lst, nxt, prv;
    PClrSeg seg;
    lst = hFlg ? e->Hs : e->Vs;
    prv = NULL;
    while (lst != NULL) {
        PClrVal val = NULL;
        PSegLnk lnk = lst->lnk;
        if (lnk != NULL) {
            seg = lnk->seg;
            if (seg != NULL)
                val = seg->sLnk;
        }
        nxt = lst->next;
        if (val == NULL) { /* prune this one */
            if (prv == NULL) {
                if (hFlg)
                    e->Hs = nxt;
                else
                    e->Vs = nxt;
            } else
                prv->next = nxt;
            lst = nxt;
        } else {
            prv = lst;
            lst = nxt;
        }
    }
}

void
PruneElementColorSegs(void)
{
    PPathElt e;
    e = gPathStart;
    while (e != NULL) {
        PruneColorSegs(e, true);
        PruneColorSegs(e, false);
        e = e->next;
    }
}

#define ElmntClrSegLst(e, hFlg) (hFlg) ? (e)->Hs : (e)->Vs

static void
RemLnk(PPathElt e, bool hFlg, PSegLnkLst rm)
{
    PSegLnkLst lst, prv, nxt;
    lst = hFlg ? e->Hs : e->Vs;
    prv = NULL;
    while (lst != NULL) {
        nxt = lst->next;
        if (lst == rm) {
            if (prv == NULL) {
                if (hFlg)
                    e->Hs = nxt;
                else
                    e->Vs = nxt;
            } else
                prv->next = nxt;
            return;
        }
        prv = lst;
        lst = nxt;
    }
    LogMsg(LOGERROR, NONFATALERROR,
           "Badly formatted segment list in glyph: %s.\n", gGlyphName);
}

static bool
AlreadyOnList(PClrVal v, PClrVal lst)
{
    while (lst != NULL) {
        if (v == lst)
            return true;
        lst = lst->vNxt;
    }
    return false;
}

static void
AutoVSeg(PClrVal sLst)
{
    AddVPair(sLst, 'y');
}

static void
AutoHSeg(PClrVal sLst)
{
    AddHPair(sLst, 'b');
}

static void
AddHColoring(PClrVal h)
{
    if (gUseH || AlreadyOnList(h, gHColoring))
        return;
    h->vNxt = gHColoring;
    gHColoring = h;
    AutoHSeg(h);
}

static void
AddVColoring(PClrVal v)
{
    if (gUseV || AlreadyOnList(v, gVColoring))
        return;
    v->vNxt = gVColoring;
    gVColoring = v;
    AutoVSeg(v);
}

static int32_t
TestColor(PClrSeg s, PClrVal colorList, bool flg, bool doLst)
{
    /* -1 means already in colorList; 0 means conflicts; 1 means ok to add */
    PClrVal v, clst;
    Fixed top, bot, vT, vB, loc;
    if (s == NULL)
        return -1;
    v = s->sLnk;
    loc = s->sLoc;
    if (v == NULL)
        return -1;
    vT = top = v->vLoc2;
    vB = bot = v->vLoc1;
    if (v->vGhst) { /* collapse width for conflict test */
        if (v->vSeg1->sType == sGHOST)
            bot = top;
        else
            top = bot;
    }
    {
        int32_t cnt = 0;
        clst = colorList;
        while (clst != NULL) {
            if (++cnt > 100) {
                LogMsg(LOGDEBUG, OK, "Loop in hintlist for TestHint\n\007");
                return 0;
            }
            clst = clst->vNxt;
        }
    }
    if (v->vGhst) {
        bool loc1;
        /* if best value for segment uses a ghost, and
           segment loc is already in the colorList, then return -1 */
        clst = colorList;
        if (abs(loc - vT) < abs(loc - vB)) {
            loc1 = false;
            loc = vT;
        } else {
            loc1 = true;
            loc = vB;
        }
        while (clst != NULL) {
            if ((loc1 ? clst->vLoc1 : clst->vLoc2) == loc)
                return -1;
            clst = clst->vNxt;
            if (!doLst)
                break;
        }
    }
    if (flg) {
        top += gBandMargin;
        bot -= gBandMargin;
    } else {
        top -= gBandMargin;
        bot += gBandMargin;
    }
    while (colorList != NULL) { /* check for conflict */
        Fixed cTop = colorList->vLoc2;
        Fixed cBot = colorList->vLoc1;
        if (vB == cBot && vT == cTop) {
            return -1;
        }
        if (colorList->vGhst) { /* collapse width for conflict test */
            if (colorList->vSeg1->sType == sGHOST)
                cBot = cTop;
            else
                cTop = cBot;
        }
        if ((flg && (cBot <= top) && (cTop >= bot)) ||
            (!flg && (cBot >= top) && (cTop <= bot))) {
            return 0;
        }
        colorList = colorList->vNxt;
        if (!doLst)
            break;
    }
    return 1;
}

#define TestHColorLst(h) TestColorLst(h, gHColoring, gYgoesUp, true)
#define TestVColorLst(v) TestColorLst(v, gVColoring, true, true)

int
TestColorLst(PSegLnkLst lst, PClrVal colorList, bool flg, bool doLst)
{
    /* -1 means already in colorList; 0 means conflicts; 1 means ok to add */
    int result, cnt;
    result = -1;
    cnt = 0;
    while (lst != NULL) {
        int i = TestColor(lst->lnk->seg, colorList, flg, doLst);
        if (i == 0) {
            result = 0;
            break;
        }
        if (i == 1)
            result = 1;
        lst = lst->next;
        if (++cnt > 100) {
            LogMsg(WARNING, OK, "Looping in TestHintLst\007\n");
            return 0;
        }
    }
    return result;
}

#define FixedMidPoint(m, a, b)                                                 \
    (m).x = ((a).x + (b).x) >> 1;                                              \
    (m).y = ((a).y + (b).y) >> 1

#define FixedBezDiv(a0, a1, a2, a3, b0, b1, b2, b3)                            \
    FixedMidPoint(b2, a2, a3);                                                 \
    FixedMidPoint(a3, a1, a2);                                                 \
    FixedMidPoint(a1, a0, a1);                                                 \
    FixedMidPoint(a2, a1, a3);                                                 \
    FixedMidPoint(b1, a3, b2);                                                 \
    FixedMidPoint(a3, a2, b1);

bool
ResolveConflictBySplit(PPathElt e, bool Hflg, PSegLnkLst lnk1, PSegLnkLst lnk2)
{
    /* insert new pathelt immediately following e */
    /* e gets first half of split; new gets second */
    /* e gets lnk1 in Hs or Vs; new gets lnk2 */
    PPathElt new;
    Cd d0, d1, d2, d3, d4, d5, d6, d7;
    if (e->type != CURVETO || e->isFlex)
        return false;
    ReportSplit(e);
    new = (PPathElt)Alloc(sizeof(PathElt));
    new->next = e->next;
    e->next = new;
    new->prev = e;
    if (new->next == NULL)
        gPathEnd = new;
    else
        new->next->prev = new;
    if (Hflg) {
        e->Hs = lnk1;
        new->Hs = lnk2;
    } else {
        e->Vs = lnk1;
        new->Vs = lnk2;
    }
    if (lnk1 != NULL)
        lnk1->next = NULL;
    if (lnk2 != NULL)
        lnk2->next = NULL;
    new->type = CURVETO;
    GetEndPoint(e->prev, &d0.x, &d0.y);
    d1.x = e->x1;
    d1.y = e->y1;
    d2.x = e->x2;
    d2.y = e->y2;
    d3.x = e->x3;
    d3.y = e->y3;
    d4 = d0;
    d5 = d1;
    d6 = d2;
    d7 = d3;
    new->x3 = d3.x;
    new->y3 = d3.y;
    FixedBezDiv(d4, d5, d6, d7, d0, d1, d2, d3);
    e->x1 = d5.x;
    e->y1 = d5.y;
    e->x2 = d6.x;
    e->y2 = d6.y;
    e->x3 = d7.x;
    e->y3 = d7.y;
    new->x1 = d1.x;
    new->y1 = d1.y;
    new->x2 = d2.x;
    new->y2 = d2.y;
    return true;
}

static void
RemDupLnks(PPathElt e, bool Hflg)
{
    PSegLnkLst l1, l2, l2nxt;
    l1 = Hflg ? e->Hs : e->Vs;
    while (l1 != NULL) {
        l2 = l1->next;
        while (l2 != NULL) {
            l2nxt = l2->next;
            if (l1->lnk->seg == l2->lnk->seg)
                RemLnk(e, Hflg, l2);
            l2 = l2nxt;
        }
        l1 = l1->next;
    }
}

#define OkToRemLnk(loc, Hflg, spc)                                             \
    (!(Hflg) || (spc) == 0 ||                                                  \
     (!InBlueBand((loc), gLenTopBands, gTopBands) &&                           \
      !InBlueBand((loc), gLenBotBands, gBotBands)))

/* The changes made here were to fix a problem in MinisterLight/E.
   The top left point was not getting colored. */
static bool
TryResolveConflict(PPathElt e, bool Hflg)
{
    int32_t typ;
    PSegLnkLst lst, lnk1, lnk2;
    PClrSeg seg, seg1, seg2;
    PClrVal val1, val2;
    Fixed lc1, lc2, loc0, loc1, loc2, loc3, x0, y0, x1, y1;
    RemDupLnks(e, Hflg);
    typ = e->type;
    if (typ == MOVETO)
        GetEndPoints(GetClosedBy(e), &x0, &y0, &x1, &y1);
    else if (typ == CURVETO) {
        x0 = e->x1;
        y0 = e->y1;
        x1 = e->x3;
        y1 = e->y3;
    } else
        GetEndPoints(e, &x0, &y0, &x1, &y1);
    loc1 = Hflg ? y0 : x0;
    loc2 = Hflg ? y1 : x1;
    lst = Hflg ? e->Hs : e->Vs;
    seg1 = lst->lnk->seg;
    lc1 = seg1->sLoc;
    lnk1 = lst;
    lst = lst->next;
    seg2 = lst->lnk->seg;
    lc2 = seg2->sLoc;
    lnk2 = lst;
    if (lc1 == loc1 || lc2 == loc2) {
    } else if (abs(lc1 - loc1) > abs(lc1 - loc2) ||
               abs(lc2 - loc2) > abs(lc2 - loc1)) {
        seg = seg1;
        seg1 = seg2;
        seg2 = seg;
        lst = lnk1;
        lnk1 = lnk2;
        lnk2 = lst;
    }
    val1 = seg1->sLnk;
    val2 = seg2->sLnk;
    if (val1->vVal < FixInt(50) && OkToRemLnk(loc1, Hflg, val1->vSpc)) {
        RemLnk(e, Hflg, lnk1);
        ReportRemConflict(e);
        return true;
    }
    if (val2->vVal < FixInt(50) && val1->vVal > val2->vVal * 20 &&
        OkToRemLnk(loc2, Hflg, val2->vSpc)) {
        RemLnk(e, Hflg, lnk2);
        ReportRemConflict(e);
        return true;
    }
    if (typ != CURVETO || ((((Hflg && IsHorizontal(x0, y0, x1, y1)) ||
                             (!Hflg && IsVertical(x0, y0, x1, y1)))) &&
                           OkToRemLnk(loc1, Hflg, val1->vSpc))) {
        RemLnk(e, Hflg, lnk1);
        ReportRemConflict(e);
        return true;
    }
    GetEndPoints(GetSubPathPrv(e), &x0, &y0, &x1, &y1);
    loc0 = Hflg ? y0 : x0;
    if (ProdLt0(loc2 - loc1, loc0 - loc1)) {
        RemLnk(e, Hflg, lnk1);
        ReportRemConflict(e);
        return true;
    }
    GetEndPoint(GetSubPathNxt(e), &x1, &y1);
    loc3 = Hflg ? y1 : x1;
    if (ProdLt0(loc3 - loc2, loc1 - loc2)) {
        RemLnk(e, Hflg, lnk2);
        ReportRemConflict(e);
        return true;
    }
    if ((loc2 == val2->vLoc1 || loc2 == val2->vLoc2) && loc1 != val1->vLoc1 &&
        loc1 != val1->vLoc2) {
        RemLnk(e, Hflg, lnk1);
        ReportRemConflict(e);
        return true;
    }
    if ((loc1 == val1->vLoc1 || loc1 == val1->vLoc2) && loc2 != val2->vLoc1 &&
        loc2 != val2->vLoc2) {
        RemLnk(e, Hflg, lnk2);
        ReportRemConflict(e);
        return true;
    }
    if (gEditChar && ResolveConflictBySplit(e, Hflg, lnk1, lnk2))
        return true;
    else
        return false;
}

static bool
CheckColorSegs(PPathElt e, bool flg, bool Hflg)
{
    PSegLnkLst lst;
    PSegLnkLst lst2;
    PClrSeg seg;
    PClrVal val;
    lst = Hflg ? e->Hs : e->Vs;
    while (lst != NULL) {
        lst2 = lst->next;
        if (lst2 != NULL) {
            seg = lst->lnk->seg;
            val = seg->sLnk;
            if (val != NULL && TestColorLst(lst2, val, flg, false) == 0) {
                if (TryResolveConflict(e, Hflg))
                    return CheckColorSegs(e, flg, Hflg);
                AskForSplit(e);
                if (Hflg)
                    e->Hs = NULL;
                else
                    e->Vs = NULL;
                return true;
            }
        }
        lst = lst2;
    }
    return false;
}

static void
CheckElmntClrSegs(void)
{
    PPathElt e;
    e = gPathStart;
    while (e != NULL) {
        if (!CheckColorSegs(e, gYgoesUp, true))
            (void)CheckColorSegs(e, true, false);
        e = e->next;
    }
}
static bool
ClrLstsClash(PSegLnkLst lst1, PSegLnkLst lst2, bool flg)
{
    while (lst1 != NULL) {
        PClrSeg seg = lst1->lnk->seg;
        PClrVal val = seg->sLnk;
        if (val != NULL) {
            PSegLnkLst lst = lst2;
            while (lst != NULL) {
                if (TestColorLst(lst, val, flg, false) == 0) {
                    return true;
                }
                lst = lst->next;
            }
        }
        lst1 = lst1->next;
    }
    return false;
}

static PSegLnkLst
BestFromLsts(PSegLnkLst lst1, PSegLnkLst lst2)
{
    PSegLnkLst bst = NULL;
    Fixed bstval = 0;
    int32_t i;
    for (i = 0; i < 2; i++) {
        PSegLnkLst lst = i ? lst1 : lst2;
        while (lst != NULL) {
            PClrSeg seg = lst->lnk->seg;
            PClrVal val = seg->sLnk;
            if (val != NULL && val->vVal > bstval) {
                bst = lst;
                bstval = val->vVal;
            }
            lst = lst->next;
        }
    }
    return bst;
}

static bool
ClrsClash(PPathElt e, PPathElt p, PSegLnkLst* hLst, PSegLnkLst* vLst,
          PSegLnkLst* phLst, PSegLnkLst* pvLst)
{
    bool clash = false;
    PSegLnkLst bst, new;
    if (ClrLstsClash(*hLst, *phLst, gYgoesUp)) {
        clash = true;
        bst = BestFromLsts(*hLst, *phLst);
        if (bst) {
            new = (PSegLnkLst)Alloc(sizeof(SegLnkLst));
            new->next = NULL;
            new->lnk = bst->lnk;
        } else
            new = NULL;
        e->Hs = p->Hs = *hLst = *phLst = new;
    }
    if (ClrLstsClash(*vLst, *pvLst, true)) {
        clash = true;
        bst = BestFromLsts(*vLst, *pvLst);
        if (bst) {
            new = (PSegLnkLst)Alloc(sizeof(SegLnkLst));
            new->next = NULL;
            new->lnk = bst->lnk;
        } else
            new = NULL;
        e->Vs = p->Vs = *vLst = *pvLst = new;
    }
    return clash;
}

static void
GetColorLsts(PPathElt e, PSegLnkLst* phLst, PSegLnkLst* pvLst, int32_t* ph,
             int32_t* pv)
{
    PSegLnkLst hLst, vLst;
    int32_t h, v;
    if (gUseH) {
        hLst = NULL;
        h = -1;
    } else {
        hLst = e->Hs;
        if (hLst == NULL)
            h = -1;
        else
            h = TestHColorLst(hLst);
    }
    if (gUseV) {
        vLst = NULL;
        v = -1;
    } else {
        vLst = e->Vs;
        if (vLst == NULL)
            v = -1;
        else
            v = TestVColorLst(vLst);
    }
    *pvLst = vLst;
    *phLst = hLst;
    *ph = h;
    *pv = v;
}

static void
ReClrBounds(PPathElt e)
{
    if (!gUseH) {
        if (clrHBounds && gHColoring == NULL && !haveHBnds)
            ReClrHBnds();
        else if (!clrBBox) {
            if (gHColoring == NULL)
                CpyHClr(e);
            if (mergeMain)
                MergeFromMainColors('b');
        }
    }
    if (!gUseV) {
        if (clrVBounds && gVColoring == NULL && !haveVBnds)
            ReClrVBnds();
        else if (!clrBBox) {
            if (gVColoring == NULL)
                CpyVClr(e);
            if (mergeMain)
                MergeFromMainColors('y');
        }
    }
}

static void
AddColorLst(PSegLnkLst lst, bool vert)
{
    PClrVal val;
    PClrSeg seg;
    while (lst != NULL) {
        seg = lst->lnk->seg;
        val = seg->sLnk;
        if (vert)
            AddVColoring(val);
        else
            AddHColoring(val);
        lst = lst->next;
    }
}

static void
StartNewColoring(PPathElt e, PSegLnkLst hLst, PSegLnkLst vLst)
{
    ReClrBounds(e);
    if (e->newcolors != 0) {
        LogMsg(LOGERROR, NONFATALERROR,
               "Uninitialized extra hints list in glyph: %s.\n", gGlyphName);
    }
    XtraClrs(e);
    clrBBox = false;
    if (gUseV)
        CopyMainV();
    if (gUseH)
        CopyMainH();
    gHColoring = gVColoring = NULL;
    if (!gUseH)
        AddColorLst(hLst, false);
    if (!gUseV)
        AddColorLst(vLst, true);
}

static bool
IsIn(int32_t h, int32_t v)
{
    return (h == -1 && v == -1);
}

static bool
IsOk(int32_t h, int32_t v)
{
    return (h != 0 && v != 0);
}

#define AddIfNeedV(v, vLst)                                                    \
    if (!gUseV && v == 1)                                                      \
    AddColorLst(vLst, true)
#define AddIfNeedH(h, hLst)                                                    \
    if (!gUseH && h == 1)                                                      \
    AddColorLst(hLst, false)

static void
SetHColors(PClrVal lst)
{
    if (gUseH)
        return;
    gHColoring = lst;
    while (lst != NULL) {
        AutoHSeg(lst);
        lst = lst->vNxt;
    }
}

static void
SetVColors(PClrVal lst)
{
    if (gUseV)
        return;
    gVColoring = lst;
    while (lst != NULL) {
        AutoVSeg(lst);
        lst = lst->vNxt;
    }
}

PClrVal
CopyClrs(PClrVal lst)
{
    PClrVal vlst;
    int cnt;
    vlst = NULL;
    cnt = 0;
    while (lst != NULL) {
        PClrVal v = (PClrVal)Alloc(sizeof(ClrVal));
        *v = *lst;
        v->vNxt = vlst;
        vlst = v;
        if (++cnt > 100) {
            LogMsg(WARNING, OK, "Loop in CopyClrs\007\n");
            return vlst;
        }
        lst = lst->vNxt;
    }
    return vlst;
}

static PPathElt
ColorBBox(PPathElt e)
{
    e = FindSubpathBBox(e);
    ClrBBox();
    clrBBox = true;
    return e;
}

static bool
IsFlare(Fixed loc, PPathElt e, PPathElt n, bool Hflg)
{
    Fixed x, y;
    while (e != n) {
        GetEndPoint(e, &x, &y);
        if ((Hflg && abs(y - loc) > gMaxFlare) ||
            (!Hflg && abs(x - loc) > gMaxFlare))
            return false;
        e = GetSubPathNxt(e);
    }
    return true;
}

static bool
IsTopSegOfVal(Fixed loc, Fixed top, Fixed bot)
{
    Fixed d1, d2;
    d1 = top - loc;
    d2 = bot - loc;
    return (abs(d1) <= abs(d2)) ? true : false;
}

static void
RemFlareLnk(PPathElt e, bool hFlg, PSegLnkLst rm, PPathElt e2, int32_t i)
{
    RemLnk(e, hFlg, rm);
    ReportRemFlare(e, e2, hFlg, i);
}

bool
CompareValues(PClrVal val1, PClrVal val2, int32_t factor, int32_t ghstshift)
{
    Fixed v1 = val1->vVal, v2 = val2->vVal, mx;
    mx = v1 > v2 ? v1 : v2;
    mx <<= 1;
    while (mx > 0) {
        mx <<= 1;
        v1 <<= 1;
        v2 <<= 1;
    }
    if (ghstshift > 0 && val1->vGhst != val2->vGhst) {
        if (val1->vGhst)
            v1 >>= ghstshift;
        if (val2->vGhst)
            v2 >>= ghstshift;
    }
    if ((val1->vSpc > 0 && val2->vSpc > 0) ||
        (val1->vSpc == 0 && val2->vSpc == 0))
        return v1 > v2;
    if (val1->vSpc > 0)
        return (v1 < FixedPosInf / factor) ? (v1 * factor > v2)
                                           : (v1 > v2 / factor);
    return (v2 < FixedPosInf / factor) ? (v1 > v2 * factor)
                                       : (v1 / factor > v2);
}

static void
RemFlares(bool Hflg)
{
    PSegLnkLst lst1, lst2, nxt1, nxt2;
    PPathElt e, n;
    PClrSeg seg1, seg2;
    PClrVal val1, val2;
    Fixed diff;
    bool nxtE;
    bool Nm1, Nm2;
    if (Hflg) {
        Nm1 = true;
        Nm2 = false;
    } else {
        Nm1 = false;
        Nm2 = true;
    }
    e = gPathStart;
    while (e != NULL) {
        if (Nm1 ? e->Hs == NULL : e->Vs == NULL) {
            e = e->next;
            continue;
        }
        /* e now is an element with Nm1 prop */
        n = GetSubPathNxt(e);
        nxtE = false;
        while (n != e && !nxtE) {
            if (Nm1 ? n->Hs != NULL : n->Vs != NULL) {
                lst1 = ElmntClrSegLst(e, Nm1);
                while (lst1 != NULL) {
                    seg1 = lst1->lnk->seg;
                    nxt1 = lst1->next;
                    lst2 = ElmntClrSegLst(n, Nm1);
                    while (lst2 != NULL) {
                        seg2 = lst2->lnk->seg;
                        nxt2 = lst2->next;
                        if (seg1 != NULL && seg2 != NULL) {
                            diff = seg1->sLoc - seg2->sLoc;
                            if (abs(diff) > gMaxFlare) {
                                nxtE = true;
                                goto Nxt2;
                            }
                            if (!IsFlare(seg1->sLoc, e, n, Hflg)) {
                                nxtE = true;
                                goto Nxt2;
                            }
                            val1 = seg1->sLnk;
                            val2 = seg2->sLnk;
                            if (diff != 0 &&
                                IsTopSegOfVal(seg1->sLoc, val1->vLoc2,
                                              val1->vLoc1) ==
                                  IsTopSegOfVal(seg2->sLoc, val2->vLoc2,
                                                val2->vLoc1)) {
                                if (CompareValues(val1, val2, spcBonus, 0)) {
                                    /* This change was made to fix flares in
                                     * Bodoni2. */
                                    if (val2->vSpc == 0 &&
                                        val2->vVal < FixInt(1000))
                                        RemFlareLnk(n, Nm1, lst2, e, 1);
                                } else if (val1->vSpc == 0 &&
                                           val1->vVal < FixInt(1000)) {
                                    RemFlareLnk(e, Nm1, lst1, n, 2);
                                    goto Nxt1;
                                }
                            }
                        }
                    Nxt2:
                        lst2 = nxt2;
                    }
                Nxt1:
                    lst1 = nxt1;
                }
            }
            if (Nm2 ? n->Hs != NULL : n->Vs != NULL)
                break;
            n = GetSubPathNxt(n);
        }
        e = e->next;
    }
}

static void
CarryIfNeed(Fixed loc, bool vert, PClrVal clrs)
{
    PClrSeg seg;
    PClrVal seglnk;
    Fixed l0, l1, tmp, halfMargin;
    if ((vert && gUseV) || (!vert && gUseH))
        return;
    halfMargin = FixHalfMul(gBandMargin);
    /* DEBUG 8 BIT. Needed to double test from 10 to 20 for change in coordinate
     * system */
    if (halfMargin > FixInt(20))
        halfMargin = FixInt(20);
    while (clrs != NULL) {
        seg = clrs->vSeg1;
        if (clrs->vGhst && seg->sType == sGHOST)
            seg = clrs->vSeg2;
        if (seg == NULL)
            goto Nxt;
        l0 = clrs->vLoc2;
        l1 = clrs->vLoc1;
        if (l0 > l1) {
            tmp = l1;
            l1 = l0;
            l0 = tmp;
        }
        l0 -= halfMargin;
        l1 += halfMargin;
        if (loc > l0 && loc < l1) {
            seglnk = seg->sLnk;
            seg->sLnk = clrs;
            if (vert) {
                if (TestColor(seg, gVColoring, true, true) == 1) {
                    ReportCarry(l0, l1, loc, clrs, vert);
                    AddVColoring(clrs);
                    seg->sLnk = seglnk;
                    break;
                }
            } else if (TestColor(seg, gHColoring, gYgoesUp, true) == 1) {
                ReportCarry(l0, l1, loc, clrs, vert);
                AddHColoring(clrs);
                seg->sLnk = seglnk;
                break;
            }
            seg->sLnk = seglnk;
        }
    Nxt:
        clrs = clrs->vNxt;
    }
}

#define PRODIST                                                                \
    (FixInt(100)) /* DEBUG 8 BIT. Needed to double test from 50 to 100 for     \
                     change in coordinate system */
static void
ProClrs(PPathElt e, bool hFlg, Fixed loc)
{
    PSegLnkLst lst;
    PPathElt prv;
    lst = ElmntClrSegLst(e, hFlg);
    if (lst == NULL)
        return;
    if (hFlg ? e->Hcopy : e->Vcopy)
        return;
    prv = e;
    while (true) {
        Fixed cx, cy, dst;
        PSegLnkLst plst;
        prv = GetSubPathPrv(prv);
        plst = ElmntClrSegLst(prv, hFlg);
        if (plst != NULL)
            return;
        GetEndPoint(prv, &cx, &cy);
        dst = (hFlg ? cy : cx) - loc;
        if (abs(dst) > PRODIST)
            return;
        if (hFlg) {
            prv->Hs = lst;
            prv->Hcopy = true;
        } else {
            prv->Vs = lst;
            prv->Vcopy = true;
        }
    }
}

static void
PromoteColors(void)
{
    PPathElt e;
    e = gPathStart;
    while (e != NULL) {
        Fixed cx, cy;
        GetEndPoint(e, &cx, &cy);
        ProClrs(e, true, cy);
        ProClrs(e, false, cx);
        e = e->next;
    }
}

static void
RemPromotedClrs(void)
{
    PPathElt e;
    e = gPathStart;
    while (e != NULL) {
        if (e->Hcopy) {
            e->Hs = NULL;
            e->Hcopy = false;
        }
        if (e->Vcopy) {
            e->Vs = NULL;
            e->Vcopy = false;
        }
        e = e->next;
    }
}

static void
RemShortColors(void)
{
    /* Must not change colors at a short element. */
    PPathElt e;
    Fixed cx, cy, ex, ey;
    e = gPathStart;
    cx = 0;
    cy = 0;
    while (e != NULL) {
        GetEndPoint(e, &ex, &ey);
        if (abs(cx - ex) < gMinColorElementLength &&
            abs(cy - ey) < gMinColorElementLength) {
            ReportRemShortColors(ex, ey);
            e->Hs = NULL;
            e->Vs = NULL;
        }
        e = e->next;
        cx = ex;
        cy = ey;
    }
}

void
AutoExtraColors(bool movetoNewClrs, bool soleol, int32_t solWhere)
{
    int32_t h, v, ph, pv;
    PPathElt e, cp, p;
    PSegLnkLst hLst, vLst, phLst, pvLst;
    PClrVal mtVclrs, mtHclrs, prvHclrs, prvVclrs;

    bool (*Tst)(int32_t, int32_t), newClrs = true;
    bool isSpc;
    Fixed x, y;

    isSpc = clrBBox = clrVBounds = clrHBounds = false;
    mergeMain = (CountSubPaths() <= 5);
    e = gPathStart;
    LogMsg(LOGDEBUG, OK, "RemFlares");
    RemFlares(true);
    RemFlares(false);
    LogMsg(LOGDEBUG, OK, "CheckElmntClrSegs");
    CheckElmntClrSegs();
    LogMsg(LOGDEBUG, OK, "PromoteColors");
    PromoteColors();
    LogMsg(LOGDEBUG, OK, "RemShortColors");
    RemShortColors();
    haveVBnds = clrVBounds;
    haveHBnds = clrHBounds;
    p = NULL;
    Tst = IsOk; /* it is ok to add to primary coloring */
    LogMsg(LOGDEBUG, OK, "color loop");
    mtVclrs = mtHclrs = NULL;
    while (e != NULL) {
        int32_t etype = e->type;
        if (movetoNewClrs && etype == MOVETO) {
            StartNewColoring(e, (PSegLnkLst)NULL, (PSegLnkLst)NULL);
            Tst = IsOk;
        }
        if (soleol && etype == MOVETO) { /* start new coloring on soleol mt */
            if ((solWhere == 1 && IsUpper(e)) ||
                (solWhere == -1 && IsLower(e)) ||
                (solWhere == 0)) { /* color bbox of next subpath */
                StartNewColoring(e, (PSegLnkLst)NULL, (PSegLnkLst)NULL);
                Tst = IsOk;
                haveHBnds = haveVBnds = isSpc = true;
                e = ColorBBox(e);
                continue;
            } else if (isSpc) { /* new coloring after the special */
                StartNewColoring(e, (PSegLnkLst)NULL, (PSegLnkLst)NULL);
                Tst = IsOk;
                haveHBnds = haveVBnds = isSpc = false;
            }
        }
        if (newClrs && e == p) {
            StartNewColoring(e, (PSegLnkLst)NULL, (PSegLnkLst)NULL);
            SetHColors(mtHclrs);
            SetVColors(mtVclrs);
            Tst = IsIn;
        }
        GetColorLsts(e, &hLst, &vLst, &h, &v);
        if (etype == MOVETO && IsShort(cp = GetClosedBy(e))) {
            GetColorLsts(p = cp->prev, &phLst, &pvLst, &ph, &pv);
            if (ClrsClash(e, p, &hLst, &vLst, &phLst, &pvLst)) {
                GetColorLsts(e, &hLst, &vLst, &h, &v);
                GetColorLsts(p, &phLst, &pvLst, &ph, &pv);
            }
            if (!(*Tst)(ph, pv) || !(*Tst)(h, v)) {
                StartNewColoring(e, hLst, vLst);
                Tst = IsOk;
                ph = pv = 1; /* force add of colors for p also */
            } else {
                AddIfNeedH(h, hLst);
                AddIfNeedV(v, vLst);
            }
            AddIfNeedH(ph, phLst);
            AddIfNeedV(pv, pvLst);
            newClrs = false; /* so can tell if added new colors in subpath */
        } else if (!(*Tst)(h, v)) { /* e needs new coloring */
            if (etype ==
                CLOSEPATH) { /* do not attach extra colors to closepath */
                e = e->prev;
                GetColorLsts(e, &hLst, &vLst, &h, &v);
            }
            prvHclrs = CopyClrs(gHColoring);
            prvVclrs = CopyClrs(gVColoring);
            if (!newClrs) { /* this is the first extra since mt */
                newClrs = true;
                mtVclrs = CopyClrs(prvVclrs);
                mtHclrs = CopyClrs(prvHclrs);
            }
            StartNewColoring(e, hLst, vLst);
            Tst = IsOk;
            if (etype == CURVETO) {
                x = e->x1;
                y = e->y1;
            } else
                GetEndPoint(e, &x, &y);
            CarryIfNeed(y, false, prvHclrs);
            CarryIfNeed(x, true, prvVclrs);
        } else { /* do not need to start new coloring */
            AddIfNeedH(h, hLst);
            AddIfNeedV(v, vLst);
        }
        e = e->next;
    }
    ReClrBounds(gPathEnd);
    LogMsg(LOGDEBUG, OK, "RemPromotedClrs");
    RemPromotedClrs();
    LogMsg(LOGDEBUG, OK, "done autoextracolors");
}
