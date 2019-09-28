/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"

PathElt*
GetDest(PathElt* cldest)
{
    if (cldest == NULL)
        return NULL;
    while (true) {
        cldest = cldest->prev;
        if (cldest == NULL)
            return gPathStart;
        if (cldest->type == MOVETO)
            return cldest;
    }
}

PathElt*
GetClosedBy(PathElt* clsdby)
{
    if (clsdby == NULL)
        return NULL;
    if (clsdby->type == CLOSEPATH)
        return clsdby;
    while (true) {
        clsdby = clsdby->next;
        if (clsdby == NULL)
            return NULL;
        if (clsdby->type == MOVETO)
            return NULL;
        if (clsdby->type == CLOSEPATH)
            return clsdby;
    }
}

void
GetEndPoint(PathElt* e, Fixed* x1p, Fixed* y1p)
{
retry:
    if (e == NULL) {
        *x1p = 0;
        *y1p = 0;
        return;
    }
    switch (e->type) {
        case MOVETO:
        case LINETO:
            *x1p = e->x;
            *y1p = e->y;
            break;
        case CURVETO:
            *x1p = e->x3;
            *y1p = e->y3;
            break;
        case CLOSEPATH:
            e = GetDest(e);
            if (e == NULL || e->type == CLOSEPATH) {
                LogMsg(LOGERROR, NONFATALERROR, "Bad description.");
            }
            goto retry;
        default: {
            LogMsg(LOGERROR, NONFATALERROR, "Illegal operator.");
        }
    }
}

void
GetEndPoints(PathElt* p, Fixed* px0, Fixed* py0, Fixed* px1, Fixed* py1)
{
    GetEndPoint(p, px1, py1);
    GetEndPoint(p->prev, px0, py0);
}

#define Interpolate(q, v0, q0, v1, q1) (v0 + (q - q0) * ((v1 - v0) / (q1 - q0)))
static Fixed
HVness(float* pq)
{
    float q;
    float result;
    /* approximately == 2 q neg exp */
    /* as q -> 0, result goes to 1.0 */
    /* as q -> inf, result goes to 0.0 */
    q = *pq;
    if (q < .25f)
        result = (float)Interpolate(q, 1.0f, 0.0f, .841f, .25f);
    else if (q < .5f)
        result = (float)Interpolate(q, .841f, .25f, .707f, .5f);
    else if (q < 1)
        result = (float)Interpolate(q, .707f, .5f, .5f, 1.0f);
    else if (q < 2)
        result = (float)Interpolate(q, .5f, 1.0f, .25f, 2.0f);
    else if (q < 4)
        result = (float)Interpolate(q, .25f, 2.0f, 0.0f, 4.0f);
    else
        result = 0.0;
    return acpflttofix(&result);
}

Fixed
VertQuo(Fixed xk, Fixed yk, Fixed xl, Fixed yl)
{
    /* FixOne means exactly vertical. 0 means not vertical */
    /* intermediate values mean almost vertical */
    Fixed xabs, yabs;
    float rx, ry, q;
    xabs = xk - xl;
    if (xabs < 0)
        xabs = -xabs;
    if (xabs == 0)
        return FixOne;
    yabs = yk - yl;
    if (yabs < 0)
        yabs = -yabs;
    if (yabs == 0)
        return 0;
    acfixtopflt(xabs, &rx);
    acfixtopflt(yabs, &ry);
    q = (float)(rx * rx) / (gTheta * ry); /* DEBUG 8 BIT. Used to by
                                            2*(rx*rx)/(theta*ry). Don't need
                                            thsi with the 8 bits of Fixed
                                            fraction. */
    return HVness(&q);
}

Fixed
HorzQuo(Fixed xk, Fixed yk, Fixed xl, Fixed yl)
{
    Fixed xabs, yabs;
    float rx, ry, q;
    yabs = yk - yl;
    if (yabs < 0)
        yabs = -yabs;
    if (yabs == 0)
        return FixOne;
    xabs = xk - xl;
    if (xabs < 0)
        xabs = -xabs;
    if (xabs == 0)
        return 0;
    acfixtopflt(xabs, &rx);
    acfixtopflt(yabs, &ry);
    q = (float)(ry * ry) / (gTheta * rx); /* DEBUG 8 BIT. Used to by
                                            2*(ry*ry)/(theta*ry). Don't need
                                            thsi with the 8 bits of Fixed
                                            fraction. */
    return HVness(&q);
}

