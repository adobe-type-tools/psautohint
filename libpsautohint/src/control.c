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

static void DoHStems(HintVal* sLst1);
static void DoVStems(HintVal* sLst);

static bool CounterFailed;

void
InitAll(int32_t reason)
{
    InitData(reason); /* must be first */
    InitFix(reason);
    InitGen(reason);
    InitPick(reason);
}

static int32_t
PtLstLen(HintPoint* lst)
{
    int32_t cnt = 0;
    while (lst != NULL) {
        cnt++;
        lst = lst->next;
    }
    return cnt;
}

static int32_t
PointListCheck(HintPoint* new, HintPoint* lst)
{
    /* -1 means not a member, 1 means already a member, 0 means conflicts */
    Fixed l1 = 0, l2 = 0, n1 = 0, n2 = 0, tmp, halfMargin;
    char ch = new->c;
    halfMargin = FixHalfMul(gBandMargin);
    halfMargin = FixHalfMul(halfMargin);
    /* DEBUG 8 BIT. In the previous version, with 7 bit fraction coordinates
    instead of the current
    8 bit, bandMargin is declared as 30, but scaled by half to match the 7 bit
    fraction coordinate -> a value of 15.
    In the current version this scaling doesn't happen. However, in this part of
    the code, the hint values are scaled up to 8 bits of fraction even in the
    original version, but topBand is applied without correcting for the scaling
    difference. In this version  I need to divide by half again in order to get
    to the same value. I think the original is a bug, but it has been working
    for 30 years, so I am not going to change the test now.
     */
    switch (ch) {
        case 'y':
        case 'm': {
            n1 = new->x0;
            n2 = new->x1;
            break;
        }
        case 'b':
        case 'v': {
            n1 = new->y0;
            n2 = new->y1;
            break;
        }
        default: {
            LogMsg(LOGERROR, NONFATALERROR, "Illegal character in point list.");
        }
    }
    if (n1 > n2) {
        tmp = n1;
        n1 = n2;
        n2 = tmp;
    }
    while (true) {
        if (lst == NULL) {
            return -1;
        }
        if (lst->c == ch) { /* same kind of hint */
            switch (ch) {
                case 'y':
                case 'm': {
                    l1 = lst->x0;
                    l2 = lst->x1;
                    break;
                }
                case 'b':
                case 'v': {
                    l1 = lst->y0;
                    l2 = lst->y1;
                    break;
                }
            }
            if (l1 > l2) {
                tmp = l1;
                l1 = l2;
                l2 = tmp;
            }
            if (l1 == n1 && l2 == n2) {
                return 1;
            }
            /* Add this extra margin to the band to fix a problem in
             TimesEuropa/Italic/v,w,y where a main hstem hint was
             being merged with newhints. This main hstem caused
             problems in rasterization so it shouldn't be included. */
            l1 -= halfMargin;
            l2 += halfMargin;
            if (l1 <= n2 && n1 <= l2) {
                return 0;
            }
        }
        lst = lst->next;
    }
}

static bool
SameHintLists(HintPoint* lst1, HintPoint* lst2)
{
    if (PtLstLen(lst1) != PtLstLen(lst2)) {
        return false;
    }
    while (lst1 != NULL) { /* go through lst1 */
        if (PointListCheck(lst1, lst2) != 1) {
            return false;
        }
        lst1 = lst1->next;
    }
    return true;
}

bool
SameHints(int32_t cn1, int32_t cn2)
{
    if (cn1 == cn2) {
        return true;
    }
    return SameHintLists(gPtLstArray[cn1], gPtLstArray[cn2]);
}

void
MergeFromMainHints(char ch)
{
    HintPoint* lst;
    for (lst = gPtLstArray[0]; lst != NULL; lst = lst->next) {
        if (lst->c != ch) {
            continue;
        }
        if (PointListCheck(lst, gPointList) == -1) {
            if (ch == 'b') {
                AddHintPoint(0, lst->y0, 0, lst->y1, ch, lst->p0, lst->p1);
            } else {
                AddHintPoint(lst->x0, 0, lst->x1, 0, ch, lst->p0, lst->p1);
            }
        }
    }
}

