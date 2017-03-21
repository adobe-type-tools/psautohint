/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/
/* check.c */

#include "ac.h"
#include "bftoac.h"
#include "machinedep.h"

static bool xflat, yflat, xdone, ydone, bbquit;
static int32_t xstate, ystate, xstart, ystart;
static Fixed x0, cy0, x1, cy1, xloc, yloc;
static Fixed x, y, xnxt, ynxt;
static Fixed yflatstartx, yflatstarty, yflatendx, yflatendy;
static Fixed xflatstarty, xflatstartx, xflatendx, xflatendy;
static bool vert, started, reCheckSmooth;
static Fixed loc, frst, lst, fltnvalue;
static PPathElt e;
static bool forMultiMaster = false, inflPtFound = false;

#define STARTING (0)
#define goingUP (1)
#define goingDOWN (2)

/* DEBUG 8 BIT. The SDELTA value must tbe increased by 2 due to change in coordinate system from 7 to 8 bit FIXED fraction. */

#define SDELTA (FixInt(8))
#define SDELTA3 (FixInt(10))

static void chkBad() {
    reCheckSmooth = ResolveConflictBySplit(e,false,NULL,NULL);;
}

#define GrTan(n,d) (abs(n)*100 > abs(d)*sCurveTan)
#define LsTan(n,d) (abs(n)*100 < abs(d)*sCurveTan)

static void chkYDIR() {
    if (y > yloc) { /* going up */
        if (ystate == goingUP) return;
        if (ystate == STARTING) ystart = ystate = goingUP;
        else /*if (ystate == goingDOWN)*/ {
            if (ystart == goingUP) {
                yflatendx = xloc; yflatendy = yloc; }
            else if (!yflat) {
                yflatstartx = xloc; yflatstarty = yloc; yflat = true; }
            ystate = goingUP; }
    }
    else if (y < yloc) { /* going down */
        if (ystate == goingDOWN) return;
        if (ystate == STARTING) ystart = ystate = goingDOWN;
        else /*if (ystate == goingUP)*/ {
            if (ystart == goingDOWN) {
                yflatendx = xloc; yflatendy = yloc; }
            else if (!yflat) {
                yflatstartx = xloc; yflatstarty = yloc; yflat = true; }
            ystate = goingDOWN; }
    }
}

static void chkYFLAT() {
    Fixed abstmp;
    if (!yflat) {
        if (LsTan(y-yloc, x-xloc)) {
            yflat = true; yflatstartx = xloc; yflatstarty = yloc; }
        return; }
    if (ystate != ystart) return;
    if (GrTan(y-yloc, x-xloc)) {
        yflatendx = xloc; yflatendy = yloc; ydone = true; }
}

static void chkXFLAT() {
    Fixed abstmp;
    if (!xflat) {
        if (LsTan(x-xloc, y-yloc)) {
            xflat = true; xflatstartx = xloc; xflatstarty = yloc; }
        return; }
    if (xstate != xstart) return;
    if (GrTan(x-xloc, y-yloc)) {
        xflatendx = xloc; xflatendy = yloc; xdone = true; }
}

static void chkXDIR() {
    if (x > xloc) { /* going up */
        if (xstate == goingUP) return;
        if (xstate == STARTING) xstart = xstate = goingUP;
        else /*if (xstate == goingDOWN)*/ {
            if (xstart == goingUP) {
                xflatendx = xloc; xflatendy = yloc; }
            else if (!xflat) {
                xflatstartx = xloc; xflatstarty = yloc; xflat = true; }
            xstate = goingUP; }
    }
    else if (x < xloc) {
        if (xstate == goingDOWN) return;
        if (xstate == STARTING) xstart = xstate = goingDOWN;
        else /*if (xstate == goingUP)*/ {
            if (xstart == goingDOWN) {
                xflatendx = xloc; xflatendy = yloc; }
            else if (!xflat) {
                xflatstartx = xloc; xflatstarty = yloc; xflat = true; }
            xstate = goingDOWN; }
    }
}