bool
IsTiny(PathElt* e)
{
    Fixed x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    GetEndPoints(e, &x0, &y0, &x1, &y1);
    return ((abs(x0 - x1) < FixTwo) && (abs(y0 - y1) < FixTwo)) ? true : false;
}

bool
IsShort(PathElt* e)
{
    Fixed x0 = 0, y0 = 0, x1 = 0, y1 = 0, dx = 0, dy = 0, mn = 0, mx = 0;
    GetEndPoints(e, &x0, &y0, &x1, &y1);
    dx = abs(x0 - x1);
    dy = abs(y0 - y1);
    if (dx > dy) {
        mn = dy;
        mx = dx;
    } else {
        mn = dx;
        mx = dy;
    }
    return ((mx + (mn * 42) / 125) < FixInt(6))
             ? true
             : false; /* DEBUG 8 BIT. Increased threshold from 3 to 6, for
                         change in coordinare system. */
}

PathElt*
NxtForBend(PathElt* p, Fixed* px2, Fixed* py2, Fixed* px3, Fixed* py3)
{
    PathElt *nxt, *nxtMT = NULL;
    Fixed x = 0, y = 0;
    nxt = p;
    GetEndPoint(p, &x, &y);
    while (true) {
        if (nxt->type == CLOSEPATH) {
            nxt = GetDest(nxt);
            /* The following test was added to prevent an infinite loop. */
            if (nxtMT != NULL && nxtMT == nxt) {
                ReportPossibleLoop(p);
                nxt = NULL;
            } else {
                nxtMT = nxt;
                nxt = nxt->next;
            }
        } else
            nxt = nxt->next;
        if (nxt == NULL) { /* forget it */
            *px2 = *py2 = *px3 = *py3 = -FixInt(9999);
            return nxt;
        }
        if (!IsTiny(nxt))
            break;
    }
    if (nxt->type == CURVETO) {
        Fixed x2 = nxt->x1;
        Fixed y2 = nxt->y1;
        if (x2 == x && y2 == y) {
            x2 = nxt->x2;
            y2 = nxt->y2;
        }
        *px2 = x2;
        *py2 = y2;
    } else
        GetEndPoint(nxt, px2, py2);
    GetEndPoint(nxt, px3, py3);
    return nxt;
}

PathElt*
PrvForBend(PathElt* p, Fixed* px2, Fixed* py2)
{
    PathElt *prv, *prvCP = NULL;
    Fixed x2, y2;
    prv = p;
    while (true) {
        prv = prv->prev;
        if (prv == NULL)
            goto Bogus;
        if (prv->type == MOVETO) {
            prv = GetClosedBy(prv);
            /* The following test was added to prevent an infinite loop. */
            if (prv == NULL || (prvCP != NULL && prvCP == prv))
                goto Bogus;
            prvCP = prv;
        }
        if (!IsTiny(prv))
            break;
    }
    if (prv->type == CURVETO) {
        x2 = prv->x2;
        y2 = prv->y2;
        if (x2 == prv->x3 && y2 == prv->y3) {
            x2 = prv->x1;
            y2 = prv->y1;
        }
        *px2 = x2;
        *py2 = y2;
    } else {
        p = prv->prev;
        if (p == NULL)
            goto Bogus;
        GetEndPoint(p, px2, py2);
    }
    return prv;
Bogus:
    *px2 = *py2 = -FixInt(9999);
    return prv;
}

static bool
CheckHeight(bool upperFlag, PathElt* p)
{
    PathElt* ee;
    Fixed y, yy;
    ee = gPathStart;
    y = -p->y;
    while (ee != NULL) {
        if (ee->type == MOVETO && ee != p) {
            yy = -ee->y;
            if ((upperFlag && yy > y) || (!upperFlag && yy < y))
                return false;
        }
        ee = ee->next;
    }
    return true;
}

bool
IsLower(PathElt* p)
{
    return CheckHeight(false, p);
}

bool
IsUpper(PathElt* p)
{
    return CheckHeight(true, p);
}
