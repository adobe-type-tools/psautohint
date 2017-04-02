/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"
#include "machinedep.h"

extern AC_REPORTFUNCPTR libReportCB;
static char S0[512];

double
FixToDbl(Fixed f)
{
    float r;
    acfixtopflt(f, &r);
    return r;
}

void
PrintMessage(char* s)
{
    char msgBuffer[512];
    if ((libReportCB != NULL) && (strlen(s) > 0)) {
        sprintf(msgBuffer, "\t%s", s);
        libReportCB(msgBuffer);
    }
}

void
ReportError(char* s)
{
    if (reportErrors && (libReportCB != NULL))
        PrintMessage(s);
}

void
ReportSmoothError(Fixed x, Fixed y)
{
    (void)sprintf(S0, "Junction at %g %g may need smoothing.",
                  FixToDbl(itfmx(x)), FixToDbl(itfmy(y)));
    ReportError(S0);
}

void
ReportAddFlex(void)
{
    if (hasFlex)
        return;
    hasFlex = true;
    PrintMessage("FYI: added flex operators to this character.");
}

void
ReportClipSharpAngle(Fixed x, Fixed y)
{
    (void)sprintf(S0, "FYI: Too sharp angle at %g %g has been clipped.",
                  FixToDbl(itfmx(x)), FixToDbl(itfmy(y)));
    PrintMessage(S0);
}

void
ReportSharpAngle(Fixed x, Fixed y)
{
    (void)sprintf(S0, "FYI: angle at %g %g is very sharp. Please check.",
                  FixToDbl(itfmx(x)), FixToDbl(itfmy(y)));
    PrintMessage(S0);
}

void
ReportLinearCurve(PPathElt e, Fixed x0, Fixed y0, Fixed x1, Fixed y1)
{
    if (autoLinearCurveFix) {
        e->type = LINETO;
        e->x = e->x3;
        e->y = e->y3;
        (void)sprintf(S0, "Curve from %g %g to %g %g was changed to a line.",
                      FixToDbl(itfmx(x0)), FixToDbl(itfmy(y0)),
                      FixToDbl(itfmx(x1)), FixToDbl(itfmy(y1)));
    } else
        (void)sprintf(S0,
                      "Curve from %g %g to %g %g should be changed to a line.",
                      FixToDbl(itfmx(x0)), FixToDbl(itfmy(y0)),
                      FixToDbl(itfmx(x1)), FixToDbl(itfmy(y1)));
    PrintMessage(S0);
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
        (void)sprintf(S0, "The line from %g %g to %g %g is not exactly %s.",
                      FixToDbl(x0), FixToDbl(y0), FixToDbl(x1), FixToDbl(y1),
                      s);
        ReportError(S0);
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
            LogMsg("Malformed path list.\n", LOGERROR, NONFATALERROR, true);
            return;
    }
    (void)sprintf(globmsg, "Path for %s character has a %s where a moveto was "
                           "expected.\n  The file is probably truncated.",
                  fileName, s);
    LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
}

void
ReportMissingClosePath(void)
{
    (void)sprintf(
      globmsg,
      "Missing closepath in %s character.\n  The file is probably truncated.",
      fileName);
    LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
}

void
ReportTryFlexNearMiss(Fixed x0, Fixed y0, Fixed x2, Fixed y2)
{
    (void)sprintf(S0, "Curves from %g %g to %g %g near miss for adding flex.",
                  FixToDbl(itfmx(x0)), FixToDbl(itfmy(y0)), FixToDbl(itfmx(x2)),
                  FixToDbl(itfmy(y2)));
    ReportError(S0);
}

void
ReportTryFlexError(bool CPflg, Fixed x, Fixed y)
{
    (void)sprintf(
      S0, CPflg ? "Please move closepath from %g %g so can add flex."
                : "Please remove zero length element at %g %g so can add flex.",
      FixToDbl(itfmx(x)), FixToDbl(itfmy(y)));
    ReportError(S0);
}

void
ReportSplit(PPathElt e)
{
    Fixed x0, y0, x1, y1;
    GetEndPoints(e, &x0, &y0, &x1, &y1);
    (void)sprintf(
      S0, "FYI: the element that goes from %g %g to %g %g has been split.",
      FixToDbl(itfmx(x0)), FixToDbl(itfmy(y0)), FixToDbl(itfmx(x1)),
      FixToDbl(itfmy(y1)));
    PrintMessage(S0);
}

void
AskForSplit(PPathElt e)
{
    Fixed x0, y0, x1, y1;
    if (e->type == MOVETO)
        e = GetClosedBy(e);
    GetEndPoints(e, &x0, &y0, &x1, &y1);
    (void)sprintf(S0, "Please split the element that goes from %g %g to %g %g.",
                  FixToDbl(itfmx(x0)), FixToDbl(itfmy(y0)), FixToDbl(itfmx(x1)),
                  FixToDbl(itfmy(y1)));
    ReportError(S0);
}