static void chkDT(c) Cd c; {
    Fixed abstmp;
    Fixed loc;

    x = c.x, y = c.y;
    ynxt = y; xnxt = x;
    if (!ydone) {
        chkYDIR(); chkYFLAT();
        if (ydone && yflat &&
            abs(yflatstarty-cy0) > SDELTA &&
            abs(cy1-yflatendy) > SDELTA) {
            if ((ystart==goingUP && yflatstarty-yflatendy > SDELTA) ||
                (ystart==goingDOWN && yflatendy-yflatstarty > SDELTA)) {
                if (editChar && !forMultiMaster)
                    chkBad();
                return; }
            if (abs(yflatstartx-yflatendx) > SDELTA3)
            {
                DEBUG_ROUND(yflatstartx);
                DEBUG_ROUND(yflatendx);
                DEBUG_ROUND(yflatstarty);
                DEBUG_ROUND(yflatendy);
                
                loc = (yflatstarty+yflatendy)/2;
                DEBUG_ROUND(loc);
                
            if (!forMultiMaster)
                {
                    AddHSegment(yflatstartx,yflatendx,loc,
                                e,(PPathElt)NULL,sCURVE,13);
                }
                else
                {
                    inflPtFound = true;
                    fltnvalue = itfmy(loc);
                }
            }
        }
    }
    if (!xdone) {
        chkXDIR(); chkXFLAT();
        if (xdone && xflat &&
            abs(xflatstartx-x0) > SDELTA &&
            abs(x1-xflatendx) > SDELTA) {
            if ((xstart==goingUP && xflatstartx-xflatendx > SDELTA) ||
                (xstart==goingDOWN && xflatendx-xflatstartx > SDELTA)) {
                if (editChar && !forMultiMaster)
                    chkBad();
                return; }
            if (abs(xflatstarty-xflatendy) > SDELTA3)
            {
                DEBUG_ROUND(xflatstarty);
                DEBUG_ROUND(xflatendy);
                DEBUG_ROUND(xflatstartx);
                DEBUG_ROUND(xflatendx);
                
                loc = (xflatstartx+xflatendx)/2;
                DEBUG_ROUND(loc);

                if (!forMultiMaster)
                
                {
                    AddVSegment(xflatstarty,xflatendy,loc,
                               e,(PPathElt)NULL,sCURVE,13);
                }
                else
                {
                    inflPtFound = true;
                    fltnvalue = itfmx(loc);
                }
                
            }
        }
    }
    xloc = xnxt; yloc = ynxt;
}

#define FQ(x) ((int32_t)((x) >> 6))
static int32_t CPDirection(x1,cy1,x2,y2,x3,y3) Fixed x1,cy1,x2,y2,x3,y3; {
    int32_t q, q1, q2, q3;
    q1 = FQ(x2)*FQ(y3-cy1);
    q2 = FQ(x1)*FQ(y2-y3);
    q3 = FQ(x3)*FQ(cy1-y2);
    q = q1 + q2 + q3;
    if (q > 0) return 1;
    if (q < 0) return -1;
    return 0;
}

void RMovePoint(dx, dy, whichcp, e)
Fixed dx, dy; PPathElt e; int32_t whichcp; {
    if (whichcp == cpStart) { e = e->prev; whichcp = cpEnd; }
    if (whichcp == cpEnd) {
        if (e->type == CLOSEPATH) e = GetDest(e);
        if (e->type == CURVETO) { e->x3 += dx; e->y3 += dy; }
        else { e->x += dx; e->y += dy; }
        return;
    }
    if (whichcp == cpCurve1) { e->x1 += dx; e->y1 += dy; return; }
    if (whichcp == cpCurve2) { e->x2 += dx; e->y2 += dy; return; }
    {
        FlushLogFiles();
        sprintf(globmsg, "Malformed path list in %s.\n", fileName);
        LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
    }
    
}

void Delete(e) PPathElt e; {
    PPathElt nxt, prv;
    nxt = e->next; prv = e->prev;
    if (nxt != NULL) nxt->prev = prv;
    else pathEnd = prv;
    if (prv != NULL) prv->next = nxt;
    else pathStart = nxt;
}

/* This procedure is called from BuildFont when adding hints
 to base designs of a multi-master font. */
bool GetInflectionPoint(x, y, x1, cy1, x2, y2, x3, y3, inflPt)
Fixed x, y, x1, cy1, x2, y2, x3, y3;
Fixed *inflPt;
{
    FltnRec fltnrec;
    Cd c0, c1, c2, c3;
    
    fltnrec.report = chkDT;
    c0.x = tfmx(x);  c0.y = tfmy(y);
    c1.x = tfmx(x1); c1.y = tfmy(cy1);
    c2.x = tfmx(x2); c2.y = tfmy(y2);
    c3.x = tfmx(x3); c3.y = tfmy(y3);
    xstate = ystate = STARTING;
    xdone = ydone = xflat = yflat = inflPtFound = false;
    x0 = c0.x; cy0 = c0.y; x1 = c3.x; cy1 = c3.y;
    xloc = x0; yloc = cy0;
    forMultiMaster = true;
    FltnCurve(c0, c1, c2, c3, &fltnrec);
    if (inflPtFound)
        *inflPt = fltnvalue;
    return inflPtFound;
}