void
AddHintPoint(Fixed x0, Fixed y0, Fixed x1, Fixed y1, char ch, PathElt* p0,
             PathElt* p1)
{
    HintPoint* pt;
    int32_t chk;
    pt = (HintPoint*)Alloc(sizeof(HintPoint));
    pt->x0 = x0;
    pt->y0 = y0;
    pt->x1 = x1;
    pt->y1 = y1;
    pt->c = ch;
    pt->done = false;
    pt->next = NULL;
    pt->p0 = p0;
    pt->p1 = p1;
    chk = PointListCheck(pt, gPointList);
    if (chk == 0) {
        ReportHintConflict(x0, y0, x1, y1, ch);
    }
    if (chk == -1) {
        pt->next = gPointList;
        gPointList = pt;
        LogHintInfo(gPointList);
    }
}

static void
CopyHintFromLst(char hint, HintPoint* lst)
{
    bool bvflg = (hint == 'b' || hint == 'v');
    while (lst != NULL) {
        if (lst->c == hint) {
            if (bvflg) {
                AddHintPoint(0, lst->y0, 0, lst->y1, hint, lst->p0, lst->p1);
            } else {
                AddHintPoint(lst->x0, 0, lst->x1, 0, hint, lst->p0, lst->p1);
            }
        }
        lst = lst->next;
    }
}

void
CopyMainV(void)
{
    CopyHintFromLst('m', gPtLstArray[0]);
}

void
CopyMainH(void)
{
    CopyHintFromLst('v', gPtLstArray[0]);
}

void
AddHPair(HintVal* v, char ch)
{
    Fixed bot, top;
    PathElt *p0, *p1, *p;
    bot = -v->vLoc1;
    top = -v->vLoc2;
    p0 = v->vBst->vSeg1->sElt;
    p1 = v->vBst->vSeg2->sElt;
    if (top < bot) {
        Fixed tmp = top;
        top = bot;
        bot = tmp;
        p = p0;
        p0 = p1;
        p1 = p;
    }
    if (v->vGhst) {
        if (v->vSeg1->sType == sGHOST) {
            bot = top;
            p0 = p1;
            p1 = NULL;
            top = bot - FixInt(20); /* width == -20 iff bottom seg is ghost */
        } else {
            top = bot;
            p1 = p0;
            p0 = NULL;
            bot = top + FixInt(21); /* width == -21 iff top seg is ghost */
        }
    }
    AddHintPoint(0, bot, 0, top, ch, p0, p1);
}

void
AddVPair(HintVal* v, char ch)
{
    Fixed lft, rght;
    PathElt *p0, *p1, *p;
    lft = v->vLoc1;
    rght = v->vLoc2;
    p0 = v->vBst->vSeg1->sElt;
    p1 = v->vBst->vSeg2->sElt;
    if (lft > rght) {
        Fixed tmp = lft;
        lft = rght;
        rght = tmp;
        p = p0;
        p0 = p1;
        p1 = p;
    }
    AddHintPoint(lft, 0, rght, 0, ch, p0, p1);
}

static bool
UseCounter(HintVal* sLst, bool mhint)
{
    int32_t cnt = 0;
    Fixed minLoc, midLoc, maxLoc, prevBstVal, bestVal;
    Fixed minDelta, midDelta, maxDelta, th;
    HintVal *lst, *newLst;
    minLoc = midLoc = maxLoc = FixInt(20000);
    minDelta = midDelta = maxDelta = 0;
    lst = sLst;
    while (lst != NULL) {
        cnt++;
        lst = lst->vNxt;
    }
    if (cnt < 3) {
        return false;
    }
    cnt -= 3;
    prevBstVal = 0;
    while (cnt > 0) {
        cnt--;
        if (cnt == 0) {
            prevBstVal = sLst->vVal;
        }
        sLst = sLst->vNxt;
    }
    bestVal = sLst->vVal;
    if (prevBstVal > FixInt(1000) || bestVal < prevBstVal * 10) {
        return false;
    }
    newLst = sLst;
    while (sLst != NULL) {
        Fixed loc = sLst->vLoc1;
        Fixed delta = sLst->vLoc2 - loc;
        loc += FixHalfMul(delta);
        if (loc < minLoc) {
            maxLoc = midLoc;
            maxDelta = midDelta;
            midLoc = minLoc;
            midDelta = minDelta;
            minLoc = loc;
            minDelta = delta;
        } else if (loc < midLoc) {
            maxLoc = midLoc;
            maxDelta = midDelta;
            midLoc = loc;
            midDelta = delta;
        } else {
            maxLoc = loc;
            maxDelta = delta;
        }
        sLst = sLst->vNxt;
    }
    th = FixInt(5) / 100;
    if (abs(minDelta - maxDelta) < th &&
        abs((maxLoc - midLoc) - (midLoc - minLoc)) < th) {
        if (mhint) {
            gVHinting = newLst;
        } else {
            gHHinting = newLst;
        }
        return true;
    }
    if (abs(minDelta - maxDelta) < FixInt(3) &&
        abs((maxLoc - midLoc) - (midLoc - minLoc)) < FixInt(3)) {
        LogMsg(INFO, OK,
               mhint ? "Near miss for using V counter hinting."
                     : "Near miss for using H counter hinting.");
    }
    return false;
}

