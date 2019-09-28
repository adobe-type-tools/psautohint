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
#include "charpath.h"
#include "opcodes.h"

char gGlyphName[MAX_GLYPHNAME_LEN];

static Fixed currentx, currenty; /* used to calculate absolute coordinates */
static Fixed tempx, tempy;       /* used to calculate relative coordinates */
#define STKMAX (20)
static Fixed stk[STKMAX];
static int32_t stkindex;
static bool flex, startchar;
static bool forMultiMaster, includeHints;
/* Reading file for comparison of multiple master data and hint information.
   Reads into GlyphPathElt structure instead of PathElt. */

static Fixed
Pop(void)
{
    if (stkindex <= 0) {
        LogMsg(LOGERROR, NONFATALERROR, "Stack underflow while reading glyph.");
    }
    stkindex--;
    return stk[stkindex];
}

static void
Push(Fixed r)
{
    if (stkindex >= STKMAX) {
        LogMsg(LOGERROR, NONFATALERROR, "Stack overflow while reading glyph.");
        return;
    }
    stk[stkindex] = r;
    stkindex++;
}

static void
Pop2(void)
{
    Pop();
    Pop();
}

static void
PopPCd(Cd* pcd)
{
    pcd->y = Pop();
    pcd->x = Pop();
}

#define DoDelta(dx, dy)                                                        \
    currentx += (dx);                                                          \
    currenty += (dy)

static PathElt*
AppendElement(int32_t etype)
{
    PathElt* e;
    e = (PathElt*)Alloc(sizeof(PathElt));
    e->type = (int16_t)etype;
    if (gPathEnd != NULL) {
        gPathEnd->next = e;
        e->prev = gPathEnd;
    } else
        gPathStart = e;
    gPathEnd = e;
    return e;
}

static void
RDcurveto(Cd c1, Cd c2, Cd c3)
{
    if (!forMultiMaster) {
        PathElt* new;
        new = AppendElement(CURVETO);
        new->x1 = c1.x;
        new->y1 = -c1.y;
        new->x2 = c2.x;
        new->y2 = -c2.y;
        new->x3 = c3.x;
        new->y3 = -c3.y;
    } else {
        GlyphPathElt* new;
        new = AppendGlyphPathElement(RCT);
        new->x = tempx;
        new->y = tempy;
        new->x1 = c1.x;
        new->y1 = c1.y;
        new->x2 = c2.x;
        new->y2 = c2.y;
        new->x3 = c3.x;
        new->y3 = c3.y;
        new->rx1 = c1.x - tempx;
        new->ry1 = c1.y - tempy;
        new->rx2 = c2.x - c1.x;
        new->ry2 = c2.y - c1.y;
        new->rx3 = c3.x - c2.x;
        new->ry3 = c3.y - c2.y;
        if (flex)
            new->isFlex = true;
    }
}

static void
RDmtlt(int32_t etype)
{
    if (!forMultiMaster) {
        PathElt* new;
        new = AppendElement(etype);
        new->x = currentx;
        new->y = -currenty;
        return;
    } else {
        GlyphPathElt* new;
        new = AppendGlyphPathElement(etype == LINETO ? RDT : RMT);
        new->x = currentx;
        new->y = currenty;
        new->rx = tempx;
        new->ry = tempy;
    }
}

#define RDlineto() RDmtlt(LINETO)
#define RDmoveto() RDmtlt(MOVETO)

static void
psRMT(void)
{
    Cd c;
    PopPCd(&c);
    if (flex)
        return;
    tempx = c.x;
    tempy = c.y;
    DoDelta(c.x, c.y);
    RDmoveto();
}

static void
Rct(Cd c1, Cd c2, Cd c3)
{
    tempx = currentx;
    tempy = currenty;
    DoDelta(c1.x, c1.y);
    c1.x = currentx;
    c1.y = currenty;
    DoDelta(c2.x, c2.y);
    c2.x = currentx;
    c2.y = currenty;
    DoDelta(c3.x, c3.y);
    c3.x = currentx;
    c3.y = currenty;
    RDcurveto(c1, c2, c3);
}

static void
psCP(void)
{
    if (!forMultiMaster)
        AppendElement(CLOSEPATH);
    else
        AppendGlyphPathElement(CP);
}

