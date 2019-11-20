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

static bool g_xflat, g_yflat, g_xdone, g_ydone, g_bbquit;
static int32_t g_xstate, g_ystate, g_xstart, g_ystart;
static Fixed g_x0, g_cy0, g_x1, g_cy1, g_xloc, g_yloc;
static Fixed g_x, g_y, g_xnxt, g_ynxt;
static Fixed g_yflatstartx, g_yflatstarty, g_yflatendx, g_yflatendy;
static Fixed g_xflatstarty, g_xflatstartx, g_xflatendx, g_xflatendy;
static bool g_vert, g_started, g_reCheckSmooth;
static Fixed g_loc, g_frst, g_lst, g_fltnvalue;
static PathElt* g_e;
static bool g_forMultiMaster = false, g_inflPtFound = false;

#define STARTING (0)
#define goingUP (1)
#define goingDOWN (2)

/* DEBUG 8 BIT. The SDELTA value must tbe increased by 2 due to change in
 * coordinate system from 7 to 8 bit FIXED fraction. */

#define SDELTA (FixInt(8))
#define SDELTA3 (FixInt(10))

static void
chkBad(void)
{
    g_reCheckSmooth = ResolveConflictBySplit(g_e, false, NULL, NULL);
    ;
}

#define GrTan(n, d) (abs(n) * 100 > abs(d) * gSCurveTan)
#define LsTan(n, d) (abs(n) * 100 < abs(d) * gSCurveTan)

static void
chkYDIR(void)
{
    if (g_y > g_yloc) { /* going up */
        if (g_ystate == goingUP)
            return;
        if (g_ystate == STARTING)
            g_ystart = g_ystate = goingUP;
        else /*if (ystate == goingDOWN)*/ {
            if (g_ystart == goingUP) {
                g_yflatendx = g_xloc;
                g_yflatendy = g_yloc;
            } else if (!g_yflat) {
                g_yflatstartx = g_xloc;
                g_yflatstarty = g_yloc;
                g_yflat = true;
            }
            g_ystate = goingUP;
        }
    } else if (g_y < g_yloc) { /* going down */
        if (g_ystate == goingDOWN)
            return;
        if (g_ystate == STARTING)
            g_ystart = g_ystate = goingDOWN;
        else /*if (ystate == goingUP)*/ {
            if (g_ystart == goingDOWN) {
                g_yflatendx = g_xloc;
                g_yflatendy = g_yloc;
            } else if (!g_yflat) {
                g_yflatstartx = g_xloc;
                g_yflatstarty = g_yloc;
                g_yflat = true;
            }
            g_ystate = goingDOWN;
        }
    }
}

static void
chkYFLAT(void)
{
    if (!g_yflat) {
        if (LsTan(g_y - g_yloc, g_x - g_xloc)) {
            g_yflat = true;
            g_yflatstartx = g_xloc;
            g_yflatstarty = g_yloc;
        }
        return;
    }
    if (g_ystate != g_ystart)
        return;
    if (GrTan(g_y - g_yloc, g_x - g_xloc)) {
        g_yflatendx = g_xloc;
        g_yflatendy = g_yloc;
        g_ydone = true;
    }
}

static void
chkXFLAT(void)
{
    if (!g_xflat) {
        if (LsTan(g_x - g_xloc, g_y - g_yloc)) {
            g_xflat = true;
            g_xflatstartx = g_xloc;
            g_xflatstarty = g_yloc;
        }
        return;
    }
    if (g_xstate != g_xstart)
        return;
    if (GrTan(g_x - g_xloc, g_y - g_yloc)) {
        g_xflatendx = g_xloc;
        g_xflatendy = g_yloc;
        g_xdone = true;
    }
}