static void
GetNewPtLst(void)
{
    if (gNumPtLsts >= gMaxPtLsts) { /* increase size */
        HintPoint** newArray;
        int32_t i;
        int32_t newSize = gMaxPtLsts * 2;
        newArray = (HintPoint**)Alloc(newSize * sizeof(HintPoint*));
        for (i = 0; i < gMaxPtLsts; i++) {
            newArray[i] = gPtLstArray[i];
        }
        gPtLstArray = newArray;
        gMaxPtLsts = newSize;
    }
    gPtLstIndex = gNumPtLsts;
    gNumPtLsts++;
    gPointList = NULL;
    gPtLstArray[gPtLstIndex] = NULL;
}

void
XtraHints(PathElt* e)
{
    /* this can be simplified for standalone hinting */
    gPtLstArray[gPtLstIndex] = gPointList;
    if (e->newhints == 0) {
        GetNewPtLst();
        e->newhints = (int16_t)gPtLstIndex;
    }
    gPtLstIndex = e->newhints;
    gPointList = gPtLstArray[gPtLstIndex];
}

static void
Blues(unsigned char* links)
{
    Fixed pv = 0, pd = 0, pc = 0, pb = 0, pa = 0;
    HintVal* sLst;

    /*
     * Top alignment zones are in the global 'gTopBands', bottom in
     * 'gBotBands'.
     *
     * This function looks through the path, as defined by the linked list of
     * PathElt's, starting at the global 'gPathStart', and adds to several
     * lists.  Coordinates are stored in the PathElt.(x,y) as (original
     * value)/2.0, aka right shifted by 1 bit from the original 24.8 Fixed. I
     * suspect that is to allow a larger integer portion - when this program
     * was written, an int was 16 bits.
     *
     * 'gHStems' and 'gVStems' are global arrays of Fixed 24.8 numbers..
     *
     * 'gSegLists' is an array of 4 HintSeg linked lists. List 0 and 1 are
     * respectively up and down vertical segments. Lists 2 and 3 are
     * respectively left pointing and right pointing horizontal segments. On a
     * counter-clockwise path, this is the same as selecting top and bottom
     * stem locations.
     *
     * NoBlueGlyph() consults a hard-coded list of glyph names, if the glyph is
     * in this list, set the alignment zones ('gTopBands' and 'gBotBands') to
     * empty.
     *
     * 1) gen.c:GenHPts()
     *    Builds the raw list of stem segments in global
     *    'topList' and 'botList'. It steps through the liked list of path
     *    segments, starting at 'gPathStart'. It decides if a path is mostly H,
     *    and if so, adds it to a linked list of hstem candidates in gSegLists,
     *    by calling gen.c:AddHSegment(). This calls ReportAddHSeg() (useful in
     *    debugging), and then gen.c:AddSegment().
     *
     *    If the path segment is in fact entirely vertical and is followed by a
     *    sharp bend, gen.c:GenHPts() adds two new path segments just 1 unit
     *    long, after the segment end point, called H/VBends (segment type
     *    sBend=1). I have no idea what these are for.
     *
     *    AddSegment() is pretty simple. It creates a new hint segment
     *    (HintSeg) for the parent PathElt, fills it in, adds it to appropriate
     *    list of the 4 gSegLists, and then sorts by hstem location. seg->sElt
     *    is the parent PathElt, seg->sType is the type, seg->sLoc is the
     *    location in Fixed 18.14: right shift 7 to get integer value.
     *
     *    If the current PathElt is a Closepath, It also calls LinkSegment() to
     *    add the current stem segment to the list of stem segments referenced
     *    by this elt's e->Hs/Vs.
     *
     *    Note that a hint segment is created for each nearly vertical or
     *    horizontal PathElt. This means that in an H, there will be two hint
     *    segments created for the bottom and top of the H, as there are two
     *    horizontal paths with the same Y at the top and bottom of the H.
     *
     *    Assign the top and bottom Hstem location lists.
     *    topList = segLists[2]
     *    botList = segLists[3];
     *
     * 2) eval.c::EvalH()
     *    Evaluates every combination of botList and topList, and assign a
     *    priority value and a 'Q' value.
     *
     *    For each bottom stem
     *    for each top stem
     *    1) assign priority (spc) and weight (val) values with EvalHPair()
     *    2) report stem near misses  in the 'HStems' list with HStemMiss()
     *    3) decide whether to add pair to 'HStems' list with AddHValue()
     *
     *     Add ghost hints.
     *     For each bottom stem segment and then for each top stem segment:
     *     if it is in an alignment zone, make a ghost hint segment and add it
     *     with AddHValue().
     *
     *     EvalHPair() sets priority (spc) and weight (val) values.
     *       Omit pair by setting value to 0 if:
     *         bottom is in bottom alignment zone, and top is in top alignment
     *         zone. (otherwise, these will override the ghost hints).
     *
     *       Boost priority by +2 if either the bot or top segment is in an
     *       alignment zone.
     *
     *       dy = stem width ( top - bot)
     *
     *       Calculate dist. Dist is set to a fudge factor * dy.
     *         if bottom segment xo->x1 overlaps top x0->x1, the fudge factor is
     *         1.0. The less the overlap, the larger the fduge factor.
     *         if bottom segment xo->x1 overlaps top x0->x1:.
     *           if  top and bottom overlap exactly, dist = dy
     *           if they barely overlap, dist = 1.4*dy
     *           in between, interpolate.
     *         else, look at closest ends betwen bottom and top segments.
     *           dx = min X separation between top and bottom segments.
     *           dist = 1.4 *dy
     *           dist += dx*dx
     *           if dx > dy:
     *             dist *= dx / dy;
     *
     *       Look through the gHStems global list. For each match to dy, boost
     *       priority by +1.
     *
     *       Calculate weight with gen.c:AdjustVal()
     *         if dy is more than twice the 1.1.5* the largest hint in gHStems,
     *         set weight to 0.
     *         Calculate weight as related to length of the segments squared
     *         divided by the distance squared.
     *         Basically, the greater the ratio segment overlap to stem width,
     *         the higher the value.
     *         if dy is greater than the largest stem hint in gHStems, decrease
     *         the value scale weight by  of * (largest stem hint in
     *         gHStems)/dy)**3.
     *
     *     AddHValue() decides whether add a (bottom, top)  pair of hint segments.
     *     Do not add the pair if:
     *     if weight (val) is 0,
     *     if both are sBEND segments
     *     if neither are a ghost hint, and weight <= pruneD and priority (spc)
     *     is <= 0:
     *     if either is an sBEND: skip
     *     if the BBox for one segment is the same or inside the BBox for the
     *     other: skip
     *
     *     else add it with eval.c:InsertHValue()
     *     add new HintVal to global valList.
     *     item->vVal = val; # weight
     *     item->initVal = val; # originl weight from EvalHPair()
     *     item->vSpc = spc; # priority
     *     item->vLoc1 = bot; # bottom Y value in Fixed 18.14
     *     item->vLoc2 = top; # top Y value in Fixed 18.14
     *     item->vSeg1 = bSeg; # bottom hint segment
     *     item->vSeg2 = tSeg; # top hint segment
     *     item->vGhst = ghst; # if it is a ghost segment.
     *     The new item is inserted after the first element where vlist->vLoc2 >= top
     *    and vlist->vLoc1 >= bottom
     *
     * 3) merge.c:PruneHVals();
     *
     *    item2 in the list knocks out item1 if:
     *    1) (item2 val is more than 3* greater than item1 val) and
     *       (val 1 is less than FixedInt(100)) and
     *       (item2 top and bottom is within item 1 top and bottom) and
     *       (if val1 is more than 50* less than val2 and either top
     *        seg1 is close to top seg 2, or bottom seg1 is close to
     *        bottom seg 2) and
     *       (val 1 < FixInt(16)) or
     *       ((item1 top not in blue zone, or top1 = top2) and
     *        (item1 bottom not in blue zone, or top1 = bottom2))
     *    "Close to" for the bottom segment means you can get to the bottom elt for
     *    item 2 from bottom elt for 1 within the same path, by
     *     stepping either forward or back from item 1's elt, and without going
     *    outside the bounds between
     *     location 1 and location 2. Same for top segments.
     *
     * 4) pick.c:FindBestHVals();
     * When a hint segment
     */

    LogMsg(LOGDEBUG, OK, "generate blues");
    if (NoBlueGlyph()) {
        gLenTopBands = gLenBotBands = 0;
    }
    GenHPts();
    LogMsg(LOGDEBUG, OK, "evaluate");
    if (!CounterFailed && HHintGlyph()) {
        pv = gPruneValue;
        gPruneValue = (Fixed)gMinVal;
        pa = gPruneA;
        gPruneA = (Fixed)gMinVal;
        pd = gPruneD;
        gPruneD = (Fixed)gMinVal;
        pc = gPruneC;
        gPruneC = (Fixed)gMaxVal;
        pb = gPruneB;
        gPruneB = (Fixed)gMinVal;
    }
    EvalH();
    PruneHVals();
    FindBestHVals();
    MergeVals(false);

    ShowHVals(gValList);
    LogMsg(LOGDEBUG, OK, "pick best");
    MarkLinks(gValList, true, links);
    CheckVals(gValList, false);

    /* Report stems and alignment zones, if this has been requested. */
    if (gDoAligns || gDoStems)
        DoHStems(gValList);

    /* Moves best HintVal items from valList to Hhinting list.
     * (? Choose from set of HintVals for the samte stem values.) */
    PickHVals(gValList);

    if (!CounterFailed && HHintGlyph()) {
        gPruneValue = pv;
        gPruneD = pd;
        gPruneC = pc;
        gPruneB = pb;
        gPruneA = pa;
        gUseH = UseCounter(gHHinting, false);
        if (!gUseH) { /* try to fix */
            AddBBoxHV(true, true);
            gUseH = UseCounter(gHHinting, false);
            if (!gUseH) { /* still bad news */
                LogMsg(INFO, OK,
                       "Glyph is in list for using H counter hints, "
                       "but didn't find any candidates.");
                CounterFailed = true;
            }
        }
    } else {
        gUseH = false;
    }
    if (gHHinting == NULL) {
        AddBBoxHV(true, false);
    }
    LogMsg(LOGDEBUG, OK, "results");
    LogMsg(LOGDEBUG, OK, gUseH ? "rv" : "rb");
    ShowHVals(gHHinting);
    if (gUseH) {
        LogMsg(INFO, OK, "Using H counter hints.");
    }
    sLst = gHHinting;
    while (sLst != NULL) {
        AddHPair(sLst, gUseH ? 'v' : 'b'); /* actually adds hint */
        sLst = sLst->vNxt;
    }
}