static void CheckSCurve(ee) PPathElt ee; {
    FltnRec fr;
    Cd c0, c1, c2, c3;
    if (ee->type != CURVETO)
    {
        FlushLogFiles();
        sprintf(globmsg, "Malformed path list in %s.\n", fileName);
        LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
    }
    
    GetEndPoint(ee->prev, &c0.x, &c0.y);
    fr.report = chkDT;
    c1.x = ee->x1; c1.y = ee->y1;
    c2.x = ee->x2; c2.y = ee->y2;
    c3.x = ee->x3; c3.y = ee->y3;
    xstate = ystate = STARTING;
    xdone = ydone = xflat = yflat = false;
    x0 = c0.x; cy0 = c0.y; x1 = c3.x; cy1 = c3.y;
    xloc = x0; yloc = cy0;
    e = ee;
    forMultiMaster = false;
    FltnCurve(c0, c1, c2, c3, &fr);
}

static void CheckZeroLength() {
    PPathElt e, NxtE;
    Fixed x0, cy0, x1, cy1, x2, y2, x3, y3;
    e = pathStart;
    while (e != NULL) { /* delete zero length elements */
        NxtE = e->next;
        GetEndPoints(e, &x0, &cy0, &x1, &cy1);
        if (e->type == LINETO && x0==x1 && cy0==cy1) {
            Delete(e); goto Nxt1; }
        if (e->type == CURVETO) {
            x2 = e->x1; y2 = e->y1; x3 = e->x2; y3 = e->y2;
            if (x0==x1 && cy0==cy1 && x2==x1 && x3==x1 && y2==cy1 && y3==cy1) {
                Delete(e); goto Nxt1; }
        }
        Nxt1: e = NxtE; }
}

void CheckSmooth() {
    PPathElt e, nxt, NxtE;
    bool recheck;
    Fixed x0, cy0, x1, cy1, x2, y2, x3, y3, smdiff, xx, yy;
    CheckZeroLength();
restart:
    reCheckSmooth = false; recheck = false;
    e = pathStart;
    while (e != NULL) {
        NxtE = e->next;
        if (e->type == MOVETO || IsTiny(e) || e->isFlex) goto Nxt;
        GetEndPoint(e, &x1, &cy1);
        if (e->type == CURVETO) {
            int32_t cpd0, cpd1;
            x2 = e->x1; y2 = e->y1; x3 = e->x2; y3 = e->y2;
            GetEndPoint(e->prev, &x0, &cy0);
            cpd0 = CPDirection(x0,cy0,x2,y2,x3,y3);
            cpd1 = CPDirection(x2,y2,x3,y3,x1,cy1);
            if (ProdLt0(cpd0, cpd1))
                CheckSCurve(e);
        }
        nxt = NxtForBend(e, &x2, &y2, &xx, &yy);
        if (nxt->isFlex) goto Nxt;
        PrvForBend(nxt, &x0, &cy0);
        if (!CheckSmoothness(x0, cy0, x1, cy1, x2, y2, &smdiff))
            ReportSmoothError(x1, cy1);
        if (smdiff > FixInt(140)) { /* trim off sharp angle */
            /* As of version 2.21 angle blunting will not occur. */
            /*
             if (editChar && ConsiderClipSharpPoint(x0, cy0, x1, cy1, x2, y2, e)) {
             ReportClipSharpAngle(x1, cy1); recheck = true; }
             else */
            if (smdiff > FixInt(160))
                ReportSharpAngle(x1, cy1);
        }
        Nxt: e = NxtE; }
    if (reCheckSmooth) goto restart;
    if (!recheck) return;
    CheckZeroLength();
    /* in certain cases clip sharp point can produce a zero length line */
}

#define BBdist (FixInt(20)) /* DEBUG 8 BIT. DOuble value from 10 to 20 for change in coordinate system. */

static void chkBBDT(c) Cd c; {
    Fixed x = c.x, y = c.y, abstmp;
    if (bbquit) return;
    if (vert) {
        lst = y;
        if (!started && abs(x-loc) <= BBdist) {
            started = true; frst = y; }
        else if (started && abs(x-loc) > BBdist) bbquit = true;
    }
    else {
        lst = x;
        if (!started && abs(y-loc) <= BBdist) {
            started = true; frst = x; }
        else if (started && abs(y-loc) > BBdist) bbquit = true;
    }
}

void CheckForMultiMoveTo() {
    PPathElt e = pathStart;
    bool moveto;
    moveto = false;
    while (e != NULL) {
        if (e->type != MOVETO) moveto = false;
        else if (!moveto) moveto = true;
        else Delete(e->prev); /* delete previous moveto */
        e = e->next;
    }
}

