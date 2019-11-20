/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"

static void
FMiniFltn(Cd f0, Cd f1, Cd f2, Cd f3, FltnRec* pfr)
{
/* Like FFltnCurve, but assumes abs(deltas) <= 127 pixels */
/* 8 bits of fraction gives enough precision for splitting curves */
#define MiniFltnMaxDepth (6)
#define inrect (p[-10])
#define inbbox (p[-9])
#define c0x (p[-8])
#define c0y (p[-7])
#define c1x (p[-6])
#define c1y (p[-5])
#define c2x (p[-4])
#define c2y (p[-3])
#define c3x (p[-2])
#define c3y (p[-1])
#define inrect2 (p[0])
#define bbox2 (p[1])
#define d0x (p[2])
#define d0y (p[3])
#define d1x (p[4])
#define d1y (p[5])
#define d2x (p[6])
#define d2y (p[7])
#define d3x (p[8])
#define d3y (p[9])
#define MiniBlkSz (10)
#define mdpt(a, b) (((a) + (b)) >> 1)
    Fixed cds[MiniBlkSz * MiniFltnMaxDepth], dpth, eps;
    Fixed bbLLX = 0, bbLLY = 0, bbURX = 0, bbURY = 0;
    Fixed* p;
    p = cds;
    dpth = 1;
    *(p++) = true;  /* inrect2 starts out true */
    *(p++) = false; /* inbbox2 starts out false */
    /* shift coordinates so that lower left of BBox is at (0,0)*/
    /* This  fills the first  MiniBlkSz series of ints with the start point,
    control point, end end point
     (x,y) values for the curve, minus the lower left (x,y) for the curve.
     One each pass, it splits the curve in two, replacing the current MiniBlkSz
    series of ints with the first
     of the two split curves, and the next MiniBlkSz series of ints with the
    second of the curves.
     It then sets the pointer p so that the second MiniBlkSz series of ints
    becomes the current set,
     and iterates, thereby splitting the second curve in two parts. This
    continues until the control points
     get very close to the start point, or we reach the limit of
    MiniFltnMaxDepth iterations. At that time,
     the PathBBox update function is called with the end point of the first of
    the most recently split curves.

    Once the the current set of points meets the test that one of the control
    points is very close to the start point,
     then the algoithm iteratively steps back to the previous set. If thsi does
    not meet the test, the algorith
     iterates forward again
     */
    /*DEBUG 8 BIT. AFter chaning the fractinoal part to 8 bits form 7 bits, had
     * to change all the int16_t values to int32_t in order to not overflow math
     * operations. The only reason these were all shorts was speed and memory
     * issues in 1986. */
    {
        Fixed llx, lly;
        llx = pfr->llx;
        lly = pfr->lly;
        *(p++) = f0.x - llx;
        *(p++) = f0.y - lly;
        *(p++) = f1.x - llx;
        *(p++) = f1.y - lly;
        *(p++) = f2.x - llx;
        *(p++) = f2.y - lly;
        *(p++) = f3.x - llx;
        *(p++) = f3.y - lly;
    }
    if (!inrect) {
        Fixed c, f128;
        c = pfr->ll.x;
        bbLLX = (c <= 0) ? 0 : c;
        c = pfr->ll.y;
        bbLLY = (c <= 0) ? 0 : c;
        f128 = FixInt(128);
        c = pfr->ur.x;
        bbURX = (c >= f128) ? 0x7fff : c;
        c = pfr->ur.y;
        bbURY = (c >= f128) ? 0x7fff : c;
    }
    eps = pfr->feps;
    if (eps < 16) /* DEBUG 8 BIT FIX */
        eps = 16; /* Brotz patch */
    while (true) {
        /* Iterate until curve has been flattened into MiniFltnMaxDepth segments
         */
        if (dpth == MiniFltnMaxDepth)
            goto ReportC3;
        if (!inrect) {
            Fixed llx, lly, urx, ury, c;
            llx = urx = c0x;
            if ((c = c1x) < llx)
                llx = c;
            else if (c > urx)
                urx = c;
            if ((c = c2x) < llx)
                llx = c;
            else if (c > urx)
                urx = c;
            if ((c = c3x) < llx)
                llx = c;
            else if (c > urx)
                urx = c;
            if (urx < bbLLX || llx > bbURX)
                goto ReportC3;
            lly = ury = c0y;
            if ((c = c1y) < lly)
                lly = c;
            else if (c > ury)
                ury = c;
            if ((c = c2y) < lly)
                lly = c;
            else if (c > ury)
                ury = c;
            if ((c = c3y) < lly)
                lly = c;
            else if (c > ury)
                ury = c;
            if (ury < bbLLY || lly > bbURY)
                goto ReportC3;
            if (urx <= bbURX && ury <= bbURY && llx >= bbLLX && lly >= bbLLY)
                inrect = true;
        }
        if (!inbbox) {
            int32_t mrgn = eps, r0, r3, ll, ur, c;
            r0 = c0x;
            r3 = c3x;
            if (r0 < r3) {
                ll = r0 - mrgn;
                ur = r3 + mrgn;
            } else {
                ll = r3 - mrgn;
                ur = r0 + mrgn;
            }
            if (ur < 0)
                ur = FixInt(128) - 1;
            c = c1x;
            if (c > ll && c < ur) {
                c = c2x;
                if (c > ll && c < ur) {
                    r0 = c0y;
                    r3 = c3y;
                    if (r0 < r3) {
                        ll = r0 - mrgn;
                        ur = r3 + mrgn;
                    } else {
                        ll = r3 - mrgn;
                        ur = r0 + mrgn;
                    }
                    if (ur < 0)
                        ur = FixInt(128) - 1;
                    c = c1y;
                    if (c > ll && c < ur) {
                        c = c2y;
                        if (c > ll && c < ur)
                            inbbox = true;
                    }
                }
            }
        }
        if (inbbox) {
            Fixed eqa, eqb, x, y;
            Fixed EPS;
            /* int64_t instead of Fixed to avoid integer overflow below */
            int64_t d;
            x = c0x;
            y = c0y;
            eqa = c3y - y;
            eqb = x - c3x;
            if (eqa == 0 && eqb == 0)
                goto ReportC3;
            EPS = ((abs(eqa) > abs(eqb)) ? eqa : eqb) * eps;
            if (EPS < 0)
                EPS = -EPS;
            /* The casts are needed to prevent integer overflow */
            d = (int64_t)eqa * (c1x - x);
            d += (int64_t)eqb * (c1y - y);
            if (labs(d) < EPS) {
                d = (int64_t)eqa * (c2x - x);
                d += (int64_t)eqb * (c2y - y);
                if (labs(d) < EPS)
                    goto ReportC3;
            }
        }
        { /* Bezier divide */
            Fixed c0, c1, c2, d1, d2, d3;
            d0x = c0 = c0x;
            c1 = c1x;
            c2 = c2x;
            d1x = d1 = mdpt(c0, c1);
            d3 = mdpt(c1, c2);
            d2x = d2 = mdpt(d1, d3);
            c2x = c2 = mdpt(c2, c3x);
            c1x = c1 = mdpt(d3, c2);
            c0x = d3x = mdpt(d2, c1);
            d0y = c0 = c0y;
            c1 = c1y;
            c2 = c2y;
            d1y = d1 = mdpt(c0, c1);
            d3 = mdpt(c1, c2);
            d2y = d2 = mdpt(d1, d3);
            c2y = c2 = mdpt(c2, c3y);
            c1y = c1 = mdpt(d3, c2);
            c0y = d3y = mdpt(d2, c1);
            bbox2 = inbbox;
            inrect2 = inrect;
            p += MiniBlkSz;
            dpth++;
            continue;
        }
    ReportC3 : {
        Cd c;
        if (--dpth == 0)
            c = f3;
        else {
            c.x = c3x + pfr->llx;
            c.y = c3y + pfr->lly;
        }
        (*pfr->report)(c); /* call FPBBoxPt() to reset bbox. */
        if (dpth == 0)
            return;
        p -= MiniBlkSz;
    }
    }
} /* end of FMiniFltn */
#undef MiniFltnMaxDepth
#undef inrect
#undef inbbox
#undef c0x
#undef c0y
#undef c1x
#undef c1y
#undef c2x
#undef c2y
#undef c3x
#undef c3y
#undef inrect2
#undef bbox2
#undef d0x
#undef d0y
#undef d1x
#undef d1y
#undef d2x
#undef d2y
#undef d3x
#undef d3y
#undef MiniBlkSz
#undef mdpt