static void
DoHStems(HintVal* sLst1)
{
    Fixed glyphTop = INT32_MIN, glyphBot = INT32_MAX;
    bool curved;

    while (sLst1 != NULL) {
        Fixed bot = -sLst1->vLoc1;
        Fixed top = -sLst1->vLoc2;
        if (top < bot) {
            Fixed tmp = top;
            top = bot;
            bot = tmp;
        }
        if (top > glyphTop)
            glyphTop = top;
        if (bot < glyphBot)
            glyphBot = bot;

        /* skip if ghost or not a line on top or bottom */
        if (!sLst1->vGhst) {
            curved = !FindLineSeg(sLst1->vLoc1, botList) &&
                     !FindLineSeg(sLst1->vLoc2, topList);
            AddHStem(top, bot, curved);
            if (top != INT32_MIN || bot != INT32_MAX)
                AddStemExtremes(bot, top);
        }

        sLst1 = sLst1->vNxt;
    }

    if (glyphTop != INT32_MIN || glyphBot != INT32_MAX)
        AddGlyphExtremes(glyphBot, glyphTop);
}

static void
Yellows(unsigned char* links)
{
    Fixed pv = 0, pd = 0, pc = 0, pb = 0, pa = 0;
    HintVal* sLst;
    LogMsg(LOGDEBUG, OK, "generate yellows");
    GenVPts(SpecialGlyphType());
    LogMsg(LOGDEBUG, OK, "evaluate");
    if (!CounterFailed && VHintGlyph()) {
        pv = gPruneValue;
        gPruneValue = (Fixed)gMinVal;
        pa = gPruneA;
        gPruneA = (Fixed)gMinVal;
        pd = gPruneD;
        gPruneD = (Fixed)gMinVal;
        pc = gPruneC;
        gPruneC = (Fixed)gMaxVal;
        pb = gPruneB;
        gPruneB = (Fixed)gMinVal;
    }
    EvalV();
    PruneVVals();
    FindBestVVals();
    MergeVals(true);
    ShowVVals(gValList);
    LogMsg(LOGDEBUG, OK, "pick best");
    MarkLinks(gValList, false, links);
    CheckVals(gValList, true);

    if (gDoAligns || gDoStems)
        DoVStems(gValList);

    PickVVals(gValList);
    if (!CounterFailed && VHintGlyph()) {
        gPruneValue = pv;
        gPruneD = pd;
        gPruneC = pc;
        gPruneB = pb;
        gPruneA = pa;
        gUseV = UseCounter(gVHinting, true);
        if (!gUseV) { /* try to fix */
            AddBBoxHV(false, true);
            gUseV = UseCounter(gVHinting, true);
            if (!gUseV) { /* still bad news */
                LogMsg(INFO, OK,
                       "Glyph is in list for using V counter hints, "
                       "but didn't find any candidates.");
                CounterFailed = true;
            }
        }
    } else {
        gUseV = false;
    }
    if (gVHinting == NULL) {
        AddBBoxHV(false, false);
    }
    LogMsg(LOGDEBUG, OK, "results");
    LogMsg(LOGDEBUG, OK, gUseV ? "rm" : "ry");
    ShowVVals(gVHinting);
    if (gUseV) {
        LogMsg(INFO, OK, "Using V counter hints.");
    }
    sLst = gVHinting;
    while (sLst != NULL) {
        AddVPair(sLst, gUseV ? 'm' : 'y');
        sLst = sLst->vNxt;
    }
}