void CheckBBoxEdge(e, vrt, lc, pf, pl)
PPathElt e; bool vrt; Fixed lc, *pf, *pl; {
    FltnRec fr;
    Cd c0, c1, c2, c3;
    if (e->type != CURVETO)
    {
        FlushLogFiles();
        sprintf(globmsg, "Malformed path list in %s.\n", fileName);
        LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
    }
    
    GetEndPoint(e->prev, &c0.x, &c0.y);
    fr.report = chkBBDT;
    bbquit = false;
    c1.x = e->x1; c1.y = e->y1;
    c2.x = e->x2; c2.y = e->y2;
    c3.x = e->x3; c3.y = e->y3;
    loc = lc; vert = vrt; started = false;
    chkBBDT(c0);
    FltnCurve(c0, c1, c2, c3, &fr);
    *pf = frst; *pl = lst;
}

static void MakeColinear(tx, ty, x0, cy0, x1, cy1, xptr, yptr)
Fixed tx, ty, x0, cy0, x1, cy1, *xptr, *yptr; {
    Fixed dx, dy;
    float rdx, rdy, dxdy, dxsq, dysq, dsq, xi, yi, rx, ry, rx0, ry0;
    dx = x1-x0; dy = cy1-cy0;
    if (dx==0 && dy==0) { *xptr = tx; *yptr = ty; return; }
    if (dx==0) { *xptr = x0; *yptr = ty; return; }
    if (dy==0) { *xptr = tx; *yptr = cy0; return; }
    acfixtopflt(dx, &rdx); acfixtopflt(dy, &rdy);
    acfixtopflt(x0, &rx0); acfixtopflt(cy0, &ry0);
    acfixtopflt(tx, &rx); acfixtopflt(ty, &ry);
    dxdy = rdx*rdy; dxsq = rdx*rdx; dysq = rdy*rdy; dsq = dxsq+dysq;
    xi = (rx*dxsq+rx0*dysq+(ry-ry0)*dxdy)/dsq;
    yi = ry0+((xi-rx0)*rdy)/rdx;
    *xptr = acpflttofix(&xi);
    *yptr = acpflttofix(&yi);
}

#define DEG(x) ((x)*57.29577951308232088)
extern double atan2();
static Fixed ATan(a, b) Fixed a, b; {
    float aa, bb, cc;
    acfixtopflt(a, &aa); acfixtopflt(b, &bb);
    cc = (float)DEG(atan2((double)aa, (double)bb));
    while (cc < 0) cc += 360.0;
    return acpflttofix(&cc);
}

bool CheckSmoothness(x0, cy0, x1, cy1, x2, y2, pd)
Fixed x0, cy0, x1, cy1, x2, y2, *pd; {
    Fixed dx, dy, smdiff, smx, smy, at0, at1, abstmp;
    dx = x0 - x1; dy = cy0 - cy1;
    *pd = 0;
    if (dx == 0 && dy == 0) return true;
    at0 = ATan(dx, dy);
    dx = x1 - x2; dy = cy1 - y2;
    if (dx == 0 && dy == 0) return true;
    at1 = ATan(dx, dy);
    smdiff = at0 - at1; if (smdiff < 0) smdiff = -smdiff;
    if (smdiff >= FixInt(180)) smdiff = FixInt(360) - smdiff;
    *pd = smdiff;
    if (smdiff == 0 || smdiff > FixInt(30)) return true;
    MakeColinear(x1, cy1, x0, cy0, x2, y2, &smx, &smy);
    smx = FHalfRnd(smx);
    smy = FHalfRnd(smy);
    /* DEBUG 8 BIT. Double hard coded distance values, for change from 7 to 8 bits for fractions. */
    return abs(smx - x1) < FixInt(4) && abs(smy - cy1) < FixInt(4);
}

void CheckForDups() {
    register PPathElt ob, nxt;
    register Fixed x, y;
    ob = pathStart;
    while (ob != NULL) {
        nxt = ob->next;
        if (ob->type == MOVETO) {
            x = ob->x; y = ob->y; ob = nxt;
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
    x = itfmx(x); y = itfmy(y);
    ReportDuplicates(x, y);
}

void MoveSubpathToEnd(e) PPathElt e; {
    PPathElt subEnd, subStart, subNext, subPrev;
    subEnd = (e->type == CLOSEPATH)? e : GetClosedBy(e);
    subStart = GetDest(subEnd);
    if (subEnd == pathEnd) return; /* already at end */
    subNext = subEnd->next;
    if (subStart == pathStart) {
        pathStart = subNext; subNext->prev = NULL; }
    else {
        subPrev = subStart->prev;
        subPrev->next = subNext;
        subNext->prev = subPrev;
    }
    pathEnd->next = subStart;
    subStart->prev = pathEnd;
    subEnd->next = NULL;
    pathEnd = subEnd;
}
