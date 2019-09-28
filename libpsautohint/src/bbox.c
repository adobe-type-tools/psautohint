/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "bbox.h"
#include "ac.h"

static Fixed xmin, ymin, xmax, ymax, vMn, vMx, hMn, hMx;
static PathElt *pxmn, *pxmx, *pymn, *pymx, *pe, *pvMn, *pvMx, *phMn, *phMx;

static void
FPBBoxPt(Cd c)
{
    if (c.x < xmin) {
        xmin = c.x;
        pxmn = pe;
    }
    if (c.x > xmax) {
        xmax = c.x;
        pxmx = pe;
    }
    if (c.y < ymin) {
        ymin = c.y;
        pymn = pe;
    }
    if (c.y > ymax) {
        ymax = c.y;
        pymx = pe;
    }
}

static void
FindPathBBox(void)
{
    FltnRec fr;
    PathElt* e;
    Cd c0, c1, c2, c3;
    memset(&c0, 0, sizeof(Cd));
    if (gPathStart == NULL) {
        xmin = ymin = xmax = ymax = 0;
        pxmn = pxmx = pymn = pymx = NULL;
        return;
    }
    fr.report = FPBBoxPt;
    xmin = ymin = FixInt(10000);
    xmax = ymax = -xmin;
    e = gPathStart;
    while (e != NULL) {
        switch (e->type) {
            case MOVETO:
            case LINETO:
                c0.x = e->x;
                c0.y = e->y;
                pe = e;
                FPBBoxPt(c0);
                break;
            case CURVETO:
                c1.x = e->x1;
                c1.y = e->y1;
                c2.x = e->x2;
                c2.y = e->y2;
                c3.x = e->x3;
                c3.y = e->y3;
                pe = e;
                FltnCurve(c0, c1, c2, c3, &fr);
                c0 = c3;
                break;
            case CLOSEPATH:
                break;
            default: {
                LogMsg(LOGERROR, NONFATALERROR, "Undefined operator.");
            }
        }
        e = e->next;
    }
    xmin = FHalfRnd(xmin);
    ymin = FHalfRnd(ymin);
    xmax = FHalfRnd(xmax);
    ymax = FHalfRnd(ymax);
}

PathElt*
FindSubpathBBox(PathElt* e)
{
    FltnRec fr;
    Cd c0, c1, c2, c3;
    memset(&c0, 0, sizeof(Cd));
    if (e == NULL) {
        xmin = ymin = xmax = ymax = 0;
        pxmn = pxmx = pymn = pymx = NULL;
        return NULL;
    }
    fr.report = FPBBoxPt;
    xmin = ymin = FixInt(10000);
    xmax = ymax = -xmin;
#if 0
  e = GetDest(e); /* back up to moveto */
#else
    /* This and the following change (in the next else clause) were made
       to fix the hinting in glyphs in the SolEol lists.  These are
       supposed to have subpath bbox hinted, but were getting path bbox
       hinted instead. */
    if (e->type != MOVETO)
        e = GetDest(e); /* back up to moveto */
#endif
    while (e != NULL) {
        switch (e->type) {
            case MOVETO:
            case LINETO:
                c0.x = e->x;
                c0.y = e->y;
                pe = e;
                FPBBoxPt(c0);
                break;
            case CURVETO:
                c1.x = e->x1;
                c1.y = e->y1;
                c2.x = e->x2;
                c2.y = e->y2;
                c3.x = e->x3;
                c3.y = e->y3;
                pe = e;
                FltnCurve(c0, c1, c2, c3, &fr);
                c0 = c3;
                break;
            case CLOSEPATH:
#if 0
        break;
#else
                e = e->next;
                goto done;
#endif
            default: {
                LogMsg(LOGERROR, NONFATALERROR, "Undefined operator.");
            }
        }
        e = e->next;
    }
#if 1
done:
#endif
    xmin = FHalfRnd(xmin);
    ymin = FHalfRnd(ymin);
    xmax = FHalfRnd(xmax);
    ymax = FHalfRnd(ymax);
    return e;
}

void
FindCurveBBox(Fixed x0, Fixed y0, Fixed px1, Fixed py1, Fixed px2, Fixed py2,
              Fixed x1, Fixed y1, Fixed* pllx, Fixed* plly, Fixed* purx,
              Fixed* pury)
{
    FltnRec fr;
    Cd c0, c1, c2, c3;
    fr.report = FPBBoxPt;
    xmin = ymin = FixInt(10000);
    xmax = ymax = -xmin;
    c0.x = x0;
    c0.y = y0;
    c1.x = px1;
    c1.y = py1;
    c2.x = px2;
    c2.y = py2;
    c3.x = x1;
    c3.y = y1;
    FPBBoxPt(c0);
    FltnCurve(c0, c1, c2, c3, &fr);
    *pllx = FHalfRnd(xmin);
    *plly = FHalfRnd(ymin);
    *purx = FHalfRnd(xmax);
    *pury = FHalfRnd(ymax);
}

void
HintVBnds(void)
{
    PathElt* p;
    if (gPathStart == NULL || VHintGlyph())
        return;
    FindPathBBox();
    vMn = xmin;
    vMx = xmax;
    pvMn = pxmn;
    pvMx = pxmx;
    if (vMn > vMx) {
        Fixed tmp = vMn;
        vMn = vMx;
        vMx = tmp;
        p = pvMn;
        pvMn = pvMx;
        pvMx = p;
    }
    AddHintPoint(vMn, 0, vMx, 0, 'y', pvMn, pvMx);
}