static void
DoVStems(HintVal* sLst)
{
    while (sLst != NULL) {
        Fixed lft, rght;
        bool curved;
        curved = !FindLineSeg(sLst->vLoc1, leftList) &&
                 !FindLineSeg(sLst->vLoc2, rightList);
        lft = sLst->vLoc1;
        rght = sLst->vLoc2;
        if (lft > rght) {
            Fixed tmp = lft;
            lft = rght;
            rght = tmp;
        }
        AddVStem(rght, lft, curved);
        sLst = sLst->vNxt;
    }
}

static void
RemoveRedundantFirstHints(void)
{
    PathElt* e;
    if (gNumPtLsts < 2 || !SameHints(0, 1)) {
        return;
    }
    e = gPathStart;
    while (e != NULL) {
        if (e->newhints == 1) {
            e->newhints = 0;
            return;
        }
        e = e->next;
    }
}

static void
AddHintsSetup(void)
{
    int i;
    gVBigDist = 0;
    for (i = 0; i < gNumVStems; i++) {
        if (gVStems[i] > gVBigDist) {
            gVBigDist = gVStems[i];
        }
    }
    if (gVBigDist < gInitBigDist) {
        gVBigDist = gInitBigDist;
    }
    gVBigDist = (gVBigDist * 23) / 20;
    acfixtopflt(gVBigDist, &gVBigDistR);
    gHBigDist = 0;
    for (i = 0; i < gNumHStems; i++) {
        if (gHStems[i] > gHBigDist) {
            gHBigDist = gHStems[i];
        }
    }
    gHBigDist = abs(gHBigDist);
    if (gHBigDist < gInitBigDist) {
        gHBigDist = gInitBigDist;
    }
    gHBigDist = (gHBigDist * 23) / 20;
    acfixtopflt(gHBigDist, &gHBigDistR);
    if (gRoundToInt) {
        RoundPathCoords();
    }
    CheckForMultiMoveTo();
    /* PreCheckForSolEol(); */
}