#define FixedMidPoint(m, a, b)                                                 \
    (m).x = ((a).x + (b).x) >> 1;                                              \
    (m).y = ((a).y + (b).y) >> 1

#define FixedBezDiv(a0, a1, a2, a3, b0, b1, b2, b3)                            \
    b3 = a3;                                                                   \
    FixedMidPoint(b2, a2, a3);                                                 \
    FixedMidPoint(a3, a1, a2);                                                 \
    FixedMidPoint(a1, a0, a1);                                                 \
    FixedMidPoint(a2, a1, a3);                                                 \
    FixedMidPoint(b1, a3, b2);                                                 \
    FixedMidPoint(b0, a2, b1);                                                 \
    a3 = b0

/* inrect = !testRect */
/* Like FltnCurve, but works in the Fixed domain. */
/* abs values of coords must be < 2^14 so will not overflow when
   find midpoint by add and shift */
static void
FFltnCurve(Cd c0, Cd c1, Cd c2, Cd c3, FltnRec* pfr)
{
    Cd d0, d1, d2, d3;
    Fixed llx, lly, urx, ury;
    if (c0.x == c1.x && c0.y == c1.y && c2.x == c3.x && c2.y == c3.y)
        goto ReportC3; /* it is a flat curve - do not need to flatten. */
    if (pfr->limit <= 0)
        goto ReportC3;
    { /* set initial bbox of llx,lly, urx, ury from bez control and end points
       */
        Fixed c;
        llx = urx = c0.x;
        if ((c = c1.x) < llx)
            llx = c;
        else if (c > urx)
            urx = c;
        if ((c = c2.x) < llx)
            llx = c;
        else if (c > urx)
            urx = c;
        if ((c = c3.x) < llx)
            llx = c;
        else if (c > urx)
            urx = c;
        lly = ury = c0.y;
        if ((c = c1.y) < lly)
            lly = c;
        else if (c > ury)
            ury = c;
        if ((c = c2.y) < lly)
            lly = c;
        else if (c > ury)
            ury = c;
        if ((c = c3.y) < lly)
            lly = c;
        else if (c > ury)
            ury = c;
    }

    { /* if the height or width of the initial bbox is > 256, split it, and this
         function on the two parts. */
        Fixed th;
        th = FixInt(256); /* DEBUG 8 Bit */ /* delta threshhold of 127 pixels */
        /* The reason we split this is that the FMiniFltn function uses and 8.8
         Fixed to hold coordindate data - so the max
         intger part must be no more than 127, to allow for signed values. This
         made sense when an int was 16 bits, but is no longer an optimization.
         I still subdivide, so that FMiniFltn, which subdivides into a maximum
         of 6 curve segments, will be owkring with short segments. */
        if (urx - llx >= th || ury - lly >= th) {
            goto Split;
        }
    }
    pfr->llx = llx;
    pfr->lly = lly;
    FMiniFltn(c0, c1, c2, c3, pfr);
    return;

Split:
    /* Split the bez curve in half */
    FixedBezDiv(c0, c1, c2, c3, d0, d1, d2, d3);
    pfr->limit--;
    FFltnCurve(c0, c1, c2, c3, pfr);
    FFltnCurve(d0, d1, d2, d3, pfr);
    pfr->limit++;
    return;

ReportC3:
    (*pfr->report)(c3);
}

void
FltnCurve(Cd c0, Cd c1, Cd c2, Cd c3, FltnRec* pfr)
{
    pfr->limit = 6; /* limit on how many times a bez curve can be split in half
                       by recursive calls to FFltnCurve() */
    pfr->feps = FixOne; /* DEBUG 8 BIT FIX */
    FFltnCurve(c0, c1, c2, c3, pfr);
}