static void
chkXDIR(void)
{
    if (g_x > g_xloc) { /* going up */
        if (g_xstate == goingUP)
            return;
        if (g_xstate == STARTING)
            g_xstart = g_xstate = goingUP;
        else /*if (xstate == goingDOWN)*/ {
            if (g_xstart == goingUP) {
                g_xflatendx = g_xloc;
                g_xflatendy = g_yloc;
            } else if (!g_xflat) {
                g_xflatstartx = g_xloc;
                g_xflatstarty = g_yloc;
                g_xflat = true;
            }
            g_xstate = goingUP;
        }
    } else if (g_x < g_xloc) {
        if (g_xstate == goingDOWN)
            return;
        if (g_xstate == STARTING)
            g_xstart = g_xstate = goingDOWN;
        else /*if (xstate == goingUP)*/ {
            if (g_xstart == goingDOWN) {
                g_xflatendx = g_xloc;
                g_xflatendy = g_yloc;
            } else if (!g_xflat) {
                g_xflatstartx = g_xloc;
                g_xflatstarty = g_yloc;
                g_xflat = true;
            }
            g_xstate = goingDOWN;
        }
    }
}

static void
chkDT(Cd c)
{
    Fixed loc;

    g_x = c.x;
    g_y = c.y;
    g_ynxt = g_y;
    g_xnxt = g_x;
    if (!g_ydone) {
        chkYDIR();
        chkYFLAT();
        if (g_ydone && g_yflat && abs(g_yflatstarty - g_cy0) > SDELTA &&
            abs(g_cy1 - g_yflatendy) > SDELTA) {
            if ((g_ystart == goingUP && g_yflatstarty - g_yflatendy > SDELTA) ||
                (g_ystart == goingDOWN && g_yflatendy - g_yflatstarty > SDELTA)) {
                if (gEditGlyph && !g_forMultiMaster)
                    chkBad();
                return;
            }
            if (abs(g_yflatstartx - g_yflatendx) > SDELTA3) {
                DEBUG_ROUND(g_yflatstartx);
                DEBUG_ROUND(g_yflatendx);
                DEBUG_ROUND(g_yflatstarty);
                DEBUG_ROUND(g_yflatendy);

                loc = (g_yflatstarty + g_yflatendy) / 2;
                DEBUG_ROUND(loc);

                if (!g_forMultiMaster) {
                    AddHSegment(g_yflatstartx, g_yflatendx, loc, g_e, NULL, sCURVE,
                                13);
                } else {
                    g_inflPtFound = true;
                    g_fltnvalue = -loc;
                }
            }
        }
    }
    if (!g_xdone) {
        chkXDIR();
        chkXFLAT();
        if (g_xdone && g_xflat && abs(g_xflatstartx - g_x0) > SDELTA &&
            abs(g_x1 - g_xflatendx) > SDELTA) {
            if ((g_xstart == goingUP && g_xflatstartx - g_xflatendx > SDELTA) ||
                (g_xstart == goingDOWN && g_xflatendx - g_xflatstartx > SDELTA)) {
                if (gEditGlyph && !g_forMultiMaster)
                    chkBad();
                return;
            }
            if (abs(g_xflatstarty - g_xflatendy) > SDELTA3) {
                DEBUG_ROUND(g_xflatstarty);
                DEBUG_ROUND(g_xflatendy);
                DEBUG_ROUND(g_xflatstartx);
                DEBUG_ROUND(g_xflatendx);

                loc = (g_xflatstartx + g_xflatendx) / 2;
                DEBUG_ROUND(loc);

                if (!g_forMultiMaster)

                {
                    AddVSegment(g_xflatstarty, g_xflatendy, loc, g_e, NULL, sCURVE,
                                13);
                } else {
                    g_inflPtFound = true;
                    g_fltnvalue = loc;
                }
            }
        }
    }
    g_xloc = g_xnxt;
    g_yloc = g_ynxt;
}

#define FQ(x) ((int32_t)((x) >> 6))
static int32_t
CPDirection(Fixed x1, Fixed cy1, Fixed x2, Fixed y2, Fixed x3, Fixed y3)
{
    int32_t q, q1, q2, q3;
    q1 = FQ(x2) * FQ(y3 - cy1);
    q2 = FQ(x1) * FQ(y2 - y3);
    q3 = FQ(x3) * FQ(cy1 - y2);
    q = q1 + q2 + q3;
    if (q > 0)
        return 1;
    if (q < 0)
        return -1;
    return 0;
}