/* If extrahint is true then it is ok to have multi-level
 hinting. */
static void
AddHintsInnerLoop(const char* srcglyph, bool extrahint)
{
    int32_t retryHinting = 0;
    unsigned char* links;

    while (true) {
        PreGenPts();
        CheckSmooth();
        links = InitShuffleSubpaths();
        Blues(links);
        if (!gDoAligns) {
            Yellows(links);
        }
        if (gEditGlyph) {
            DoShuffleSubpaths(links);
        }
        gHPrimary = CopyHints(gHHinting);
        gVPrimary = CopyHints(gVHinting);
        PruneElementHintSegs();
        ListHintInfo();
        if (extrahint) {
            AutoExtraHints(MoveToNewHints());
        }
        gPtLstArray[gPtLstIndex] = gPointList;
        retryHinting++;
        /* we want to retry hinting if
         `1) CounterFailed or
         2) doFixes changed something, but in both cases, only on the first
         pass.
         */
        if (CounterFailed && retryHinting == 1) {
            goto retry;
        }
        if (retryHinting > 1) {
            break;
        }
    retry:
        /* if we are doing the stem and zones reporting, we need to discard the
         * reported. */
        if (gReportRetryCB != NULL) {
            gReportRetryCB(gReportRetryUserData);
        }
        if (gPathStart == NULL || gPathStart == gPathEnd) {
            LogMsg(LOGERROR, NONFATALERROR, "No glyph path.");
        }

        /* SaveFile(); SaveFile is always called in AddHintsCleanup, so this is
         * a duplciate */
        InitAll(RESTART);
        if (gWriteHintedBez && !ReadGlyph(srcglyph, false, false)) {
            break;
        }
        AddHintsSetup();
        if (!PreCheckForHinting()) {
            break;
        }
        if (gFlexOK) {
            gHasFlex = false;
            AutoAddFlex();
        }
    }
}

