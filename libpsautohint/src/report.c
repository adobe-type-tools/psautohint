/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include <stdarg.h>

#include "ac.h"

double
FixToDbl(Fixed f)
{
    float r;
    acfixtopflt(f, &r);
    return r;
}

void
PrintMessage(char* format, ...)
{
    if ((gLibReportCB != NULL) && (strlen(format) > 0)) {
        char msgBuffer[MAXMSGLEN + 1];
        va_list va;

        msgBuffer[0] = '\t';

        va_start(va, format);
        vsnprintf(msgBuffer + 1, MAXMSGLEN - 1, format, va);
        va_end(va);

        gLibReportCB(msgBuffer);
        gLibReportCB("\n");
    }
}

void
ReportError(char* format, ...)
{
    if (gReportErrors) {
        char msgBuffer[MAXMSGLEN + 1];
        va_list va;

        va_start(va, format);
        vsnprintf(msgBuffer, MAXMSGLEN, format, va);
        va_end(va);

        PrintMessage(msgBuffer);
    }
}

void
ReportSmoothError(Fixed x, Fixed y)
{
    ReportError("Junction at %g %g may need smoothing.", FixToDbl(itfmx(x)),
                FixToDbl(itfmy(y)));
}

void
ReportAddFlex(void)
{
    if (gHasFlex)
        return;
    gHasFlex = true;
    PrintMessage("FYI: added flex operators to this character.");
}

void
ReportClipSharpAngle(Fixed x, Fixed y)
{
    PrintMessage("FYI: Too sharp angle at %g %g has been clipped.",
                 FixToDbl(itfmx(x)), FixToDbl(itfmy(y)));
}

void
ReportSharpAngle(Fixed x, Fixed y)
{
    PrintMessage("FYI: angle at %g %g is very sharp. Please check.",
                 FixToDbl(itfmx(x)), FixToDbl(itfmy(y)));
}

void
ReportLinearCurve(PPathElt e, Fixed x0, Fixed y0, Fixed x1, Fixed y1)
{
    if (gAutoLinearCurveFix) {
        e->type = LINETO;
        e->x = e->x3;
        e->y = e->y3;
        PrintMessage("Curve from %g %g to %g %g was changed to a line.",
                     FixToDbl(itfmx(x0)), FixToDbl(itfmy(y0)),
                     FixToDbl(itfmx(x1)), FixToDbl(itfmy(y1)));
    } else {
        PrintMessage("Curve from %g %g to %g %g should be changed to a line.",
                     FixToDbl(itfmx(x0)), FixToDbl(itfmy(y0)),
                     FixToDbl(itfmx(x1)), FixToDbl(itfmy(y1)));
    }
}

static void
ReportNonHVError(Fixed x0, Fixed y0, Fixed x1, Fixed y1, char* s)
{
    Fixed dx, dy;
    x0 = itfmx(x0);
    y0 = itfmy(y0);
    x1 = itfmx(x1);
    y1 = itfmy(y1);
    dx = x0 - x1;
    dy = y0 - y1;
    if (abs(dx) > FixInt(10) || abs(dy) > FixInt(10) ||
        FTrunc(dx * dx) + FTrunc(dy * dy) > FixInt(100)) {
        ReportError("The line from %g %g to %g %g is not exactly %s.",
                    FixToDbl(x0), FixToDbl(y0), FixToDbl(x1), FixToDbl(y1), s);
    }
}

void
ReportNonHError(Fixed x0, Fixed y0, Fixed x1, Fixed y1)
{
    ReportNonHVError(x0, y0, x1, y1, "horizontal");
}

void
ReportNonVError(Fixed x0, Fixed y0, Fixed x1, Fixed y1)
{
    ReportNonHVError(x0, y0, x1, y1, "vertical");
}

void
ExpectedMoveTo(PPathElt e)
{
    char* s;
    switch (e->type) {
        case LINETO:
            s = (char*)"lineto";
            break;
        case CURVETO:
            s = (char*)"curveto";
            break;
        case CLOSEPATH:
            s = (char*)"closepath";
            break;
        default:
            LogMsg(LOGERROR, NONFATALERROR, "Malformed path list.\n");
            return;
    }
    LogMsg(LOGERROR, NONFATALERROR,
           "Path for %s character has a %s where a moveto was "
           "expected.\n  The data are probably truncated.",
           gGlyphName, s);
}

