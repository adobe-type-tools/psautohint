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

static bool mergeMain;

static PathElt*
GetSubPathNxt(PathElt* e)
{
    if (e->type == CLOSEPATH)
        return GetDest(e);
    return e->next;
}

static PathElt*
GetSubPathPrv(PathElt* e)
{
    if (e->type == MOVETO)
        e = GetClosedBy(e);
    return e->prev;
}

static HintVal*
FindClosestVal(HintVal* sLst, Fixed loc)
{
    Fixed dist = FixInt(10000);
    HintVal* best = NULL;
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
CpyHHint(PathElt* e)
{
    Fixed x1, y1;
    HintVal* best;
    GetEndPoint(e, &x1, &y1);
    best = FindClosestVal(gHPrimary, y1);
    if (best != NULL)
        AddHPair(best, 'b');
}

static void
CpyVHint(PathElt* e)
{
    Fixed x1, y1;
    HintVal* best;
    GetEndPoint(e, &x1, &y1);
    best = FindClosestVal(gVPrimary, x1);
    if (best != NULL)
        AddVPair(best, 'y');
}

static void
PruneHintSegs(PathElt* e, bool hFlg)
{
    SegLnkLst *lst, *nxt, *prv;
    HintSeg* seg;
    lst = hFlg ? e->Hs : e->Vs;
    prv = NULL;
    while (lst != NULL) {
        HintVal* val = NULL;
        SegLnk* lnk = lst->lnk;
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
PruneElementHintSegs(void)
{
    PathElt* e;
    e = gPathStart;
    while (e != NULL) {
        PruneHintSegs(e, true);
        PruneHintSegs(e, false);
        e = e->next;
    }
}

#define ElmntHintSegLst(e, hFlg) (hFlg) ? (e)->Hs : (e)->Vs

static void
RemLnk(PathElt* e, bool hFlg, SegLnkLst* rm)
{
    SegLnkLst *lst, *prv, *nxt;
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
    LogMsg(LOGERROR, NONFATALERROR, "Badly formatted segment list.");
}

static bool
AlreadyOnList(HintVal* v, HintVal* lst)
{
    while (lst != NULL) {
        if (v == lst)
            return true;
        lst = lst->vNxt;
    }
    return false;
}

static void
AutoVSeg(HintVal* sLst)
{
    AddVPair(sLst, 'y');
}

static void
AutoHSeg(HintVal* sLst)
{
    AddHPair(sLst, 'b');
}

static void
AddHHinting(HintVal* h)
{
    if (gUseH || AlreadyOnList(h, gHHinting))
        return;
    h->vNxt = gHHinting;
    gHHinting = h;
    AutoHSeg(h);
}

static void
AddVHinting(HintVal* v)
{
    if (gUseV || AlreadyOnList(v, gVHinting))
        return;
    v->vNxt = gVHinting;
    gVHinting = v;
    AutoVSeg(v);
}

static int32_t
TestHint(HintSeg* s, HintVal* hintList, bool flg, bool doLst)
{
    /* -1 means already in hintList; 0 means conflicts; 1 means ok to add */
    HintVal *v, *clst;
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
        clst = hintList;
        while (clst != NULL) {
            if (++cnt > 100) {
                LogMsg(LOGDEBUG, OK, "Loop in hintlist for TestHint.");
                return 0;
            }
            clst = clst->vNxt;
        }
    }
    if (v->vGhst) {
        bool loc1;
        /* if best value for segment uses a ghost, and
           segment loc is already in the hintList, then return -1 */
        clst = hintList;
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
    while (hintList != NULL) { /* check for conflict */
        Fixed cTop = hintList->vLoc2;
        Fixed cBot = hintList->vLoc1;
        if (vB == cBot && vT == cTop) {
            return -1;
        }
        if (hintList->vGhst) { /* collapse width for conflict test */
            if (hintList->vSeg1->sType == sGHOST)
                cBot = cTop;
            else
                cTop = cBot;
        }
        if ((flg && (cBot <= top) && (cTop >= bot)) ||
            (!flg && (cBot >= top) && (cTop <= bot))) {
            return 0;
        }
        hintList = hintList->vNxt;
        if (!doLst)
            break;
    }
    return 1;
}

#define TestHHintLst(h) TestHintLst(h, gHHinting, false, true)
#define TestVHintLst(v) TestHintLst(v, gVHinting, true, true)

int
TestHintLst(SegLnkLst* lst, HintVal* hintList, bool flg, bool doLst)
{
    /* -1 means already in hintList; 0 means conflicts; 1 means ok to add */
    int result, cnt;
    result = -1;
    cnt = 0;
    while (lst != NULL) {
        int i = TestHint(lst->lnk->seg, hintList, flg, doLst);
        if (i == 0) {
            result = 0;
            break;
        }
        if (i == 1)
            result = 1;
        lst = lst->next;
        if (++cnt > 100) {
            LogMsg(WARNING, OK, "Looping in TestHintLst.");
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
ResolveConflictBySplit(PathElt* e, bool Hflg, SegLnkLst* lnk1, SegLnkLst* lnk2)
{
    /* insert new pathelt immediately following e */
    /* e gets first half of split; new gets second */
    /* e gets lnk1 in Hs or Vs; new gets lnk2 */
    PathElt* new;
    Cd d0, d1, d2, d3, d4, d5, d6, d7;
    if (e->type != CURVETO || e->isFlex)
        return false;
    ReportSplit(e);
    new = (PathElt*)Alloc(sizeof(PathElt));
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
RemDupLnks(PathElt* e, bool Hflg)
{
    SegLnkLst *l1, *l2, *l2nxt;
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
   The top left point was not getting hinted. */
static bool
TryResolveConflict(PathElt* e, bool Hflg)
{
    int32_t typ;
    SegLnkLst *lst, *lnk1, *lnk2;
    HintSeg *seg, *seg1, *seg2;
    HintVal *val1, *val2;
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
    if (lst == NULL)
        return false;
    seg2 = lst->lnk->seg;
    lc2 = seg2->sLoc;
    lnk2 = lst;
    if (lc1 == loc1 || lc2 == loc2) {
        /* do nothing */
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
    if (gEditGlyph && ResolveConflictBySplit(e, Hflg, lnk1, lnk2))
        return true;
    else
        return false;
}

static bool
CheckHintSegs(PathElt* e, bool flg, bool Hflg)
{
    SegLnkLst* lst;
    SegLnkLst* lst2;
    HintSeg* seg;
    HintVal* val;
    lst = Hflg ? e->Hs : e->Vs;
    while (lst != NULL) {
        lst2 = lst->next;
        if (lst2 != NULL) {
            seg = lst->lnk->seg;
            val = seg->sLnk;
            if (val != NULL && TestHintLst(lst2, val, flg, false) == 0) {
                if (TryResolveConflict(e, Hflg))
                    return CheckHintSegs(e, flg, Hflg);
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
CheckElmntHintSegs(void)
{
    PathElt* e;
    e = gPathStart;
    while (e != NULL) {
        if (!CheckHintSegs(e, false, true))
            CheckHintSegs(e, true, false);
        e = e->next;
    }
}
static bool
HintLstsClash(SegLnkLst* lst1, SegLnkLst* lst2, bool flg)
{
    while (lst1 != NULL) {
        HintSeg* seg = lst1->lnk->seg;
        HintVal* val = seg->sLnk;
        if (val != NULL) {
            SegLnkLst* lst = lst2;
            while (lst != NULL) {
                if (TestHintLst(lst, val, flg, false) == 0) {
                    return true;
                }
                lst = lst->next;
            }
        }
        lst1 = lst1->next;
    }
    return false;
}

static SegLnkLst*
BestFromLsts(SegLnkLst* lst1, SegLnkLst* lst2)
{
    SegLnkLst* bst = NULL;
    Fixed bstval = 0;
    int32_t i;
    for (i = 0; i < 2; i++) {
        SegLnkLst* lst = i ? lst1 : lst2;
        while (lst != NULL) {
            HintSeg* seg = lst->lnk->seg;
            HintVal* val = seg->sLnk;
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
HintsClash(PathElt* e, PathElt* p, SegLnkLst** hLst, SegLnkLst** vLst,
           SegLnkLst** phLst, SegLnkLst** pvLst)
{
    bool clash = false;
    SegLnkLst *bst, *new;
    if (HintLstsClash(*hLst, *phLst, false)) {
        clash = true;
        bst = BestFromLsts(*hLst, *phLst);
        if (bst) {
            new = (SegLnkLst*)Alloc(sizeof(SegLnkLst));
            new->next = NULL;
            new->lnk = bst->lnk;
        } else
            new = NULL;
        e->Hs = p->Hs = *hLst = *phLst = new;
    }
    if (HintLstsClash(*vLst, *pvLst, true)) {
        clash = true;
        bst = BestFromLsts(*vLst, *pvLst);
        if (bst) {
            new = (SegLnkLst*)Alloc(sizeof(SegLnkLst));
            new->next = NULL;
            new->lnk = bst->lnk;
        } else
            new = NULL;
        e->Vs = p->Vs = *vLst = *pvLst = new;
    }
    return clash;
}

static void
GetHintLsts(PathElt* e, SegLnkLst** phLst, SegLnkLst** pvLst, int32_t* ph,
            int32_t* pv)
{
    SegLnkLst *hLst, *vLst;
    int32_t h, v;
    if (gUseH) {
        hLst = NULL;
        h = -1;
    } else {
        hLst = e->Hs;
        if (hLst == NULL)
            h = -1;
        else
            h = TestHHintLst(hLst);
    }
    if (gUseV) {
        vLst = NULL;
        v = -1;
    } else {
        vLst = e->Vs;
        if (vLst == NULL)
            v = -1;
        else
            v = TestVHintLst(vLst);
    }
    *pvLst = vLst;
    *phLst = hLst;
    *ph = h;
    *pv = v;
}

static void
ReHintBounds(PathElt* e)
{
    if (!gUseH) {
        if (gHHinting == NULL)
            CpyHHint(e);
        if (mergeMain)
            MergeFromMainHints('b');
    }
    if (!gUseV) {
        if (gVHinting == NULL)
            CpyVHint(e);
        if (mergeMain)
            MergeFromMainHints('y');
    }
}

static void
AddHintLst(SegLnkLst* lst, bool vert)
{
    while (lst != NULL) {
        HintSeg* seg = lst->lnk->seg;
        HintVal* val = seg->sLnk;
        if (vert)
            AddVHinting(val);
        else
            AddHHinting(val);
        lst = lst->next;
    }
}

static void
StartNewHinting(PathElt* e, SegLnkLst* hLst, SegLnkLst* vLst)
{
    ReHintBounds(e);
    if (e->newhints != 0) {
        LogMsg(LOGERROR, NONFATALERROR, "Uninitialized extra hints list.");
    }
    XtraHints(e);
    if (gUseV)
        CopyMainV();
    if (gUseH)
        CopyMainH();
    gHHinting = gVHinting = NULL;
    if (!gUseH)
        AddHintLst(hLst, false);
    if (!gUseV)
        AddHintLst(vLst, true);
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
    AddHintLst(vLst, true)
#define AddIfNeedH(h, hLst)                                                    \
    if (!gUseH && h == 1)                                                      \
    AddHintLst(hLst, false)

static void
SetHHints(HintVal* lst)
{
    if (gUseH)
        return;
    gHHinting = lst;
    while (lst != NULL) {
        AutoHSeg(lst);
        lst = lst->vNxt;
    }
}

static void
SetVHints(HintVal* lst)
{
    if (gUseV)
        return;
    gVHinting = lst;
    while (lst != NULL) {
        AutoVSeg(lst);
        lst = lst->vNxt;
    }
}

HintVal*
CopyHints(HintVal* lst)
{
    HintVal* vlst;
    int cnt;
    vlst = NULL;
    cnt = 0;
    while (lst != NULL) {
        HintVal* v = (HintVal*)Alloc(sizeof(HintVal));
        *v = *lst;
        v->vNxt = vlst;
        vlst = v;
        if (++cnt > 100) {
            LogMsg(WARNING, OK, "Loop in CopyHints.");
            return vlst;
        }
        lst = lst->vNxt;
    }
    return vlst;
}

static bool
IsFlare(Fixed loc, PathElt* e, PathElt* n, bool Hflg)
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
RemFlareLnk(PathElt* e, bool hFlg, SegLnkLst* rm, PathElt* e2, int32_t i)
{
    RemLnk(e, hFlg, rm);
    ReportRemFlare(e, e2, hFlg, i);
}

bool
CompareValues(HintVal* val1, HintVal* val2, int32_t factor, int32_t ghstshift)
{
    Fixed v1 = val1->vVal, v2 = val2->vVal, mx;
    mx = NUMMAX(v1, v2);
    while (mx < FIXED_MAX / 2) {
        mx *= 2;
        v1 *= 2;
        v2 *= 2;
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
        return (v1 < FIXED_MAX / factor) ? (v1 * factor > v2)
                                         : (v1 > v2 / factor);
    return (v2 < FIXED_MAX / factor) ? (v1 > v2 * factor) : (v1 / factor > v2);
}

static void
RemFlares(bool Hflg)
{
    SegLnkLst *lst1, *lst2, *nxt1, *nxt2;
    PathElt *e, *n;
    HintSeg *seg1, *seg2;
    HintVal *val1, *val2;
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
                lst1 = ElmntHintSegLst(e, Nm1);
                while (lst1 != NULL) {
                    seg1 = lst1->lnk->seg;
                    nxt1 = lst1->next;
                    lst2 = ElmntHintSegLst(n, Nm1);
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
CarryIfNeed(Fixed loc, bool vert, HintVal* hints)
{
    HintSeg* seg;
    HintVal* seglnk;
    Fixed l0, l1, tmp, halfMargin;
    if ((vert && gUseV) || (!vert && gUseH))
        return;
    halfMargin = FixHalfMul(gBandMargin);
    /* DEBUG 8 BIT. Needed to double test from 10 to 20 for change in coordinate
     * system */
    if (halfMargin > FixInt(20))
        halfMargin = FixInt(20);
    while (hints != NULL) {
        seg = hints->vSeg1;
        if (hints->vGhst && seg->sType == sGHOST)
            seg = hints->vSeg2;
        if (seg == NULL)
            goto Nxt;
        l0 = hints->vLoc2;
        l1 = hints->vLoc1;
        if (l0 > l1) {
            tmp = l1;
            l1 = l0;
            l0 = tmp;
        }
        l0 -= halfMargin;
        l1 += halfMargin;
        if (loc > l0 && loc < l1) {
            seglnk = seg->sLnk;
            seg->sLnk = hints;
            if (vert) {
                if (TestHint(seg, gVHinting, true, true) == 1) {
                    ReportCarry(l0, l1, loc, hints, vert);
                    AddVHinting(hints);
                    seg->sLnk = seglnk;
                    break;
                }
            } else if (TestHint(seg, gHHinting, false, true) == 1) {
                ReportCarry(l0, l1, loc, hints, vert);
                AddHHinting(hints);
                seg->sLnk = seglnk;
                break;
            }
            seg->sLnk = seglnk;
        }
    Nxt:
        hints = hints->vNxt;
    }
}

#define PRODIST                                                                \
    (FixInt(100)) /* DEBUG 8 BIT. Needed to double test from 50 to 100 for     \
                     change in coordinate system */
static void
ProHints(PathElt* e, bool hFlg, Fixed loc)
{
    SegLnkLst* lst;
    PathElt* prv;
    lst = ElmntHintSegLst(e, hFlg);
    if (lst == NULL)
        return;
    if (hFlg ? e->Hcopy : e->Vcopy)
        return;
    prv = e;
    while (true) {
        Fixed cx, cy, dst;
        SegLnkLst* plst;
        prv = GetSubPathPrv(prv);
        plst = ElmntHintSegLst(prv, hFlg);
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
PromoteHints(void)
{
    PathElt* e;
    e = gPathStart;
    while (e != NULL) {
        Fixed cx, cy;
        GetEndPoint(e, &cx, &cy);
        ProHints(e, true, cy);
        ProHints(e, false, cx);
        e = e->next;
    }
}

static void
RemPromotedHints(void)
{
    PathElt* e;
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
RemShortHints(void)
{
    /* Must not change hints at a short element. */
    PathElt* e;
    Fixed cx, cy, ex, ey;
    e = gPathStart;
    cx = 0;
    cy = 0;
    while (e != NULL) {
        GetEndPoint(e, &ex, &ey);
        if (abs(cx - ex) < gMinHintElementLength &&
            abs(cy - ey) < gMinHintElementLength) {
            ReportRemShortHints(ex, ey);
            e->Hs = NULL;
            e->Vs = NULL;
        }
        e = e->next;
        cx = ex;
        cy = ey;
    }
}

void
AutoExtraHints(bool movetoNewHints)
{
    int32_t h, v, ph, pv;
    PathElt *e, *cp, *p;
    SegLnkLst *hLst, *vLst, *phLst, *pvLst;
    HintVal *mtVhints, *mtHhints, *prvHhints, *prvVhints;

    bool (*Tst)(int32_t, int32_t), newHints = true;
    Fixed x, y;

    mergeMain = (CountSubPaths() <= 5);
    e = gPathStart;
    LogMsg(LOGDEBUG, OK, "RemFlares");
    RemFlares(true);
    RemFlares(false);
    LogMsg(LOGDEBUG, OK, "CheckElmntHintSegs");
    CheckElmntHintSegs();
    LogMsg(LOGDEBUG, OK, "PromoteHints");
    PromoteHints();
    LogMsg(LOGDEBUG, OK, "RemShortHints");
    RemShortHints();
    p = NULL;
    Tst = IsOk; /* it is ok to add to primary hinting */
    LogMsg(LOGDEBUG, OK, "hint loop");
    mtVhints = mtHhints = NULL;
    while (e != NULL) {
        int32_t etype = e->type;
        if (movetoNewHints && etype == MOVETO) {
            StartNewHinting(e, NULL, NULL);
            Tst = IsOk;
        }
        if (newHints && e == p) {
            StartNewHinting(e, NULL, NULL);
            SetHHints(mtHhints);
            SetVHints(mtVhints);
            Tst = IsIn;
        }
        GetHintLsts(e, &hLst, &vLst, &h, &v);
        if (etype == MOVETO && IsShort(cp = GetClosedBy(e))) {
            GetHintLsts(p = cp->prev, &phLst, &pvLst, &ph, &pv);
            if (HintsClash(e, p, &hLst, &vLst, &phLst, &pvLst)) {
                GetHintLsts(e, &hLst, &vLst, &h, &v);
                GetHintLsts(p, &phLst, &pvLst, &ph, &pv);
            }
            if (!(*Tst)(ph, pv) || !(*Tst)(h, v)) {
                StartNewHinting(e, hLst, vLst);
                Tst = IsOk;
                ph = pv = 1; /* force add of hints for p also */
            } else {
                AddIfNeedH(h, hLst);
                AddIfNeedV(v, vLst);
            }
            AddIfNeedH(ph, phLst);
            AddIfNeedV(pv, pvLst);
            newHints = false; /* so can tell if added new hints in subpath */
        } else if (!(*Tst)(h, v)) { /* e needs new hinting */
            if (etype ==
                CLOSEPATH) { /* do not attach extra hints to closepath */
                e = e->prev;
                GetHintLsts(e, &hLst, &vLst, &h, &v);
            }
            prvHhints = CopyHints(gHHinting);
            prvVhints = CopyHints(gVHinting);
            if (!newHints) { /* this is the first extra since mt */
                newHints = true;
                mtVhints = CopyHints(prvVhints);
                mtHhints = CopyHints(prvHhints);
            }
            StartNewHinting(e, hLst, vLst);
            Tst = IsOk;
            if (etype == CURVETO) {
                x = e->x1;
                y = e->y1;
            } else
                GetEndPoint(e, &x, &y);
            CarryIfNeed(y, false, prvHhints);
            CarryIfNeed(x, true, prvVhints);
        } else { /* do not need to start new hinting */
            AddIfNeedH(h, hLst);
            AddIfNeedV(v, vLst);
        }
        e = e->next;
    }
    ReHintBounds(gPathEnd);
    LogMsg(LOGDEBUG, OK, "RemPromotedHints");
    RemPromotedHints();
    LogMsg(LOGDEBUG, OK, "done AutoExtraHints");
}