void
RMovePoint(Fixed dx, Fixed dy, int32_t whichcp, PathElt* e)
{
    if (whichcp == cpStart) {
        e = e->prev;
        whichcp = cpEnd;
    }
    if (whichcp == cpEnd) {
        if (e->type == CLOSEPATH)
            e = GetDest(e);
        if (e->type == CURVETO) {
            e->x3 += dx;
            e->y3 += dy;
        } else {
            e->x += dx;
            e->y += dy;
        }
        return;
    }
    if (whichcp == cpCurve1) {
        e->x1 += dx;
        e->y1 += dy;
        return;
    }
    if (whichcp == cpCurve2) {
        e->x2 += dx;
        e->y2 += dy;
        return;
    }
    LogMsg(LOGERROR, NONFATALERROR, "Malformed path list.");
}

void
Delete(PathElt* e)
{
    PathElt *nxt, *prv;
    nxt = e->next;
    prv = e->prev;
    if (nxt != NULL)
        nxt->prev = prv;
    else
        gPathEnd = prv;
    if (prv != NULL)
        prv->next = nxt;
    else
        gPathStart = nxt;
}

/* This procedure is called from BuildFont when adding hints
 to base designs of a multi-master font. */
bool
GetInflectionPoint(Fixed px, Fixed py, Fixed px1, Fixed pcy1, Fixed px2,
                   Fixed py2, Fixed px3, Fixed py3, Fixed* inflPt)
{
    FltnRec fltnrec;
    Cd c0, c1, c2, c3;

    fltnrec.report = chkDT;
    c0.x = px;
    c0.y = -py;
    c1.x = px1;
    c1.y = -pcy1;
    c2.x = px2;
    c2.y = -py2;
    c3.x = px3;
    c3.y = -py3;
    g_xstate = g_ystate = STARTING;
    g_xdone = g_ydone = g_xflat = g_yflat = g_inflPtFound = false;
    g_x0 = c0.x;
    g_cy0 = c0.y;
    g_x1 = c3.x;
    g_cy1 = c3.y;
    g_xloc = g_x0;
    g_yloc = g_cy0;
    g_forMultiMaster = true;
    FltnCurve(c0, c1, c2, c3, &fltnrec);
    if (g_inflPtFound)
        *inflPt = g_fltnvalue;
    return g_inflPtFound;
}

static void
CheckSCurve(PathElt* ee)
{
    FltnRec fr;
    Cd c0, c1, c2, c3;
    if (ee->type != CURVETO) {
        LogMsg(LOGERROR, NONFATALERROR, "Malformed path list.");
    }

    GetEndPoint(ee->prev, &c0.x, &c0.y);
    fr.report = chkDT;
    c1.x = ee->x1;
    c1.y = ee->y1;
    c2.x = ee->x2;
    c2.y = ee->y2;
    c3.x = ee->x3;
    c3.y = ee->y3;
    g_xstate = g_ystate = STARTING;
    g_xdone = g_ydone = g_xflat = g_yflat = false;
    g_x0 = c0.x;
    g_cy0 = c0.y;
    g_x1 = c3.x;
    g_cy1 = c3.y;
    g_xloc = g_x0;
    g_yloc = g_cy0;
    g_e = ee;
    g_forMultiMaster = false;
    FltnCurve(c0, c1, c2, c3, &fr);
}

static void
CheckZeroLength(void)
{
    PathElt *e, *NxtE;
    Fixed x0, cy0, x1, cy1, x2, y2, x3, y3;
    if ((!gEditGlyph) || g_forMultiMaster)
    {
        /* Do not change topology when hinting MM fonts,
         and do not edit glyphs if not requested */
        return;
    }
    e = gPathStart;
    while (e != NULL) { /* delete zero length elements */
        NxtE = e->next;
        GetEndPoints(e, &x0, &cy0, &x1, &cy1);
        if (e->type == LINETO && x0 == x1 && cy0 == cy1) {
            Delete(e);
            goto Nxt1;
        }
        if (e->type == CURVETO) {
            x2 = e->x1;
            y2 = e->y1;
            x3 = e->x2;
            y3 = e->y2;
            if (x0 == x1 && cy0 == cy1 && x2 == x1 && x3 == x1 && y2 == cy1 &&
                y3 == cy1) {
                Delete(e);
                goto Nxt1;
            }
        }
    Nxt1:
        e = NxtE;
    }
}