void
ReportMissingClosePath(void)
{
    LogMsg(LOGERROR, NONFATALERROR, "Missing closepath in %s character.\n"
                                    "  The data are probably truncated.",
           gGlyphName);
}

void
ReportTryFlexNearMiss(Fixed x0, Fixed y0, Fixed x2, Fixed y2)
{
    ReportError("Curves from %g %g to %g %g near miss for adding flex.",
                FixToDbl(itfmx(x0)), FixToDbl(itfmy(y0)), FixToDbl(itfmx(x2)),
                FixToDbl(itfmy(y2)));
}

void
ReportTryFlexError(bool CPflg, Fixed x, Fixed y)
{
    ReportError(
      CPflg ? "Please move closepath from %g %g so can add flex."
            : "Please remove zero length element at %g %g so can add flex.",
      FixToDbl(itfmx(x)), FixToDbl(itfmy(y)));
}

void
ReportSplit(PPathElt e)
{
    Fixed x0, y0, x1, y1;
    GetEndPoints(e, &x0, &y0, &x1, &y1);
    PrintMessage(
      "FYI: the element that goes from %g %g to %g %g has been split.",
      FixToDbl(itfmx(x0)), FixToDbl(itfmy(y0)), FixToDbl(itfmx(x1)),
      FixToDbl(itfmy(y1)));
}

void
AskForSplit(PPathElt e)
{
    Fixed x0, y0, x1, y1;
    if (e->type == MOVETO)
        e = GetClosedBy(e);
    GetEndPoints(e, &x0, &y0, &x1, &y1);
    ReportError("Please split the element that goes from %g %g to %g %g.",
                FixToDbl(itfmx(x0)), FixToDbl(itfmy(y0)), FixToDbl(itfmx(x1)),
                FixToDbl(itfmy(y1)));
}

void
ReportPossibleLoop(PPathElt e)
{
    Fixed x0, y0, x1, y1;
    if (e->type == MOVETO)
        e = GetClosedBy(e);
    GetEndPoints(e, &x0, &y0, &x1, &y1);
    ReportError("Possible loop in element that goes from %g %g to %g %g."
                " Please check.",
                FixToDbl(itfmx(x0)), FixToDbl(itfmy(y0)), FixToDbl(itfmx(x1)),
                FixToDbl(itfmy(y1)));
}

void
ReportConflictCheck(PPathElt e, PPathElt conflict, PPathElt cp)
{
    Fixed ex, ey, cx, cy, cpx, cpy;
    GetEndPoint(e, &ex, &ey);
    GetEndPoint(conflict, &cx, &cy);
    GetEndPoint(cp, &cpx, &cpy);
    ReportError("Check e %g %g conflict %g %g cp %g %g.", FixToDbl(itfmx(ex)),
                FixToDbl(itfmy(ey)), FixToDbl(itfmx(cx)), FixToDbl(itfmy(cy)),
                FixToDbl(itfmx(cpx)), FixToDbl(itfmy(cpy)));
}

void
ReportConflictCnt(PPathElt e, int32_t cnt)
{
    Fixed ex, ey;
    GetEndPoint(e, &ex, &ey);
    ReportError("%g %g conflict count = %d", FixToDbl(itfmx(ex)),
                FixToDbl(itfmy(ey)), cnt);
}

void
ReportRemFlare(PPathElt e, PPathElt e2, bool hFlg, int32_t i)
{
    Fixed ex1, ey1, ex2, ey2;
    if (!gShowClrInfo)
        return;
    GetEndPoint(e, &ex1, &ey1);
    GetEndPoint(e2, &ex2, &ey2);
    PrintMessage("Removed %s flare at %g %g by %g %g : %d.",
                 hFlg ? "horizontal" : "vertical", FixToDbl(itfmx(ex1)),
                 FixToDbl(itfmy(ey1)), FixToDbl(itfmx(ex2)),
                 FixToDbl(itfmy(ey2)), i);
}

void
ReportRemConflict(PPathElt e)
{
    Fixed ex, ey;
    if (!gShowClrInfo)
        return;
    GetEndPoint(e, &ex, &ey);
    ReportError("Removed conflicting hints at %g %g.", FixToDbl(itfmx(ex)),
                FixToDbl(itfmy(ey)));
}