static void
AddHintsCleanup(void)
{
    RemoveRedundantFirstHints();
    if (gWriteHintedBez) {

        if (gPathStart == NULL || gPathStart == gPathEnd) {
            LogMsg(LOGERROR, NONFATALERROR,
                   "The glyph path vanished while adding hints.");
        } else {
            SaveFile();
        }
    }
    InitAll(RESTART);
}

static void
AddHints(const char* srcglyph, bool extrahint)
{
    if (gPathStart == NULL || gPathStart == gPathEnd) {
        LogMsg(INFO, OK, "No glyph path, so no hints.");
        SaveFile(); /* make sure it gets saved with no hinting */
        return;
    }
    CounterFailed = gBandError = false;
    CheckPathBBox();
    CheckForDups();
    AddHintsSetup();
    if (!PreCheckForHinting()) {
        return;
    }
    if (gFlexOK) {
        gHasFlex = false;
        AutoAddFlex();
    }
    AddHintsInnerLoop(srcglyph, extrahint);
    AddHintsCleanup();
}

bool
AutoHintGlyph(const char* srcglyph, bool extrahint)
{
    int32_t lentop = gLenTopBands, lenbot = gLenBotBands;
    if (!ReadGlyph(srcglyph, false, false)) {
        LogMsg(LOGERROR, NONFATALERROR, "Cannot parse glyph.");
    }
    AddHints(srcglyph, extrahint);
    gLenTopBands = lentop;
    gLenBotBands = lenbot;
    return true;
}
