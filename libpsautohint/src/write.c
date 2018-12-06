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

#define WRTABS_COMMENT (0)

static Fixed currentx, currenty;
static bool firstFlex, wrtHintInfo;
#define MAXBUFFLEN 127
static char S0[MAXBUFFLEN + 1];
static HintPoint* bst;
static char bch;
static Fixed bx, by;
static bool bstB;

int32_t
FRnd(int32_t x)
{
    /* This is meant to work on Fixed 24.8 values, not the elt path (x,y) which
     * are 25.7 */
    int32_t r;
    r = x;
    if (gRoundToInt) {
        r = r + (1 << 7);
        r = r & ~0xFF;
    }
    return r;
}

#define WriteString(...) ACBufferWriteF(gBezOutput, __VA_ARGS__)

/* Note: The 8 bit fixed fraction cannot support more than 2 decimal places. */
#define WRTNUM(i) WriteString("%d ", (int32_t)(i))
#define WRTRNUM(i) WriteString("%0.2f ", round((double)(i)*100) / 100)

static void
wrtxa(Fixed x)
{
    if ((gRoundToInt) || (FracPart(x) == 0)) {
        Fixed i = FRnd(x);
        WRTNUM(FTrunc(i));
        currentx = i;
    } else {
        float r;
        currentx = x;
        r = (float)FIXED2FLOAT(x);
        WRTRNUM(r);
    }
}

static void
wrtya(Fixed y)
{
    if ((gRoundToInt) || (FracPart(y) == 0)) {
        Fixed i = FRnd(y);
        WRTNUM(FTrunc(i));
        currenty = i;
    } else {
        float r;
        currenty = y;
        r = (float)FIXED2FLOAT(y);
        WRTRNUM(r);
    }
}

#define wrtcda(c)                                                              \
    wrtxa(c.x);                                                                \
    wrtya(c.y)

/*To avoid pointless hint subs*/
#define HINTMAXSTR 2048
static char hintmaskstr[HINTMAXSTR];
static char prevhintmaskstr[HINTMAXSTR];

static void
safestrcat(char* s1, char* s2)
{
    if (strlen(s1) + strlen(s2) + 1 > HINTMAXSTR) {
        LogMsg(LOGERROR, FATALERROR, "Hint information overflowing buffer.");
    } else {
        strcat(s1, s2);
    }
}

#define sws(str) safestrcat(hintmaskstr, (char*)str)

#define SWRTNUM(i)                                                             \
    {                                                                          \
        snprintf(S0, MAXBUFFLEN, "%d ", (int32_t)(i));                         \
        sws(S0);                                                               \
    }

#define SWRTNUMA(i)                                                            \
    {                                                                          \
        snprintf(S0, MAXBUFFLEN, "%0.2f ", round((double)(i)*100) / 100);      \
        sws(S0);                                                               \
    }

static void
NewBest(HintPoint* lst)
{
    bst = lst;
    bch = lst->c;
    if (bch == 'y' || bch == 'm') {
        Fixed x0, x1;
        bstB = true;
        x0 = lst->x0;
        x1 = lst->x1;
        bx = NUMMIN(x0, x1);
    } else {
        Fixed y0, y1;
        bstB = false;
        y0 = lst->y0;
        y1 = lst->y1;
        by = NUMMIN(y0, y1);
    }
}

static void
WriteOne(Fixed s)
{ /* write s to output file */
    if (FracPart(s) == 0) {
        SWRTNUM(FTrunc(s))
    } else {
        float d = (float)FIXED2FLOAT(s);
        SWRTNUMA(d);
    }
}

static void
WritePointItem(HintPoint* lst)
{
    switch (lst->c) {
        case 'b':
        case 'v':
            WriteOne(lst->y0);
            WriteOne(lst->y1 - lst->y0);
            sws(((lst->c == 'b') ? "rb" : "rv"));
            break;
        case 'y':
        case 'm':
            WriteOne(lst->x0);
            WriteOne(lst->x1 - lst->x0);
            sws(((lst->c == 'y') ? "ry" : "rm"));
            break;
        default: {
            LogMsg(LOGERROR, NONFATALERROR, "Illegal point list data.");
        }
    }
    sws(" % ");
    SWRTNUM(lst->p0 != NULL ? lst->p0->count : 0);
    SWRTNUM(lst->p1 != NULL ? lst->p1->count : 0);
    sws("\n");
}