void
ReportRotateSubpath(PPathElt e)
{
    Fixed ex, ey;
    if (!gShowClrInfo)
        return;
    GetEndPoint(e, &ex, &ey);
    PrintMessage("FYI: changed closepath to %g %g.", FixToDbl(itfmx(ex)),
                 FixToDbl(itfmy(ey)));
}

void
ReportRemShortColors(Fixed ex, Fixed ey)
{
    if (!gShowClrInfo)
        return;
    PrintMessage("Removed hints from short element at %g %g.",
                 FixToDbl(itfmx(ex)), FixToDbl(itfmy(ey)));
}

static void
PrntVal(Fixed v)
{
    if (v >= FixInt(100000))
        PrintMessage("%d", FTrunc(v));
    else
        PrintMessage("%g", FixToDbl(v));
}

static void
ShwHV(PClrVal val)
{
    Fixed bot, top;
    bot = itfmy(val->vLoc1);
    top = itfmy(val->vLoc2);
    PrintMessage("b %g t %g v ", FixToDbl(bot), FixToDbl(top));
    PrntVal(val->vVal);
    PrintMessage(" s %g", FixToDbl(val->vSpc));
    if (val->vGhst)
        PrintMessage(" G");
}

void
ShowHVal(PClrVal val)
{
    Fixed l, r;
    PClrSeg seg;
    ShwHV(val);
    seg = val->vSeg1;
    if (seg == NULL)
        return;
    l = itfmx(seg->sMin);
    r = itfmx(seg->sMax);
    PrintMessage(" l1 %g r1 %g ", FixToDbl(l), FixToDbl(r));
    seg = val->vSeg2;
    l = itfmx(seg->sMin);
    r = itfmx(seg->sMax);
    PrintMessage(" l2 %g r2 %g", FixToDbl(l), FixToDbl(r));
}

void
ShowHVals(PClrVal lst)
{
    while (lst != NULL) {
        ShowHVal(lst);
        lst = lst->vNxt;
    }
}

void
ReportAddHVal(PClrVal val)
{
    ShowHVal(val);
}

static void
ShwVV(PClrVal val)
{
    Fixed lft, rht;
    lft = itfmx(val->vLoc1);
    rht = itfmx(val->vLoc2);
    PrintMessage("l %g r %g v ", FixToDbl(lft), FixToDbl(rht));
    PrntVal(val->vVal);
    PrintMessage(" s %g", FixToDbl(val->vSpc));
}

void
ShowVVal(PClrVal val)
{
    Fixed b, t;
    PClrSeg seg;
    ShwVV(val);
    seg = val->vSeg1;
    if (seg == NULL)
        return;
    b = itfmy(seg->sMin);
    t = itfmy(seg->sMax);
    PrintMessage(" b1 %g t1 %g ", FixToDbl(b), FixToDbl(t));
    seg = val->vSeg2;
    b = itfmy(seg->sMin);
    t = itfmy(seg->sMax);
    PrintMessage(" b2 %g t2 %g", FixToDbl(b), FixToDbl(t));
}

void
ShowVVals(PClrVal lst)
{
    while (lst != NULL) {
        ShowVVal(lst);
        lst = lst->vNxt;
    }
}

void
ReportAddVVal(PClrVal val)
{
    ShowVVal(val);
}

void
ReportFndBstVal(PClrSeg seg, PClrVal val, bool hFlg)
{
    if (hFlg) {
        PrintMessage("FndBstVal: sLoc %g sLft %g sRght %g ",
                     FixToDbl(itfmy(seg->sLoc)), FixToDbl(itfmx(seg->sMin)),
                     FixToDbl(itfmx(seg->sMax)));
        if (val)
            ShwHV(val);
        else
            PrintMessage("NULL");
    } else {
        PrintMessage("FndBstVal: sLoc %g sBot %g sTop %g ",
                     FixToDbl(itfmx(seg->sLoc)), FixToDbl(itfmy(seg->sMin)),
                     FixToDbl(itfmy(seg->sMax)));
        if (val)
            ShwVV(val);
        else
            PrintMessage("NULL");
    }
}