void
CheckSmooth(void)
{
    PathElt *e, *nxt, *NxtE;
    bool recheck;
    Fixed x0, cy0, x1, cy1, x2, y2, x3, y3, smdiff, xx, yy;
    CheckZeroLength();
restart:
    g_reCheckSmooth = false;
    recheck = false;
    e = gPathStart;
    while (e != NULL) {
        NxtE = e->next;
        if (e->type == MOVETO || IsTiny(e) || e->isFlex)
            goto Nxt;
        GetEndPoint(e, &x1, &cy1);
        if (e->type == CURVETO) {
            int32_t cpd0, cpd1;
            x2 = e->x1;
            y2 = e->y1;
            x3 = e->x2;
            y3 = e->y2;
            GetEndPoint(e->prev, &x0, &cy0);
            cpd0 = CPDirection(x0, cy0, x2, y2, x3, y3);
            cpd1 = CPDirection(x2, y2, x3, y3, x1, cy1);
            if (ProdLt0(cpd0, cpd1))
                CheckSCurve(e);
        }
        nxt = NxtForBend(e, &x2, &y2, &xx, &yy);
        if (nxt->isFlex)
            goto Nxt;
        PrvForBend(nxt, &x0, &cy0);
        if (!CheckSmoothness(x0, cy0, x1, cy1, x2, y2, &smdiff))
            LogMsg(INFO, OK, "Junction at %g %g may need smoothing.",
                   FixToDbl(x1), FixToDbl(-cy1));
        if (smdiff > FixInt(160))
            LogMsg(INFO, OK, "Too sharp angle at %g %g has been clipped.",
                   FixToDbl(x1), FixToDbl(-cy1));
    Nxt:
        e = NxtE;
    }
    if (g_reCheckSmooth)
        goto restart;
    if (!recheck)
        return;
    CheckZeroLength();
    /* in certain cases clip sharp point can produce a zero length line */
}

#define BBdist                                                                 \
    (FixInt(20)) /* DEBUG 8 BIT. DOuble value from 10 to 20 for change in      \
                    coordinate system. */

static void
chkBBDT(Cd c)
{
    Fixed x = c.x, y = c.y;
    if (g_bbquit)
        return;
    if (g_vert) {
        g_lst = y;
        if (!g_started && abs(x - g_loc) <= BBdist) {
            g_started = true;
            g_frst = y;
        } else if (g_started && abs(x - g_loc) > BBdist)
            g_bbquit = true;
    } else {
        g_lst = x;
        if (!g_started && abs(y - g_loc) <= BBdist) {
            g_started = true;
            g_frst = x;
        } else if (g_started && abs(y - g_loc) > BBdist)
            g_bbquit = true;
    }
}

void
CheckForMultiMoveTo(void)
{
    PathElt* e = gPathStart;
    bool moveto;
    moveto = false;
    while (e != NULL) {
        if (e->type != MOVETO)
            moveto = false;
        else if (!moveto)
            moveto = true;
        else
            Delete(e->prev); /* delete previous moveto */
        e = e->next;
    }
}

void
CheckBBoxEdge(PathElt* e, bool vrt, Fixed lc, Fixed* pf, Fixed* pl)
{
    FltnRec fr;
    Cd c0, c1, c2, c3;
    if (e->type != CURVETO) {
        LogMsg(LOGERROR, NONFATALERROR, "Malformed path list.");
    }

    GetEndPoint(e->prev, &c0.x, &c0.y);
    fr.report = chkBBDT;
    g_bbquit = false;
    c1.x = e->x1;
    c1.y = e->y1;
    c2.x = e->x2;
    c2.y = e->y2;
    c3.x = e->x3;
    c3.y = e->y3;
    g_loc = lc;
    g_vert = vrt;
    g_started = false;
    chkBBDT(c0);
    FltnCurve(c0, c1, c2, c3, &fr);
    *pf = g_frst;
    *pl = g_lst;
}

