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

static void DoHStems(const ACFontInfo* fontinfo, PClrVal sLst1);
static void DoVStems(PClrVal sLst);

static bool CounterFailed;

void
InitAll(const ACFontInfo* fontinfo, int32_t reason)
{
    InitData(fontinfo, reason); /* must be first */
    InitAuto(reason);
    InitFix(reason);
    InitGen(reason);
    InitPick(reason);
}

static int32_t
PtLstLen(PClrPoint lst)
{
    int32_t cnt = 0;
    while (lst != NULL) {
        cnt++;
        lst = lst->next;
    }
    return cnt;
}

static int32_t
PointListCheck(PClrPoint new, PClrPoint lst)
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
            LogMsg(LOGERROR, NONFATALERROR,
                   "Illegal character in point list in %s.\n", gGlyphName);
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
        if (lst->c == ch) { /* same kind of color */
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
             being merged with newcolors. This main hstem caused
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
SameColorLists(PClrPoint lst1, PClrPoint lst2)
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
SameColors(int32_t cn1, int32_t cn2)
{
    if (cn1 == cn2) {
        return true;
    }
    return SameColorLists(gPtLstArray[cn1], gPtLstArray[cn2]);
}

void
MergeFromMainColors(char ch)
{
    PClrPoint lst;
    for (lst = gPtLstArray[0]; lst != NULL; lst = lst->next) {
        if (lst->c != ch) {
            continue;
        }
        if (PointListCheck(lst, gPointList) == -1) {
            if (ch == 'b') {
                AddColorPoint(0, lst->y0, 0, lst->y1, ch, lst->p0, lst->p1);
            } else {
                AddColorPoint(lst->x0, 0, lst->x1, 0, ch, lst->p0, lst->p1);
            }
        }
    }
}

void
AddColorPoint(Fixed x0, Fixed y0, Fixed x1, Fixed y1, char ch, PPathElt p0,
              PPathElt p1)
{
    PClrPoint pt;
    int32_t chk;
    pt = (PClrPoint)Alloc(sizeof(ClrPoint));
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
        ReportColorConflict(x0, y0, x1, y1, ch);
    }
    if (chk == -1) {
        pt->next = gPointList;
        gPointList = pt;
        LogColorInfo(gPointList);
    }
}

static void
CopyClrFromLst(char clr, PClrPoint lst)
{
    bool bvflg = (clr == 'b' || clr == 'v');
    while (lst != NULL) {
        if (lst->c == clr) {
            if (bvflg) {
                AddColorPoint(0, lst->y0, 0, lst->y1, clr, lst->p0, lst->p1);
            } else {
                AddColorPoint(lst->x0, 0, lst->x1, 0, clr, lst->p0, lst->p1);
            }
        }
        lst = lst->next;
    }
}

void
CopyMainV(void)
{
    CopyClrFromLst('m', gPtLstArray[0]);
}

void
CopyMainH(void)
{
    CopyClrFromLst('v', gPtLstArray[0]);
}

void
AddHPair(PClrVal v, char ch)
{
    Fixed bot, top;
    PPathElt p0, p1, p;
    bot = itfmy(v->vLoc1);
    top = itfmy(v->vLoc2);
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
    AddColorPoint(0, bot, 0, top, ch, p0, p1);
}

void
AddVPair(PClrVal v, char ch)
{
    Fixed lft, rght;
    PPathElt p0, p1, p;
    lft = itfmx(v->vLoc1);
    rght = itfmx(v->vLoc2);
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
    AddColorPoint(lft, 0, rght, 0, ch, p0, p1);
}

static bool
UseCounter(PClrVal sLst, bool mclr)
{
    int32_t cnt = 0;
    Fixed minLoc, midLoc, maxLoc, prevBstVal, bestVal;
    Fixed minDelta, midDelta, maxDelta, th;
    PClrVal lst, newLst;
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
        if (mclr) {
            gVColoring = newLst;
        } else {
            gHColoring = newLst;
        }
        return true;
    }
    if (abs(minDelta - maxDelta) < FixInt(3) &&
        abs((maxLoc - midLoc) - (midLoc - minLoc)) < FixInt(3)) {
        LogMsg(LOGERROR, OK,
               mclr ? "Near miss for using V counter hinting."
                    : "Near miss for using H counter hinting.");
    }
    return false;
}