void
ReportPossibleLoop(PPathElt e)
{
    Fixed x0, y0, x1, y1;
    if (e->type == MOVETO)
        e = GetClosedBy(e);
    GetEndPoints(e, &x0, &y0, &x1, &y1);
    (void)sprintf(
      S0,
      "Possible loop in element that goes from %g %g to %g %g. Please check.",
      FixToDbl(itfmx(x0)), FixToDbl(itfmy(y0)), FixToDbl(itfmx(x1)),
      FixToDbl(itfmy(y1)));
    ReportError(S0);
}

void
ReportConflictCheck(PPathElt e, PPathElt conflict, PPathElt cp)
{
    Fixed ex, ey, cx, cy, cpx, cpy;
    GetEndPoint(e, &ex, &ey);
    GetEndPoint(conflict, &cx, &cy);
    GetEndPoint(cp, &cpx, &cpy);
    (void)sprintf(S0, "Check e %g %g conflict %g %g cp %g %g.",
                  FixToDbl(itfmx(ex)), FixToDbl(itfmy(ey)), FixToDbl(itfmx(cx)),
                  FixToDbl(itfmy(cy)), FixToDbl(itfmx(cpx)),
                  FixToDbl(itfmy(cpy)));
    ReportError(S0);
}

void
ReportConflictCnt(PPathElt e, int32_t cnt)
{
    Fixed ex, ey;
    GetEndPoint(e, &ex, &ey);
    (void)sprintf(S0, "%g %g conflict count = %d", FixToDbl(itfmx(ex)),
                  FixToDbl(itfmy(ey)), cnt);
    ReportError(S0);
}

void
ReportRemFlare(PPathElt e, PPathElt e2, bool hFlg, int32_t i)
{
    Fixed ex1, ey1, ex2, ey2;
    if (!showClrInfo)
        return;
    GetEndPoint(e, &ex1, &ey1);
    GetEndPoint(e2, &ex2, &ey2);
    (void)sprintf(S0, "Removed %s flare at %g %g by %g %g : %d.",
                  hFlg ? "horizontal" : "vertical", FixToDbl(itfmx(ex1)),
                  FixToDbl(itfmy(ey1)), FixToDbl(itfmx(ex2)),
                  FixToDbl(itfmy(ey2)), i);
    PrintMessage(S0);
}

void
ReportRemConflict(PPathElt e)
{
    Fixed ex, ey;
    if (!showClrInfo)
        return;
    GetEndPoint(e, &ex, &ey);
    (void)sprintf(S0, "Removed conflicting hints at %g %g.",
                  FixToDbl(itfmx(ex)), FixToDbl(itfmy(ey)));
    ReportError(S0);
}

void
ReportRotateSubpath(PPathElt e)
{
    Fixed ex, ey;
    if (!showClrInfo)
        return;
    GetEndPoint(e, &ex, &ey);
    sprintf(S0, "FYI: changed closepath to %g %g.", FixToDbl(itfmx(ex)),
            FixToDbl(itfmy(ey)));
    PrintMessage(S0);
}

void
ReportRemShortColors(Fixed ex, Fixed ey)
{
    if (!showClrInfo)
        return;
    sprintf(S0, "Removed hints from short element at %g %g.",
            FixToDbl(itfmx(ex)), FixToDbl(itfmy(ey)));
    PrintMessage(S0);
}

static void
PrntVal(Fixed v)
{
    if (v >= FixInt(100000))
        sprintf(S0, "%d", FTrunc(v));
    else
        sprintf(S0, "%g", FixToDbl(v));
    PrintMessage(S0);
}

