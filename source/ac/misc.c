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

int32_t
CountSubPaths(void)
{
    register PPathElt e = pathStart;
    int32_t cnt = 0;
    while (e != NULL) {
        if (e->type == MOVETO)
            cnt++;
        e = e->next;
    }
    return cnt;
}
void
RoundPathCoords(void)
{
    register PPathElt e;
    e = pathStart;
    while (e != NULL) {
        if (e->type == CURVETO) {
            e->x1 = FHalfRnd(e->x1);
            e->y1 = FHalfRnd(e->y1);
            e->x2 = FHalfRnd(e->x2);
            e->y2 = FHalfRnd(e->y2);
            e->x3 = FHalfRnd(e->x3);
            e->y3 = FHalfRnd(e->y3);
        } else if (e->type == LINETO || e->type == MOVETO) {
            e->x = FHalfRnd(e->x);
            e->y = FHalfRnd(e->y);
        }
        e = e->next;
    }
}

static int32_t
CheckForClr(void)
{
    PPathElt mt, cp;
    mt = pathStart;
    while (mt != NULL) {
        if (mt->type != MOVETO) {
            ExpectedMoveTo(mt);
            return -1;
        }
        cp = GetClosedBy(mt);
        if (cp == NULL) {
            ReportMissingClosePath();
            return -1;
        }
        mt = cp->next;
    }
    return 0;
}

bool
PreCheckForColoring(void)
{
    PPathElt e, nxt;
    int32_t cnt = 0;
    int32_t chk;
    while (pathEnd != NULL) {
        if (pathEnd->type == MOVETO)
            Delete(pathEnd);
        else if (pathEnd->type != CLOSEPATH) {
            ReportMissingClosePath();
            return false;
        } else
            break;
    }
    e = pathStart;
    while (e != NULL) {
        if (e->type == CLOSEPATH) {
            if (e == pathEnd)
                break;
            nxt = e->next;
            if (nxt->type == MOVETO) {
                e = nxt;
                continue;
            }
            if (nxt->type == CLOSEPATH) { /* remove double closepath */
                Delete(nxt);
                continue;
            }
        }
        e = e->next;
    }
    while (true) {
        chk = CheckForClr();
        if (chk == -1)
            return false;
        if (chk == 0)
            break;
        if (++cnt > 10) {
            LogMsg("Looping in PreCheckForHints!\n", WARNING, OK, true);
            break;
        }
    }
    return true;
}

static PPathElt
GetSubpathNext(register PPathElt e)
{
    while (true) {
        e = e->next;
        if (e == NULL)
            break;
        if (e->type == CLOSEPATH)
            break;
        if (!IsTiny(e))
            break;
    }
    return e;
}

static PPathElt
GetSubpathPrev(register PPathElt e)
{
    while (true) {
        e = e->prev;
        if (e == NULL)
            break;
        if (e->type == MOVETO)
            e = GetClosedBy(e);
        if (!IsTiny(e))
            break;
    }
    return e;
}

static bool
AddAutoFlexProp(PPathElt e, bool yflag)
{
    PPathElt e0 = e, e1 = e->next;
    if (e0->type != CURVETO || e1->type != CURVETO) {
        sprintf(globmsg, "Illegal input in character file: %s.\n", fileName);
        LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
    }
    /* Don't add flex to linear curves. */
    if (yflag && e0->y3 == e1->y1 && e1->y1 == e1->y2 && e1->y2 == e1->y3)
        return false;
    else if (e0->x3 == e1->x1 && e1->x1 == e1->x2 && e1->x2 == e1->x3)
        return false;
    e0->yFlex = yflag;
    e1->yFlex = yflag;
    e0->isFlex = true;
    e1->isFlex = true;
    return true;
}

#define LENGTHRATIOCUTOFF                                                      \
    0.11 /* 0.33^2 : two curves must be in approximate length ratio of 1:3 or  \
            better */