void
ReportCarry(Fixed l0, Fixed l1, Fixed loc, PClrVal clrs, bool vert)
{
    if (!gShowClrInfo)
        return;
    if (vert) {
        ShowVVal(clrs);
        loc = itfmx(loc);
        l0 = itfmx(l0);
        l1 = itfmx(l1);
    } else {
        ShowHVal(clrs);
        loc = itfmy(loc);
        l0 = itfmy(l0);
        l1 = itfmy(l1);
    }
    PrintMessage(" carry to %g in [%g..%g]", FixToDbl(loc), FixToDbl(l0),
                 FixToDbl(l1));
}

void
ReportBestCP(PPathElt e, PPathElt cp)
{
    Fixed ex, ey, px, py;
    GetEndPoint(e, &ex, &ey);
    if (cp != NULL) {
        GetEndPoint(cp, &px, &py);
        PrintMessage("%g %g best cp at %g %g", FixToDbl(itfmx(ex)),
                     FixToDbl(itfmy(ey)), FixToDbl(itfmx(px)),
                     FixToDbl(itfmy(py)));
    } else {
        PrintMessage("%g %g no best cp", FixToDbl(itfmx(ex)),
                     FixToDbl(itfmy(ey)));
    }
}

void
LogColorInfo(PClrPoint pl)
{
    char c = pl->c;
    if (c == 'y' || c == 'm') { /* vertical lines */
        Fixed lft = pl->x0;
        Fixed rht = pl->x1;
        PrintMessage("%4g  %-30s%5g%5g\n", FixToDbl(rht - lft), gGlyphName,
                     FixToDbl(lft), FixToDbl(rht));
    } else {
        Fixed bot = pl->y0;
        Fixed top = pl->y1;
        Fixed wdth = top - bot;
        if (wdth == -FixInt(21) || wdth == -FixInt(20))
            return; /* ghost pair */
        PrintMessage("%4g  %-30s%5g%5g\n", FixToDbl(wdth), gGlyphName,
                     FixToDbl(bot), FixToDbl(top));
    }
}

static void
LstHVal(PClrVal val)
{
    PrintMessage("\t");
    ShowHVal(val);
    PrintMessage(" ");
}

static void
LstVVal(PClrVal val)
{
    PrintMessage("\t");
    ShowVVal(val);
    PrintMessage(" ");
}

void
ListClrInfo(void)
{ /* debugging routine */
    PPathElt e;
    PSegLnkLst hLst, vLst;
    PClrSeg seg;
    Fixed x, y;
    e = gPathStart;
    while (e != NULL) {
        hLst = e->Hs;
        vLst = e->Vs;
        if ((hLst != NULL) || (vLst != NULL)) {
            GetEndPoint(e, &x, &y);
            x = itfmx(x);
            y = itfmy(y);
            PrintMessage("x %g y %g ", FixToDbl(x), FixToDbl(y));
            while (hLst != NULL) {
                seg = hLst->lnk->seg;
                LstHVal(seg->sLnk);
                hLst = hLst->next;
            }
            while (vLst != NULL) {
                seg = vLst->lnk->seg;
                LstVVal(seg->sLnk);
                vLst = vLst->next;
            }
        }
        e = e->next;
    }
}

void
ReportAddVSeg(Fixed from, Fixed to, Fixed loc, int32_t i)
{
    if (!gShowClrInfo || !gShowVs)
        return;
    PrintMessage("add vseg %g %g to %g %g %d", FixToDbl(itfmx(loc)),
                 FixToDbl(itfmy(from)), FixToDbl(itfmx(loc)),
                 FixToDbl(itfmy(to)), i);
}

void
ReportAddHSeg(Fixed from, Fixed to, Fixed loc, int32_t i)
{
    if (!gShowClrInfo || !gShowHs)
        return;
    PrintMessage("add hseg %g %g to %g %g %d", FixToDbl(itfmx(from)),
                 FixToDbl(itfmy(loc)), FixToDbl(itfmx(to)),
                 FixToDbl(itfmy(loc)), i);
}

void
ReportRemVSeg(Fixed from, Fixed to, Fixed loc)
{
    if (!gShowClrInfo || !gShowVs)
        return;
    PrintMessage("rem vseg %g %g to %g %g", FixToDbl(itfmx(loc)),
                 FixToDbl(itfmy(from)), FixToDbl(itfmx(loc)),
                 FixToDbl(itfmy(to)));
}