static void
MakeColinear(Fixed tx, Fixed ty, Fixed x0, Fixed cy0, Fixed x1, Fixed cy1,
             Fixed* xptr, Fixed* yptr)
{
    Fixed dx, dy;
    float rdx, rdy, dxdy, dxsq, dysq, dsq, xi, yi, rx, ry, rx0, ry0;
    dx = x1 - x0;
    dy = cy1 - cy0;
    if (dx == 0 && dy == 0) {
        *xptr = tx;
        *yptr = ty;
        return;
    }
    if (dx == 0) {
        *xptr = x0;
        *yptr = ty;
        return;
    }
    if (dy == 0) {
        *xptr = tx;
        *yptr = cy0;
        return;
    }
    acfixtopflt(dx, &rdx);
    acfixtopflt(dy, &rdy);
    acfixtopflt(x0, &rx0);
    acfixtopflt(cy0, &ry0);
    acfixtopflt(tx, &rx);
    acfixtopflt(ty, &ry);
    dxdy = rdx * rdy;
    dxsq = rdx * rdx;
    dysq = rdy * rdy;
    dsq = dxsq + dysq;
    xi = (rx * dxsq + rx0 * dysq + (ry - ry0) * dxdy) / dsq;
    yi = ry0 + ((xi - rx0) * rdy) / rdx;
    *xptr = acpflttofix(&xi);
    *yptr = acpflttofix(&yi);
}

#define DEG(x) ((x)*57.29577951308232088)
static Fixed
ATan(Fixed a, Fixed b)
{
    float aa, bb, cc;
    acfixtopflt(a, &aa);
    acfixtopflt(b, &bb);
    cc = (float)DEG(atan2((double)aa, (double)bb));
    while (cc < 0)
        cc += 360.0f;
    return acpflttofix(&cc);
}

bool
CheckSmoothness(Fixed x0, Fixed cy0, Fixed x1, Fixed cy1, Fixed x2, Fixed y2,
                Fixed* pd)
{
    Fixed dx, dy, smdiff, smx, smy, at0, at1;
    dx = x0 - x1;
    dy = cy0 - cy1;
    *pd = 0;
    if (dx == 0 && dy == 0)
        return true;
    at0 = ATan(dx, dy);
    dx = x1 - x2;
    dy = cy1 - y2;
    if (dx == 0 && dy == 0)
        return true;
    at1 = ATan(dx, dy);
    smdiff = at0 - at1;
    if (smdiff < 0)
        smdiff = -smdiff;
    if (smdiff >= FixInt(180))
        smdiff = FixInt(360) - smdiff;
    *pd = smdiff;
    if (smdiff == 0 || smdiff > FixInt(30))
        return true;
    MakeColinear(x1, cy1, x0, cy0, x2, y2, &smx, &smy);
    smx = FHalfRnd(smx);
    smy = FHalfRnd(smy);
    /* DEBUG 8 BIT. Double hard coded distance values, for change from 7 to 8
     * bits for fractions. */
    return abs(smx - x1) < FixInt(4) && abs(smy - cy1) < FixInt(4);
}

void
CheckForDups(void)
{
    PathElt *ob, *nxt;
    Fixed x, y;
    ob = gPathStart;
    while (ob != NULL) {
        nxt = ob->next;
        if (ob->type == MOVETO) {
            x = ob->x;
            y = ob->y;
            ob = nxt;
            while (ob != NULL) {
                if (ob->type == MOVETO && x == ob->x && y == ob->y)
                    goto foundMatch;
                ob = ob->next;
            }
        }
        ob = nxt;
    }
    return;
foundMatch:
    y = -y;
    ReportDuplicates(x, y);
}

void
MoveSubpathToEnd(PathElt* e)
{
    PathElt *subEnd, *subStart, *subNext, *subPrev;
    subEnd = (e->type == CLOSEPATH) ? e : GetClosedBy(e);
    subStart = GetDest(subEnd);
    if (subEnd == gPathEnd)
        return; /* already at end */
    subNext = subEnd->next;
    if (subStart == gPathStart) {
        gPathStart = subNext;
        subNext->prev = NULL;
    } else {
        subPrev = subStart->prev;
        subPrev->next = subNext;
        subNext->prev = subPrev;
    }
    gPathEnd->next = subStart;
    subStart->prev = gPathEnd;
    subEnd->next = NULL;
    gPathEnd = subEnd;
}