static void
TryYFlex(PPathElt e, PPathElt n, Fixed x0, Fixed y0, Fixed x1, Fixed y1)
{
    Fixed x2, y2, x3, y3, x4, y4;
    PPathElt p, q;
    bool top, dwn;
    double d0sq, d1sq, quot, dx, dy;

    GetEndPoint(n, &x2, &y2);
    dy = abs(y0 - y2);
    if (dy > flexCand)
        return; /* too big diff in bases. If dy is within flexCand, flex will
                   fail , but we will report it as a candidate. */
    dx = abs(x0 - x2);
    if (dx < MAXFLEX)
        return; /* Let's not add flex to features less than MAXFLEX wide. */
    if (dx < (3 * abs(y0 - y2)))
        return; /* We want the width to be at least three times the height. */
    if (ProdLt0(y1 - y0, y1 - y2))
        return; /* y0 and y2 not on same side of y1 */

    /* check the ratios of the "lengths" of 'e' and 'n'  */
    dx = (x1 - x0);
    dy = (y1 - y0);
    d0sq = dx * dx + dy * dy;
    dx = (x2 - x1);
    dy = (y2 - y1);
    d1sq = dx * dx + dy * dy;
    quot = (d0sq > d1sq) ? (d1sq / d0sq) : (d0sq / d1sq);
    if (quot < LENGTHRATIOCUTOFF)
        return;

    if (flexStrict) {
        q = GetSubpathNext(n);
        GetEndPoint(q, &x3, &y3);
        if (ProdLt0(y3 - y2, y1 - y2))
            return; /* y1 and y3 not on same side of y2 */
        p = GetSubpathPrev(e);
        GetEndPoint(p->prev, &x4, &y4);
        if (ProdLt0(y4 - y0, y1 - y0))
            return; /* y1 and y4 not on same side of y0 */
        top = (x0 > x1) ? true : false;
        if (YgoesUp)
            dwn = (y1 < y0) ? true : false;
        else
            dwn = (y1 > y0) ? true : false;
        if ((top && !dwn) || (!top && dwn))
            return; /* concave */
    }
    if (n != e->next) { /* something in the way */
        n = e->next;
        ReportTryFlexError(n->type == CLOSEPATH, x1, y1);
        return;
    }
    if (y0 != y2) {
        ReportTryFlexNearMiss(x0, y0, x2, y2);
        return;
    }
    if (AddAutoFlexProp(e, true))
        ReportAddFlex();
}

static void
TryXFlex(PPathElt e, PPathElt n, Fixed x0, Fixed y0, Fixed x1, Fixed y1)
{
    Fixed x2, y2, x3, y3, x4, y4;
    PPathElt p, q;
    bool lft;
    double d0sq, d1sq, quot, dx, dy;

    GetEndPoint(n, &x2, &y2);
    dx = abs(y0 - y2);
    if (dx > flexCand)
        return; /* too big diff in bases */

    dy = abs(x0 - x2);
    if (dy < MAXFLEX)
        return; /* Let's not add flex to features less than MAXFLEX wide. */
    if (dy < (3 * abs(x0 - x2)))
        return; /* We want the width to be at least three times the height. */

    if (ProdLt0(x1 - x0, x1 - x2))
        return; /* x0 and x2 not on same side of x1 */

    /* check the ratios of the "lengths" of 'e' and 'n'  */
    dx = (x1 - x0);
    dy = (y1 - y0);
    d0sq = dx * dx + dy * dy;
    dx = (x2 - x1);
    dy = (y2 - y1);
    d1sq = dx * dx + dy * dy;
    quot = (d0sq > d1sq) ? (d1sq / d0sq) : (d0sq / d1sq);
    if (quot < LENGTHRATIOCUTOFF)
        return;

    if (flexStrict) {
        q = GetSubpathNext(n);
        GetEndPoint(q, &x3, &y3);
        if (ProdLt0(x3 - x2, x1 - x2))
            return; /* x1 and x3 not on same side of x2 */
        p = GetSubpathPrev(e);
        GetEndPoint(p->prev, &x4, &y4);
        if (ProdLt0(x4 - x0, x1 - x0))
            return; /* x1 and x4 not on same side of x0 */
        if (YgoesUp)
            lft = (y0 > y2) ? true : false;
        else
            lft = (y0 < y2) ? true : false;
        if ((lft && x0 > x1) || (!lft && x0 < x1))
            return; /* concave */
    }
    if (n != e->next) { /* something in the way */
        n = e->next;
        ReportTryFlexError(n->type == CLOSEPATH, x1, y1);
        return;
    }
    if (x0 != x2) {
        ReportTryFlexNearMiss(x0, y0, x2, y2);
        return;
    }
    if (AddAutoFlexProp(e, false))
        ReportAddFlex();
}

void
AutoAddFlex(void)
{
    PPathElt e, n;
    Fixed x0, y0, x1, y1;
    e = pathStart;
    while (e != NULL) {
        if (e->type != CURVETO || e->isFlex)
            goto Nxt;
        n = GetSubpathNext(e);
        if (n->type != CURVETO)
            goto Nxt;
        GetEndPoints(e, &x0, &y0, &x1, &y1);
        if (abs(y0 - y1) <= MAXFLEX)
            TryYFlex(e, n, x0, y0, x1, y1);
        if (abs(x0 - x1) <= MAXFLEX)
            TryXFlex(e, n, x0, y0, x1, y1);
    Nxt:
        e = e->next;
    }
}