static void
ShwHV(PClrVal val)
{
    Fixed bot, top;
    bot = itfmy(val->vLoc1);
    top = itfmy(val->vLoc2);
    sprintf(S0, "b %g t %g v ", FixToDbl(bot), FixToDbl(top));
    PrintMessage(S0);
    PrntVal(val->vVal);
    sprintf(S0, " s %g", FixToDbl(val->vSpc));
    PrintMessage(S0);
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
    sprintf(S0, " l1 %g r1 %g ", FixToDbl(l), FixToDbl(r));
    PrintMessage(S0);
    seg = val->vSeg2;
    l = itfmx(seg->sMin);
    r = itfmx(seg->sMax);
    sprintf(S0, " l2 %g r2 %g", FixToDbl(l), FixToDbl(r));
    PrintMessage(S0);
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
    sprintf(S0, "l %g r %g v ", FixToDbl(lft), FixToDbl(rht));
    PrintMessage(S0);
    PrntVal(val->vVal);
    sprintf(S0, " s %g", FixToDbl(val->vSpc));
    PrintMessage(S0);
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
    sprintf(S0, " b1 %g t1 %g ", FixToDbl(b), FixToDbl(t));
    PrintMessage(S0);
    seg = val->vSeg2;
    b = itfmy(seg->sMin);
    t = itfmy(seg->sMax);
    sprintf(S0, " b2 %g t2 %g", FixToDbl(b), FixToDbl(t));
    PrintMessage(S0);
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
        sprintf(S0, "FndBstVal: sLoc %g sLft %g sRght %g ",
                FixToDbl(itfmy(seg->sLoc)), FixToDbl(itfmx(seg->sMin)),
                FixToDbl(itfmx(seg->sMax)));
        PrintMessage(S0);
        if (val)
            ShwHV(val);
        else
            PrintMessage("NULL");
    } else {
        sprintf(S0, "FndBstVal: sLoc %g sBot %g sTop %g ",
                FixToDbl(itfmx(seg->sLoc)), FixToDbl(itfmy(seg->sMin)),
                FixToDbl(itfmy(seg->sMax)));
        PrintMessage(S0);
        if (val)
            ShwVV(val);
        else
            PrintMessage("NULL");
    }
}

void
ReportCarry(Fixed l0, Fixed l1, Fixed loc, PClrVal clrs, bool vert)
{
    if (!showClrInfo)
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
    sprintf(S0, " carry to %g in [%g..%g]", FixToDbl(loc), FixToDbl(l0),
            FixToDbl(l1));
    PrintMessage(S0);
}

void
ReportBestCP(PPathElt e, PPathElt cp)
{
    Fixed ex, ey, px, py;
    GetEndPoint(e, &ex, &ey);
    if (cp != NULL) {
        GetEndPoint(cp, &px, &py);
        sprintf(S0, "%g %g best cp at %g %g", FixToDbl(itfmx(ex)),
                FixToDbl(itfmy(ey)), FixToDbl(itfmx(px)), FixToDbl(itfmy(py)));
    } else
        sprintf(S0, "%g %g no best cp", FixToDbl(itfmx(ex)),
                FixToDbl(itfmy(ey)));
    PrintMessage(S0);
}