static void
psMT(void)
{
    Cd c;
    c.y = Pop();
    c.x = Pop();
    tempx = c.x - currentx;
    tempy = c.y - currenty;
    currenty = c.y;
    currentx = c.x;
    RDmoveto();
}

static void
psDT(void)
{
    Cd c;
    c.y = Pop();
    c.x = Pop();
    tempx = c.x - currentx;
    tempy = c.y - currenty;
    currenty = c.y;
    currentx = c.x;
    RDlineto();
}

static void
psCT(void)
{
    Cd c1, c2, c3;
    tempx = currentx;
    tempy = currenty;
    PopPCd(&c3);
    PopPCd(&c2);
    PopPCd(&c1);
    RDcurveto(c1, c2, c3);
}

static void
psFLX(void)
{
    Cd c0, c1, c2, c3, c4, c5;
    int32_t i;
    for (i = 0; i < 5; i++)
        Pop();
    PopPCd(&c5);
    PopPCd(&c4);
    PopPCd(&c3);
    PopPCd(&c2);
    PopPCd(&c1);
    PopPCd(&c0);
    Rct(c0, c1, c2);
    Rct(c3, c4, c5);
    flex = false;
}

static void
ReadHintInfo(char nm, const char* str)
{
    Cd c0;
    int16_t hinttype =
      nm == 'y' ? RY : nm == 'b' ? RB : nm == 'm' ? RM + ESCVAL : RV + ESCVAL;
    int32_t elt1, elt2;
    PopPCd(&c0);
    c0.y += c0.x; /* make absolute */
    /* Look for comment of path elements used to determine this band. */
    if (sscanf(str, " %% %d %d", &elt1, &elt2) != 2) {
        LogMsg(WARNING, NONFATALERROR,
               "Extra hint information required for blended fonts is "
               "not in glyph. Please re-hint using the latest software. "
               "Hints will not be included in this glyph.");
        gAddHints = false;
        includeHints = false;
    } else
        SetHintsElt(hinttype, &c0, elt1, elt2, (bool)!startchar);
}

/*Used instead of StringEqual to keep ac from cloberring source string*/

static int
isPrefix(const char* s, const char* pref)
{
    while (*pref) {
        if (*pref != *s)
            return 0;
        pref++;
        s++;
    }
    return 1;
}

static void
DoName(const char* nm, const char* buff, int len)
{
    switch (len) {
        case 2:
            switch (nm[0]) {
                case 'c': /* ct, cp */
                    switch (nm[1]) {
                        case 't':
                            psCT();
                            break;
                        case 'p':
                            psCP();
                            break;
                        default:
                            goto badFile;
                    }
                    break;
                case 'm': /* mt */
                    if (nm[1] != 't')
                        goto badFile;
                    psMT();
                    break;
                case 'd': /* dt */
                    if (nm[1] != 't')
                        goto badFile;
                    psDT();
                    break;
                case 's': /* sc */
                    if (nm[1] != 'c')
                        goto badFile;
                    startchar = true;
                    break;
                case 'e': /* ed */
                    if (nm[1] != 'd')
                        goto badFile;
                    break;
                case 'r': /* rm, rv, ry, rb */
                    if (includeHints)
                        ReadHintInfo(nm[1], buff);
                    else
                        Pop2();
                    break;
                default:
                    goto badFile;
            }
            break;
        case 3:
            switch (nm[0]) {
                case 'r': /* rmt */
                    if (nm[1] != 'm' || nm[2] != 't')
                        goto badFile;
                    psRMT();
                    break;
                case 's': /* snc */
                case 'e': /* enc */
                    switch (nm[1]) {
                        case 'n':
                            if (nm[2] != 'c')
                                goto badFile;
                            break;
                        default:
                            goto badFile;
                    }
                    break;
                case 'f': /* flx */
                    if (nm[1] == 'l' && nm[2] == 'x')
                        psFLX();
                    else
                        goto badFile;
                    break;
                default:
                    goto badFile;
            }
            break;
        case 7:
            switch (nm[6]) {
                case '1': /* preflx1 */
                case '2': /* preflx2 */
                    if (nm[0] != 'p' || nm[1] != 'r' || nm[2] != 'e' ||
                        nm[3] != 'f' || nm[4] != 'l' || nm[5] != 'x')
                        goto badFile;
                    flex = true;
                    break;
                case 'r': /* endsubr */
                    if (!isPrefix(nm, "endsubr"))
                        goto badFile;
                    break;
                default:
                    goto badFile;
            }
            break;
        case 9:
            switch (nm[0]) {
                case 'b': /* beginsubr */
                    if (!isPrefix(nm, "beginsubr"))
                        goto badFile;
                    break;
                case 'n': /* newhints */
                    if (!isPrefix(nm, "newcolors"))
                        goto badFile;
                    break;
                default:
                    goto badFile;
            }
            break;
        default:
            goto badFile;
    }
    return;
badFile : {
    char op[80];
    if (len > 79)
        len = 79;
    strncpy(op, nm, len);
    op[len] = 0;

    LogMsg(LOGERROR, NONFATALERROR, "Bad file format. Unknown operator: %s.",
           op);
}
}