static void
GetNewPtLst(void)
{
    if (gNumPtLsts >= gMaxPtLsts) { /* increase size */
        PClrPoint* newArray;
        int32_t i;
        gMaxPtLsts += 5;
        newArray = (PClrPoint*)Alloc(gMaxPtLsts * sizeof(PClrPoint));
        for (i = 0; i < gMaxPtLsts - 5; i++) {
            newArray[i] = gPtLstArray[i];
        }
        gPtLstArray = newArray;
    }
    gPtLstIndex = gNumPtLsts;
    gNumPtLsts++;
    gPointList = NULL;
    gPtLstArray[gPtLstIndex] = NULL;
}

void
XtraClrs(PPathElt e)
{
    /* this can be simplified for standalone coloring */
    gPtLstArray[gPtLstIndex] = gPointList;
    if (e->newcolors == 0) {
        GetNewPtLst();
        e->newcolors = (int16_t)gPtLstIndex;
    }
    gPtLstIndex = e->newcolors;
    gPointList = gPtLstArray[gPtLstIndex];
}

static void
Blues(const ACFontInfo* fontinfo)
{
    Fixed pv = 0, pd = 0, pc = 0, pb = 0, pa = 0;
    PClrVal sLst;

    /*
     Top alignment zones are in the global 'topBands', bottom in 'botBands'.

     This function looks through the path, as defined by the linked list of
    'elts', starting at the global 'pathStart', and adds to several lists.
    Coordinates are stored in the elt.(x,y) as (original value)/2.0, aka right
    shifted by 1 bit from the original 24.8 Fixed. I suspect that is to allow a
    larger integer portion - when this program was written, an int was 16 bits.


     HStems, Vstems are global lists of Fixed 24.8 numbers..

     segLists is an array of 4  ClrSeg linked lists. list 0 and 1 are
    respectively up and down vertical segments. Lists 2 and 3 are
     respectively left pointing and right pointing horizontal segments. On a
    counter-clockwise path, this is the same as selecting
     top and bottom stem locations.

     NoBlueChar() consults a hard-coded list of glyph names, If the glyph is in
    this list, set the alignment zones (top and botBands) to empty.

     1) gen.c:GenHPts() . Buid the raw list of stem segments in global 'topList'
    and 'botList'.

     gen.c:GenHPts() steps through the liked list of path segments, starting at
    'pathStart' It decides if a path is mostly H,
     and if so, adds it to a linked list of hstem candidates in segLists, by
    calling gen.c:AddHSegment(). This calls ReportAddHSeg() (useful in
    debugging),
     and then gen.c:AddSegment.

     If the path segment is in fact entirely vertical and is followed by a sharp
    bend,
     gen.c:GenHPts adds two new path segments just 1 unit long,  after the
    segment end point,
     called H/VBends (segment type sBend=1). I have no idea what these are for.

     AddSegment is pretty simple. It creates a new hint segment 'ClrSeg' for the
    parent path elt , fills it in,
     adds it to appropriate  list of the 4 segLists, and then sorts by hstem
    location.
     seg->sElt is the parent elt
     seg->sType is the type
     seg->sLoc is the location in Fixed 18.14: right shift 7 to get integer
    value.

     If the current path elt is a Closepath, It also calls LinkSegment() to add
    the current stem segment to the list of stem segments referenced by this
    elt.
     e->Hs/Vs.

     Note that a hint segment is created for each nearly vertical or horioztonal
    path elt. Ths means that in an H, there will be two hint segments created
    for
     the bottom and top of the H, as there are two horizontal paths with the
    same Y at the top and bottom of the H.

     Assign the top and bottom Hstem location lists.
     topList = segLists[2]
     botList = segLists[3];

     2) eval.c::EvalH().  Evaluate every combination of botList and topList, and
    assign a priority value and a 'Q' value.

     For each bottom stem
     for each top stem
     1) assign priority (spc) and weight (val) values with EvalHPair()
     2) report stem near misses  in the 'HStems' list with HStemMiss()
     3) decide whether to add pair to 'HStems' list with AddHValue()

     Add ghost hints.
     For each bottom stem segment and then for each top stem segment:
     if it is in an alignment zone, make a ghost hint segment and add it with
    AddHValue().

     EvalHPair() sets priority (spc) and weight (val) values.
            Omit pair by setting value to 0 if:
                    bottom is in bottom alignment zone, and top is in top
    alignment zone. (otherwise, these will override the ghost hints).

            Boost priority by +2 if either the bot or top segment is in an
    alignment zone.

            dy = stem widt ( top - bot)

            Calculcate dist. Dist is set to a fudge factor *dy.
            if bottom segment xo->x1 overlaps top x0->x1, the fudge factor is
    1.0. The
            less the overlap, the larger the fduge factor.
            if bottom segment xo->x1 overlaps top x0->x1:.
                    if  top and bottom overlap exactly, dist = dy
                    if they barely overlap, dist = 1.4*dy
                    in between, interpolate.
            else, look at closest ends betwen bottom and top segments.
                    dx = min X separation between top and bottom segments.
                    dist = 1.4 *dy
                    dist += dx*dx
                    if dx > dy:
                            dist *= dx / dy;
            Look through the HStems global list. For each match to dy, boost
    priority by + 1.
            Calculate weight with gen.c:AdjustVal()
                    if dy is more than twice the 1.1.5* the largest hint in
    HStems, set weight to 0.
                    Calculate weight as related to length of the segments
    squared divied by the distance squared.
                    Basically, the greater the ratio segment overlap  to stem
    width, the higher the value.
                    if dy is greater than the largest stem hint in HStems,
    decrease the value
                            scale weight by  of *(largest stem hint in HStems)/
    dy)**3.

     AddHValue() decides whether add a (bottom, top)  pair of color segments.
     Do not add the pair if:
     if weight (val) is 0,
     if both are sBEND segments
     if neither are a ghost hint, and weight <= pruneD and priority (spc) is <=
    0:
     if either is an sBEND: skip
     if the BBox for one segment is the same or inside the BBox for the other:
    skip

     else add it with eval.c:InsertHValue()
     add new ClrVal to global valList.
     item->vVal = val; # weight
     item->initVal = val; # originl weight from EvalHPair()
     item->vSpc = spc; # priority
     item->vLoc1 = bot; # bottom Y value in Fixed 18.14
     item->vLoc2 = top; # top Y value in Fixed 18.14
     item->vSeg1 = bSeg; # bottom color segment
     item->vSeg2 = tSeg; # top color segment
     item->vGhst = ghst; # if it is a ghost segment.
     The new item is inserted after the first element where vlist->vLoc2 >= top
    and vlist->vLoc1 >= bottom

     3) merge.c:PruneHVals();

     item2 in the list knocks out item1 if:
    1) (item2 val is more than 3* greater than item1 val) and
            val 1 is less than FixedInt(100) and
            item2 top and bottom is within item 1 top and bottom
            and (  if val1 is more than 50* less than val2 and
                                    either top seg1 is close to top seg 2, or
    bottom seg1 is close to bottom seg 2
                    )
            and (val 1 < FixInt(16)) or ( ( item1 top not in blue zone, or top1
    = top2) and
                                                                    ( item1
    bottom not in blue zone, or top1 = bottom2))
    "Close to" for the bottom segment means you can get to the bottom elt for
    item 2 from bottom elt for 1 within the same path, by
     stepping either forward or back from item 1's elt, and without going
    outside the bounds between
     location 1 and location 2. Same for top segments.




     4) pick.c:FindBestHVals();
     When a hint segment    */

    LogMsg(LOGDEBUG, OK, "generate blues");
    if (NoBlueChar()) {
        gLenTopBands = gLenBotBands = 0;
    }
    GenHPts();
    LogMsg(LOGDEBUG, OK, "evaluate");
    if (!CounterFailed && HColorChar()) {
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
    MarkLinks(gValList, true);
    CheckVals(gValList, false);
    DoHStems(fontinfo, gValList); /* Report stems and alignment zones, if this
                                    has been requested. */
    PickHVals(gValList); /* Moves best ClrVal items from valList to Hcoloring
                           list. (? Choose from set of ClrVals for the samte
                           stem values.) */
    if (!CounterFailed && HColorChar()) {
        gPruneValue = pv;
        gPruneD = pd;
        gPruneC = pc;
        gPruneB = pb;
        gPruneA = pa;
        gUseH = UseCounter(gHColoring, false);
        if (!gUseH) { /* try to fix */
            AddBBoxHV(true, true);
            gUseH = UseCounter(gHColoring, false);
            if (!gUseH) { /* still bad news */
                LogMsg(LOGERROR, OK,
                       "Note: Glyph is in list for using H counter hints, "
                       "but didn't find any candidates.");
                CounterFailed = true;
            }
        }
    } else {
        gUseH = false;
    }
    if (gHColoring == NULL) {
        AddBBoxHV(true, false);
    }
    LogMsg(LOGDEBUG, OK, "results");
    LogMsg(LOGDEBUG, OK, gUseH ? "rv" : "rb");
    ShowHVals(gHColoring);
    if (gUseH) {
        LogMsg(INFO, OK, "Using H counter hints.");
    }
    sLst = gHColoring;
    while (sLst != NULL) {
        AddHPair(sLst, gUseH ? 'v' : 'b'); /* actually adds hint */
        sLst = sLst->vNxt;
    }
}

static void
DoHStems(const ACFontInfo* fontinfo, PClrVal sLst1)
{
    Fixed charTop = INT32_MIN, charBot = INT32_MAX;
    bool curved;
    if (!gDoAligns && !gDoStems) {
        return;
    }
    while (sLst1 != NULL) {
        Fixed bot = itfmy(sLst1->vLoc1);
        Fixed top = itfmy(sLst1->vLoc2);
        if (top < bot) {
            Fixed tmp = top;
            top = bot;
            bot = tmp;
        }
        if (top > charTop) {
            charTop = top;
        }
        if (bot < charBot) {
            charBot = bot;
        }
        /* skip if ghost or not a line on top or bottom */
        if (sLst1->vGhst) {
            sLst1 = sLst1->vNxt;
            continue;
        }
        curved = !FindLineSeg(sLst1->vLoc1, botList) &&
                 !FindLineSeg(sLst1->vLoc2, topList);
        AddHStem(top, bot, curved);
        sLst1 = sLst1->vNxt;
        if (top != INT32_MIN || bot != INT32_MAX) {
            AddStemExtremes(UnScaleAbs(fontinfo, bot),
                            UnScaleAbs(fontinfo, top));
        }
    }
    if (charTop != INT32_MIN || charBot != INT32_MAX) {
        AddCharExtremes(UnScaleAbs(fontinfo, charBot),
                        UnScaleAbs(fontinfo, charTop));
    }
}

static void
Yellows(void)
{
    Fixed pv = 0, pd = 0, pc = 0, pb = 0, pa = 0;
    PClrVal sLst;
    LogMsg(LOGDEBUG, OK, "generate yellows");
    GenVPts(SpecialCharType());
    LogMsg(LOGDEBUG, OK, "evaluate");
    if (!CounterFailed && VColorChar()) {
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
    MarkLinks(gValList, false);
    CheckVals(gValList, true);
    DoVStems(gValList);
    PickVVals(gValList);
    if (!CounterFailed && VColorChar()) {
        gPruneValue = pv;
        gPruneD = pd;
        gPruneC = pc;
        gPruneB = pb;
        gPruneA = pa;
        gUseV = UseCounter(gVColoring, true);
        if (!gUseV) { /* try to fix */
            AddBBoxHV(false, true);
            gUseV = UseCounter(gVColoring, true);
            if (!gUseV) { /* still bad news */
                LogMsg(LOGERROR, OK,
                       "Note: Glyph is in list for using V counter hints, "
                       "but didn't find any candidates.");
                CounterFailed = true;
            }
        }
    } else {
        gUseV = false;
    }
    if (gVColoring == NULL) {
        AddBBoxHV(false, false);
    }
    LogMsg(LOGDEBUG, OK, "results");
    LogMsg(LOGDEBUG, OK, gUseV ? "rm" : "ry");
    ShowVVals(gVColoring);
    if (gUseV) {
        LogMsg(INFO, OK, "Using V counter hints.");
    }
    sLst = gVColoring;
    while (sLst != NULL) {
        AddVPair(sLst, gUseV ? 'm' : 'y');
        sLst = sLst->vNxt;
    }
}

static void
DoVStems(PClrVal sLst)
{
    if (!gDoAligns && !gDoStems) {
        return;
    }
    while (sLst != NULL) {
        Fixed lft, rght;
        bool curved;
        curved = !FindLineSeg(sLst->vLoc1, leftList) &&
                 !FindLineSeg(sLst->vLoc2, rightList);
        lft = itfmx(sLst->vLoc1);
        rght = itfmx(sLst->vLoc2);
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
RemoveRedundantFirstColors(void)
{
    PPathElt e;
    if (gNumPtLsts < 2 || !SameColors(0, 1)) {
        return;
    }
    e = gPathStart;
    while (e != NULL) {
        if (e->newcolors == 1) {
            e->newcolors = 0;
            return;
        }
        e = e->next;
    }
}

static void
AddColorsSetup(void)
{
    int i;
    gVBigDist = 0;
    for (i = 0; i < gNumVStems; i++) {
        if (gVStems[i] > gVBigDist) {
            gVBigDist = gVStems[i];
        }
    }
    gVBigDist = dtfmx(gVBigDist);
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
    gHBigDist = abs(dtfmy(gHBigDist));
    if (gHBigDist < gInitBigDist) {
        gHBigDist = gInitBigDist;
    }
    gHBigDist = (gHBigDist * 23) / 20;
    acfixtopflt(gHBigDist, &gHBigDistR);
    if ((!gScalingHints) && (gRoundToInt)) {
        RoundPathCoords();
    }
    CheckForMultiMoveTo();
    /* PreCheckForSolEol(); */
}

/* If extracolor is true then it is ok to have multi-level
 coloring. */
static void
AddColorsInnerLoop(const ACFontInfo* fontinfo, const char* srcglyph,
                   bool extracolor)
{
    int32_t solEolCode = 2, retryColoring = 0;
    bool isSolEol = false;
    while (true) {
        PreGenPts();
        CheckSmooth();
        InitShuffleSubpaths();
        Blues(fontinfo);
        if (!gDoAligns) {
            Yellows();
        }
        if (gEditChar) {
            DoShuffleSubpaths();
        }
        gHPrimary = CopyClrs(gHColoring);
        gVPrimary = CopyClrs(gVColoring);
        /*
         isSolEol = SpecialSolEol() && !useV && !useH;
         solEolCode = isSolEol? SolEolCharCode() : 2;
         */
        PruneElementColorSegs();
        ListClrInfo();
        if (extracolor) {
            AutoExtraColors(MoveToNewClrs(), isSolEol, solEolCode);
        }
        gPtLstArray[gPtLstIndex] = gPointList;
        if (isSolEol) {
            break;
        }
        retryColoring++;
        /* we want to retry coloring if
         `1) CounterFailed or
         2) doFixes changed something, but in both cases, only on the first
         pass.
         */
        if (CounterFailed && retryColoring == 1) {
            goto retry;
        }
        if (!DoFixes()) {
            break;
        }
        if (retryColoring > 1) {
            break;
        }
    retry:
        /* if we are doing the stem and zones reporting, we need to discard the
         * reported. */
        if (gReportRetryCB != NULL) {
            gReportRetryCB();
        }
        if (gPathStart == NULL || gPathStart == gPathEnd) {
            LogMsg(LOGERROR, NONFATALERROR, "No glyph path in %s.\n",
                   gGlyphName);
        }

        /* SaveFile(); SaveFile is always called in AddColorsCleanup, so this is
         * a duplciate */
        InitAll(fontinfo, RESTART);
        if (gWriteColoredBez && !ReadGlyph(fontinfo, srcglyph, false, false)) {
            break;
        }
        AddColorsSetup();
        if (!PreCheckForColoring()) {
            break;
        }
        if (gFlexOK) {
            gHasFlex = false;
            AutoAddFlex();
        }
    }
}

static void
AddColorsCleanup(const ACFontInfo* fontinfo)
{
    RemoveRedundantFirstColors();
    if (gWriteColoredBez) {

        if (gPathStart == NULL || gPathStart == gPathEnd) {
            LogMsg(LOGERROR, NONFATALERROR,
                   "The %s glyph path vanished while adding "
                   "hints.\n",
                   gGlyphName);
        } else {
            SaveFile(fontinfo);
        }
    }
    InitAll(fontinfo, RESTART);
}

static void
AddColors(const ACFontInfo* fontinfo, const char* srcglyph, bool extracolor)
{
    if (gPathStart == NULL || gPathStart == gPathEnd) {
        LogMsg(INFO, OK, "No character path, so no hints.");
        SaveFile(fontinfo); /* make sure it gets saved with no coloring */
        return;
    }
    CounterFailed = gBandError = false;
    CheckPathBBox();
    CheckForDups();
    AddColorsSetup();
    if (!PreCheckForColoring()) {
        return;
    }
    if (gFlexOK) {
        gHasFlex = false;
        AutoAddFlex();
    }
    AddColorsInnerLoop(fontinfo, srcglyph, extracolor);
    AddColorsCleanup(fontinfo);
}

bool
AutoColorGlyph(const ACFontInfo* fontinfo, const char* srcglyph,
               bool extracolor)
{
    int32_t lentop = gLenTopBands, lenbot = gLenBotBands;
    if (!ReadGlyph(fontinfo, srcglyph, false, false)) {
        LogMsg(LOGERROR, NONFATALERROR, "Cannot parse %s glyph.\n", gGlyphName);
    }
    AddColors(fontinfo, srcglyph, extracolor);
    gLenTopBands = lentop;
    gLenBotBands = lenbot;
    return true;
}