void
LogColorInfo(PClrPoint pl)
{
    char c = pl->c;
    Fixed lft, rht, top, bot, wdth;
    if (c == 'y' || c == 'm') { /* vertical lines */
        lft = pl->x0;
        rht = pl->x1;
        (void)sprintf(S0, "%4g  %-30s%5g%5g\n", FixToDbl(rht - lft), fileName,
                      FixToDbl(lft), FixToDbl(rht));
    } else {
        bot = pl->y0;
        top = pl->y1;
        wdth = top - bot;
        if (wdth == -FixInt(21) || wdth == -FixInt(20))
            return; /* ghost pair */
        (void)sprintf(S0, "%4g  %-30s%5g%5g\n", FixToDbl(wdth), fileName,
                      FixToDbl(bot), FixToDbl(top));
    }
    PrintMessage(S0);
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
    e = pathStart;
    while (e != NULL) {
        hLst = e->Hs;
        vLst = e->Vs;
        if ((hLst != NULL) || (vLst != NULL)) {
            GetEndPoint(e, &x, &y);
            x = itfmx(x);
            y = itfmy(y);
            sprintf(S0, "x %g y %g ", FixToDbl(x), FixToDbl(y));
            PrintMessage(S0);
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
    if (!showClrInfo || !showVs)
        return;
    (void)sprintf(S0, "add vseg %g %g to %g %g %d", FixToDbl(itfmx(loc)),
                  FixToDbl(itfmy(from)), FixToDbl(itfmx(loc)),
                  FixToDbl(itfmy(to)), i);
    PrintMessage(S0);
}

void
ReportAddHSeg(Fixed from, Fixed to, Fixed loc, int32_t i)
{
    if (!showClrInfo || !showHs)
        return;
    (void)sprintf(S0, "add hseg %g %g to %g %g %d", FixToDbl(itfmx(from)),
                  FixToDbl(itfmy(loc)), FixToDbl(itfmx(to)),
                  FixToDbl(itfmy(loc)), i);
    PrintMessage(S0);
}

void
ReportRemVSeg(Fixed from, Fixed to, Fixed loc)
{
    if (!showClrInfo || !showVs)
        return;
    (void)sprintf(S0, "rem vseg %g %g to %g %g", FixToDbl(itfmx(loc)),
                  FixToDbl(itfmy(from)), FixToDbl(itfmx(loc)),
                  FixToDbl(itfmy(to)));
    PrintMessage(S0);
}

void
ReportRemHSeg(Fixed from, Fixed to, Fixed loc)
{
    if (!showClrInfo || !showHs)
        return;
    (void)sprintf(S0, "rem hseg %g %g to %g %g", FixToDbl(itfmx(from)),
                  FixToDbl(itfmy(loc)), FixToDbl(itfmx(to)),
                  FixToDbl(itfmy(loc)));
    PrintMessage(S0);
}

void
ReportBandError(char* str, Fixed loc, Fixed blu)
{
    (void)sprintf(S0, "Near miss %s horizontal zone at %g instead of %g.", str,
                  FixToDbl(loc), FixToDbl(blu));
    ReportError(S0);
}
void
ReportBandNearMiss(char* str, Fixed loc, Fixed blu)
{
    (void)sprintf(S0, "Near miss %s horizontal zone at %g instead of %g.", str,
                  FixToDbl(loc), FixToDbl(blu));
    ReportError(S0);
}

void
ReportStemNearMiss(bool vert, Fixed w, Fixed minW, Fixed b, Fixed t, bool curve)
{
    (void)sprintf(S0, "%s %s stem near miss: %g instead of %g at %g to %g.",
                  vert ? "Vertical" : "Horizontal", curve ? "curve" : "linear",
                  FixToDbl(w), FixToDbl(minW), FixToDbl(NUMMIN(b, t)),
                  FixToDbl(NUMMAX(b, t)));
    ReportError(S0);
}

void
ReportColorConflict(Fixed x0, Fixed y0, Fixed x1, Fixed y1, char ch)
{
    unsigned char s[2];
    s[0] = ch;
    s[1] = 0;
    (void)sprintf(S0, "  Conflicts with current hints: %g %g %g %g %s.",
                  FixToDbl(x0), FixToDbl(y0), FixToDbl(x1), FixToDbl(y1), s);
    ReportError(S0);
}

void
ReportDuplicates(Fixed x, Fixed y)
{
    (void)sprintf(S0, "Check for duplicate subpath at %g %g.", FixToDbl(x),
                  FixToDbl(y));
    ReportError(S0);
}

void
ReportBBoxBogus(Fixed llx, Fixed lly, Fixed urx, Fixed ury)
{
    (void)sprintf(S0, "Character bounding box looks bogus: %g %g %g %g.",
                  FixToDbl(llx), FixToDbl(lly), FixToDbl(urx), FixToDbl(ury));
    ReportError(S0);
}

void
ReportMergeHVal(Fixed b0, Fixed t0, Fixed b1, Fixed t1, Fixed v0, Fixed s0,
                Fixed v1, Fixed s1)
{
    if (!showClrInfo)
        return;
    sprintf(S0, "Replace H hints pair at %g %g by %g %g\n\told value ",
            FixToDbl(itfmy(b0)), FixToDbl(itfmy(t0)), FixToDbl(itfmy(b1)),
            FixToDbl(itfmy(t1)));
    PrintMessage(S0);
    PrntVal(v0);
    sprintf(S0, " %g new value ", FixToDbl(s0));
    PrintMessage(S0);
    PrntVal(v1);
    sprintf(S0, " %g", FixToDbl(s1));
    PrintMessage(S0);
}

void
ReportMergeVVal(Fixed l0, Fixed r0, Fixed l1, Fixed r1, Fixed v0, Fixed s0,
                Fixed v1, Fixed s1)
{
    if (!showClrInfo)
        return;
    sprintf(S0, "Replace V hints pair at %g %g by %g %g\n\told value ",
            FixToDbl(itfmx(l0)), FixToDbl(itfmx(r0)), FixToDbl(itfmx(l1)),
            FixToDbl(itfmx(r1)));
    PrintMessage(S0);
    PrntVal(v0);
    sprintf(S0, " %g new value ", FixToDbl(s0));
    PrintMessage(S0);
    PrntVal(v1);
    sprintf(S0, " %g", FixToDbl(s1));
    PrintMessage(S0);
}

void
ReportPruneHVal(PClrVal val, PClrVal v, int32_t i)
{
    if (!showClrInfo)
        return;
    sprintf(S0, "PruneHVal: %d\n\t", i);
    PrintMessage(S0);
    ShowHVal(val);
    PrintMessage("\n\t");
    ShowHVal(v);
}

void
ReportPruneVVal(PClrVal val, PClrVal v, int32_t i)
{
    if (!showClrInfo)
        return;
    sprintf(S0, "PruneVVal: %d\n\t", i);
    PrintMessage(S0);
    ShowVVal(val);
    PrintMessage("\n\t");
    ShowVVal(v);
}

void
ReportMoveSubpath(PPathElt e, char* s)
{
    Fixed x, y;
    GetEndPoint(e, &x, &y);
    sprintf(S0, "FYI: Moving subpath %g %g to %s.", FixToDbl(itfmx(x)),
            FixToDbl(itfmy(y)), s);
    PrintMessage(S0);
}