void
ReportRemHSeg(Fixed from, Fixed to, Fixed loc)
{
    if (!gShowClrInfo || !gShowHs)
        return;
    PrintMessage("rem hseg %g %g to %g %g", FixToDbl(itfmx(from)),
                 FixToDbl(itfmy(loc)), FixToDbl(itfmx(to)),
                 FixToDbl(itfmy(loc)));
}

void
ReportBandNearMiss(char* str, Fixed loc, Fixed blu)
{
    ReportError("Near miss %s horizontal zone at %g instead of %g.", str,
                FixToDbl(loc), FixToDbl(blu));
}

void
ReportStemNearMiss(bool vert, Fixed w, Fixed minW, Fixed b, Fixed t, bool curve)
{
    ReportError("%s %s stem near miss: %g instead of %g at %g to %g.",
                vert ? "Vertical" : "Horizontal", curve ? "curve" : "linear",
                FixToDbl(w), FixToDbl(minW), FixToDbl(NUMMIN(b, t)),
                FixToDbl(NUMMAX(b, t)));
}

void
ReportColorConflict(Fixed x0, Fixed y0, Fixed x1, Fixed y1, char ch)
{
    unsigned char s[2];
    s[0] = ch;
    s[1] = 0;
    ReportError("  Conflicts with current hints: %g %g %g %g %s.", FixToDbl(x0),
                FixToDbl(y0), FixToDbl(x1), FixToDbl(y1), s);
}

void
ReportDuplicates(Fixed x, Fixed y)
{
    ReportError("Check for duplicate subpath at %g %g.", FixToDbl(x),
                FixToDbl(y));
}

void
ReportBBoxBogus(Fixed llx, Fixed lly, Fixed urx, Fixed ury)
{
    ReportError("Character bounding box looks bogus: %g %g %g %g.",
                FixToDbl(llx), FixToDbl(lly), FixToDbl(urx), FixToDbl(ury));
}

void
ReportMergeHVal(Fixed b0, Fixed t0, Fixed b1, Fixed t1, Fixed v0, Fixed s0,
                Fixed v1, Fixed s1)
{
    if (!gShowClrInfo)
        return;
    PrintMessage(

      "Replace H hints pair at %g %g by %g %g\n\told value ",
      FixToDbl(itfmy(b0)), FixToDbl(itfmy(t0)), FixToDbl(itfmy(b1)),
      FixToDbl(itfmy(t1)));
    PrntVal(v0);
    PrintMessage(" %g new value ", FixToDbl(s0));
    PrntVal(v1);
    PrintMessage(" %g", FixToDbl(s1));
}

void
ReportMergeVVal(Fixed l0, Fixed r0, Fixed l1, Fixed r1, Fixed v0, Fixed s0,
                Fixed v1, Fixed s1)
{
    if (!gShowClrInfo)
        return;
    PrintMessage("Replace V hints pair at %g %g by %g %g\n\told value ",
                 FixToDbl(itfmx(l0)), FixToDbl(itfmx(r0)), FixToDbl(itfmx(l1)),
                 FixToDbl(itfmx(r1)));
    PrntVal(v0);
    PrintMessage(" %g new value ", FixToDbl(s0));
    PrntVal(v1);
    PrintMessage(" %g", FixToDbl(s1));
}

void
ReportPruneHVal(PClrVal val, PClrVal v, int32_t i)
{
    if (!gShowClrInfo)
        return;
    PrintMessage("PruneHVal: %d\n\t", i);
    ShowHVal(val);
    PrintMessage("\n\t");
    ShowHVal(v);
}

void
ReportPruneVVal(PClrVal val, PClrVal v, int32_t i)
{
    if (!gShowClrInfo)
        return;
    PrintMessage("PruneVVal: %d\n\t", i);
    ShowVVal(val);
    PrintMessage("\n\t");
    ShowVVal(v);
}

void
ReportMoveSubpath(PPathElt e, char* s)
{
    Fixed x, y;
    GetEndPoint(e, &x, &y);
    PrintMessage("FYI: Moving subpath %g %g to %s.", FixToDbl(itfmx(x)),
                 FixToDbl(itfmy(y)), s);
}