static void
WrtPntLst(HintPoint* lst)
{
    HintPoint* ptLst;
    char ch;
    Fixed x0, x1, y0, y1;
    ptLst = lst;

    while (lst != NULL) { /* mark all as not yet done */
        lst->done = false;
        lst = lst->next;
    }
    while (true) { /* write in sort order */
        lst = ptLst;
        bst = NULL;
        while (lst != NULL) { /* find first not yet done as init best */
            if (!lst->done) {
                NewBest(lst);
                break;
            }
            lst = lst->next;
        }
        if (bst == NULL) {
            break; /* finished with entire list */
        }
        lst = bst->next;
        while (lst != NULL) { /* search for best */
            if (!lst->done) {
                ch = lst->c;
                if (ch > bch) {
                    NewBest(lst);
                } else if (ch == bch) {
                    if (bstB) {
                        x0 = lst->x0;
                        x1 = lst->x1;
                        if (NUMMIN(x0, x1) < bx) {
                            NewBest(lst);
                        }
                    } else {
                        y0 = lst->y0;
                        y1 = lst->y1;
                        if (NUMMIN(y0, y1) < by) {
                            NewBest(lst);
                        }
                    }
                }
            }
            lst = lst->next;
        }
        bst->done = true; /* mark as having been done */
        WritePointItem(bst);
    }
}

static void
wrtnewhints(PathElt* e)
{
    if (!wrtHintInfo) {
        return;
    }
    hintmaskstr[0] = '\0';
    WrtPntLst(gPtLstArray[e->newhints]);
    if (strcmp(prevhintmaskstr, hintmaskstr)) {
        WriteString("beginsubr snc\n%sendsubr enc\nnewcolors\n", hintmaskstr);
        strcpy(prevhintmaskstr, hintmaskstr);
    }
}

static bool
IsFlex(PathElt* e)
{
    PathElt *e0, *e1;
    if (firstFlex) {
        e0 = e;
        e1 = e->next;
    } else {
        e0 = e->prev;
        e1 = e;
    }
    return (e0 != NULL && e0->isFlex && e1 != NULL && e1->isFlex);
}

static void
mt(Cd c, PathElt* e)
{
    if (e->newhints != 0) {
        wrtnewhints(e);
    }
    wrtcda(c);
    WriteString("mt\n");
}

static void
dt(Cd c, PathElt* e)
{
    if (e->newhints != 0) {
        wrtnewhints(e);
    }
    wrtcda(c);
    WriteString("dt\n");
}

static Fixed flX, flY;
static Cd fc1, fc2, fc3;

#define wrtpreflx2a(c)                                                         \
    wrtcda(c);                                                                 \
    WriteString("rmt\npreflx2a\n")