void
HintHBnds(void)
{
    if (gPathStart == NULL || HHintGlyph())
        return;
    FindPathBBox();
    hMn = -ymin;
    hMx = -ymax;
    phMn = pymn;
    phMx = pymx;
    if (hMn > hMx) {
        PathElt* p;
        Fixed tmp = hMn;
        hMn = hMx;
        hMx = tmp;
        p = phMn;
        phMn = phMx;
        phMx = p;
    }
    AddHintPoint(0, hMn, 0, hMx, 'b', phMn, phMx);
}

static bool
CheckValOverlaps(Fixed lft, Fixed rht, HintVal* lst, bool xflg)
{
    Fixed tmp;
    if (!xflg) {
        lft = -lft;
        rht = -rht;
    }
    if (lft > rht) {
        tmp = lft;
        lft = rht;
        rht = tmp;
    }
    while (lst != NULL) {
        Fixed lft2 = lst->vLoc1;
        Fixed rht2 = lst->vLoc2;
        if (!xflg) {
            lft2 = -lft2;
            rht2 = -rht2;
        }
        if (lft2 > rht2) {
            tmp = lft2;
            lft2 = rht2;
            rht2 = tmp;
        }
        if (lft2 <= rht && lft <= rht2)
            return true;
        lst = lst->vNxt;
    }
    return false;
}

void
AddBBoxHV(bool Hflg, bool subs)
{
    PathElt* e;
    HintVal* val;
    HintSeg *seg1, *seg2;
    e = gPathStart;
    while (e != NULL) {
        if (subs)
            e = FindSubpathBBox(e);
        else {
            FindPathBBox();
            e = NULL;
        }
        if (!Hflg) {
            if (!CheckValOverlaps(xmin, xmax, gVHinting, true)) {
                val = (HintVal*)Alloc(sizeof(HintVal));
                seg1 = (HintSeg*)Alloc(sizeof(HintSeg));
                seg1->sLoc = xmin;
                seg1->sElt = pxmn;
                seg1->sBonus = 0;
                seg1->sType = sLINE;
                seg1->sMin = ymin;
                seg1->sMax = ymax;
                seg1->sNxt = NULL;
                seg1->sLnk = NULL;
                seg2 = (HintSeg*)Alloc(sizeof(HintSeg));
                seg2->sLoc = xmax;
                seg2->sElt = pxmx;
                seg2->sBonus = 0;
                seg2->sType = sLINE;
                seg2->sMin = ymin;
                seg2->sMax = ymax;
                seg2->sNxt = NULL;
                seg2->sLnk = NULL;
                val->vVal = 100;
                val->vSpc = 0;
                val->vLoc1 = xmin;
                val->vLoc2 = xmax;
                val->vSeg1 = seg1;
                val->vSeg2 = seg2;
                val->vGhst = false;
                val->vNxt = gVHinting;
                val->vBst = val;
                gVHinting = val;
            }
        } else {
            if (!CheckValOverlaps(ymin, ymax, gHHinting, false)) {
                val = (HintVal*)Alloc(sizeof(HintVal));
                seg1 = (HintSeg*)Alloc(sizeof(HintSeg));
                seg1->sLoc = ymax;
                seg1->sElt = pymx;
                seg1->sBonus = 0;
                seg1->sType = sLINE;
                seg1->sMin = xmin;
                seg1->sMax = xmax;
                seg1->sNxt = NULL;
                seg1->sLnk = NULL;
                seg2 = (HintSeg*)Alloc(sizeof(HintSeg));
                seg2->sLoc = ymin;
                seg2->sElt = pymn;
                seg2->sBonus = 0;
                seg2->sType = sLINE;
                seg2->sMin = xmin;
                seg2->sMax = xmax;
                seg2->sNxt = NULL;
                seg2->sLnk = NULL;
                val->vVal = 100;
                val->vSpc = 0;
                val->vLoc1 = ymax; /* bot is > top because y axis is reversed */
                val->vLoc2 = ymin;
                val->vSeg1 = seg1;
                val->vSeg2 = seg2;
                val->vGhst = false;
                val->vNxt = gHHinting;
                val->vBst = val;
                gHHinting = val;
            }
        }
    }
}

void
CheckPathBBox(void)
{
    Fixed llx, lly, urx, ury, tmp;
    FindPathBBox();
    llx = xmin;
    urx = xmax;
    if (llx > urx) {
        tmp = llx;
        llx = urx;
        urx = tmp;
    }
    lly = -ymax;
    ury = -ymin;
    if (lly > ury) {
        tmp = lly;
        lly = ury;
        ury = tmp;
    }
    if (llx < -FixInt(600) || lly < -FixInt(600) || urx > FixInt(1600) ||
        ury > FixInt(1600))
        ReportBBoxBogus(llx, lly, urx, ury);
}

bool
CheckBBoxes(PathElt* e1, PathElt* e2)
{
    /* return true if e1 and e2 in same subpath or i
       the bbox for one is inside the bbox of the other */
    Fixed xmn, xmx, ymn, ymx;
    e1 = GetDest(e1);
    e2 = GetDest(e2);
    if (e1 == e2)
        return true; /* same subpath */
    FindSubpathBBox(e1);
    xmn = xmin;
    xmx = xmax;
    ymn = ymin;
    ymx = ymax;
    FindSubpathBBox(e2);
    return ((xmn <= xmin && xmax <= xmx && ymn <= ymin && ymax <= ymx) ||
            (xmn >= xmin && xmax >= xmx && ymn >= ymin && ymax >= ymx));
}