static void
ParseString(const char* s)
{
    const char* s0;
    char c;
    const char* c0;
    bool neg = false;
    bool isReal;
    float rval;
    int32_t val = 0;
    Fixed r;
    gPathStart = gPathEnd = NULL;
    gGlyphName[0] = '\0';

    while (true) {
        c = *s++;
    nxtChar:
        switch (c) {
            case '-': /* negative number */
                neg = true;
                val = 0;
                goto rdnum;
            case '%': /* comment */
                if (gGlyphName[0] == '\0') {
                    unsigned int end = 0;
                    while (*s == ' ')
                        s++;

                    while (s[end] && (s[end] != ' ') && (s[end] != '\r') &&
                           (s[end] != '\n'))
                        end++;

                    if (end >= MAX_GLYPHNAME_LEN) {
                        LogMsg(LOGERROR, NONFATALERROR,
                               "Bad input data. Glyph name is greater than "
                               "%d chars.",
                               MAX_GLYPHNAME_LEN);
                        end = MAX_GLYPHNAME_LEN - 1;
                    }

                    strncpy(gGlyphName, s, end);
                    gGlyphName[end] = '\0';
                }
                while (*s && (*s != '\n') && (*s != '\r')) {
                    s++;
                }
                continue;
            case ' ':
                continue;
            case '\t':
                continue;
            case '\n':
                continue;
            case '\r':
                continue;
            case 0: /* end of file */
                if (stkindex != 0) {
                    LogMsg(LOGERROR, NONFATALERROR,
                           "Bad input data. Numbers left on stack "
                           "at end of glyph.");
                }
                return;
            default:
                if (c >= '0' && c <= '9') {
                    neg = false;
                    val = c - '0';
                    goto rdnum;
                }
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                    s0 = s - 1;
                    while (true) {
                        c = *s++;
                        if ((c == ' ') || (c == '\t') || (c == '\n') ||
                            (c == '\r') || (c == '\0'))
                            break;
                        if (c == 0)
                            break;
                    }
                    DoName(s0, s, (int)(s - s0 - 1));
                    if (c == '\0')
                        s--;
                    continue;
                }
                LogMsg(LOGERROR, NONFATALERROR, "Unexpected character: %c", c);
        }
    rdnum:
        isReal = false;
        c0 = s - 1;
        while (true) {
            c = *s++;
            if (c == '.')
                isReal = true;
            else if (c >= '0' && c <= '9')
                val = val * 10 + (c - '0');
            else if ((c == ' ') || (c == '\t')) {
                if (isReal) {
                    rval = strtod(c0, NULL);
                    /* Autohint only supports 2 digits of decimal precision. */
                    rval = roundf(rval * 100) / 100;
                    /* do not need to use 'neg' to negate the value, as c0
                     * string includes the minus sign.*/
                    r = FixReal(rval); /* convert to Fixed */
                } else {
                    if (neg)
                        val = -val;
                    r = FixInt(val); /* convert to Fixed */
                }
                Push(r);
                goto nxtChar;
            } else {
                LogMsg(LOGERROR, NONFATALERROR,
                       "Illegal number terminator while reading glyph.");
                return;
            }
        } /*end while true */
    }
}

bool
ReadGlyph(const char* srcglyph, bool forBlendData, bool readHints)
{
    if (!srcglyph)
        return false;

    currentx = currenty = tempx = tempy = stkindex = 0;
    flex = startchar = false;
    forMultiMaster = forBlendData;
    includeHints = readHints;

    ParseString(srcglyph);

    return true;
}