static void
wrtflex(Cd c1, Cd c2, Cd c3, PathElt* e)
{
    int32_t dmin, delta;
    bool yflag;
    Cd c13;
    float shrink, r1, r2;
    if (firstFlex) {
        flX = currentx;
        flY = currenty;
        fc1 = c1;
        fc2 = c2;
        fc3 = c3;
        firstFlex = false;
        return;
    }
    yflag = e->yFlex;
    dmin = gDMin;
    delta = gDelta;
    WriteString("preflx1\n");
    if (yflag) {
        if (fc3.y == c3.y) {
            c13.y = c3.y;
        } else {
            acfixtopflt(fc3.y - c3.y, &shrink);
            shrink = (float)delta / shrink;
            if (shrink < 0.0f) {
                shrink = -shrink;
            }
            acfixtopflt(fc3.y - c3.y, &r1);
            r1 *= shrink;
            acfixtopflt(c3.y, &r2);
            r1 += r2;
            c13.y = acpflttofix(&r1);
        }
        c13.x = fc3.x;
    } else {
        if (fc3.x == c3.x) {
            c13.x = c3.x;
        } else {
            acfixtopflt(fc3.x - c3.x, &shrink);
            shrink = (float)delta / shrink;
            if (shrink < 0.0f) {
                shrink = -shrink;
            }
            acfixtopflt(fc3.x - c3.x, &r1);
            r1 *= shrink;
            acfixtopflt(c3.x, &r2);
            r1 += r2;
            c13.x = acpflttofix(&r1);
        }
        c13.y = fc3.y;
    }

    wrtpreflx2a(c13);
    wrtpreflx2a(fc1);
    wrtpreflx2a(fc2);
    wrtpreflx2a(fc3);
    wrtpreflx2a(c1);
    wrtpreflx2a(c2);
    wrtpreflx2a(c3);
    currentx = flX;
    currenty = flY;
    wrtcda(fc1);
    wrtcda(fc2);
    wrtcda(fc3);
    wrtcda(c1);
    wrtcda(c2);
    wrtcda(c3);
    WRTNUM(dmin);
    WRTNUM(delta);
    WRTNUM(yflag);
    WRTNUM(FTrunc(FRnd(currentx)));
    WRTNUM(FTrunc(FRnd(currenty)));
    WriteString("flxa\n");
    firstFlex = true;
}

static void
ct(Cd c1, Cd c2, Cd c3, PathElt* e)
{
    if (e->newhints != 0) {
        wrtnewhints(e);
    }
    if (e->isFlex && IsFlex(e)) {
        wrtflex(c1, c2, c3, e);
    } else {
        wrtcda(c1);
        wrtcda(c2);
        wrtcda(c3);
        WriteString("ct\n");
    }
}

static void
cp(PathElt* e)
{
    if (e->newhints != 0) {
        wrtnewhints(e);
    }
    WriteString("cp\n");
}

static void
NumberPath(void)
{
    int16_t cnt;
    PathElt* e;
    e = gPathStart;
    cnt = 1;
    while (e != NULL) {
        e->count = cnt++;
        e = e->next;
    }
}

void
SaveFile(void)
{
    PathElt* e = gPathStart;
    Cd c1, c2, c3;

    WriteString("%% %s\n", gGlyphName);
    wrtHintInfo = (gPathStart != NULL && gPathStart != gPathEnd);
    NumberPath();
    prevhintmaskstr[0] = '\0';
    if (wrtHintInfo && (!e->newhints)) {
        hintmaskstr[0] = '\0';
        WrtPntLst(gPtLstArray[0]);
        WriteString("%s", hintmaskstr);
        strcpy(prevhintmaskstr, hintmaskstr);
    }

    WriteString("sc\n");
    firstFlex = true;
    currentx = currenty = 0;
    while (e != NULL) {
        switch (e->type) {
            case CURVETO:
                c1.x = e->x1;
                c1.y = -e->y1;
                c2.x = e->x2;
                c2.y = -e->y2;
                c3.x = e->x3;
                c3.y = -e->y3;
                ct(c1, c2, c3, e);
                break;
            case LINETO:
                c1.x = e->x;
                c1.y = -e->y;
                dt(c1, e);
                break;
            case MOVETO:
                c1.x = e->x;
                c1.y = -e->y;
                mt(c1, e);
                break;
            case CLOSEPATH:
                cp(e);
                break;
            default: {
                LogMsg(LOGERROR, NONFATALERROR, "Illegal path list.");
            }
        }
#if WRTABS_COMMENT
        WriteString(" %% ");
        WRTNUM(e->count)
        switch (e->type) {
            case CURVETO:
                wrtfx(c1.x);
                wrtfx(c1.y);
                wrtfx(c2.x);
                wrtfx(c2.y);
                wrtfx(c3.x);
                wrtfx(c3.y);
                WriteString("ct");
                break;
            case LINETO:
                wrtfx(c1.x);
                wrtfx(c1.y);
                WriteString("dt");
                break;
            case MOVETO:
                wrtfx(c1.x);
                wrtfx(c1.y);
                WriteString("mt");
                break;
            case CLOSEPATH:
                WriteString("cp");
                break;
        }
        WriteString("\n");
#endif
        e = e->next;
    }
    WriteString("ed\n");
}
