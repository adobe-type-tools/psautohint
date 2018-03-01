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
#include "fontinfo.h"
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
   Reads into PCharPathElt structure instead of PPathElt. */

static float origEmSquare = 0.0;

Fixed
ScaleAbs(const ACFontInfo* fontinfo, Fixed unscaled)
{
    Fixed temp1;
    if (!gScalingHints)
        return unscaled;
    if (origEmSquare == 0.0) {
        char* fistr = GetFontInfo(fontinfo, "OrigEmSqUnits", ACOPTIONAL);
        if (fistr)
            origEmSquare = strtod(fistr, NULL);
        else
            origEmSquare = 1000.0;
    }
    temp1 = (Fixed)(1000.0 / origEmSquare * ((float)unscaled));
    return temp1;
}

Fixed
UnScaleAbs(const ACFontInfo* fontinfo, Fixed scaled)
{
    Fixed temp1;
    if (!gScalingHints)
        return scaled;
    if (origEmSquare == 0.0) {
        char* fistr = GetFontInfo(fontinfo, "OrigEmSqUnits", ACOPTIONAL);
        if (fistr)
            origEmSquare = strtod(fistr, NULL);
        else
            origEmSquare = 1000.0;
    }
    temp1 = (Fixed)(origEmSquare / 1000.0 * ((float)scaled));
    temp1 = FRnd(temp1);
    return (temp1);
}

static Fixed
Pop(void)
{
    if (stkindex <= 0) {
        LogMsg(LOGERROR, NONFATALERROR,
               "Stack underflow while reading %s glyph.\n", gGlyphName);
    }
    stkindex--;
    return stk[stkindex];
}

static void
Push(Fixed r)
{
    if (stkindex >= STKMAX) {
        LogMsg(LOGERROR, NONFATALERROR,
               "Stack underflow while reading %s glyph.\n", gGlyphName);
    }
    stk[stkindex] = r;
    stkindex++;
}

static void
Pop2(void)
{
    (void)Pop();
    (void)Pop();
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

static PPathElt
AppendElement(int32_t etype)
{
    PPathElt e;
    e = (PPathElt)Alloc(sizeof(PathElt));
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
psDIV(void)
{
    Fixed x, y;
    y = Pop();
    x = Pop();
    if (y == FixInt(100))
        x /= 100; /* this will usually be the case */
    else
        x = (x * FixOne) / y;
    Push(x);
}

static void
RDcurveto(const ACFontInfo* fontinfo, Cd c1, Cd c2, Cd c3)
{
    if (!forMultiMaster) {
        PPathElt new;
        new = AppendElement(CURVETO);
        new->x1 = tfmx(ScaleAbs(fontinfo, c1.x));
        new->y1 = tfmy(ScaleAbs(fontinfo, c1.y));
        new->x2 = tfmx(ScaleAbs(fontinfo, c2.x));
        new->y2 = tfmy(ScaleAbs(fontinfo, c2.y));
        new->x3 = tfmx(ScaleAbs(fontinfo, c3.x));
        new->y3 = tfmy(ScaleAbs(fontinfo, c3.y));
    } else {
        PCharPathElt new;
        new = AppendCharPathElement(RCT);
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
RDmtlt(const ACFontInfo* fontinfo, int32_t etype)
{
    if (!forMultiMaster) {
        PPathElt new;
        new = AppendElement(etype);
        new->x = tfmx(ScaleAbs(fontinfo, currentx));
        new->y = tfmy(ScaleAbs(fontinfo, currenty));
        return;
    } else {
        PCharPathElt new;
        new = AppendCharPathElement(etype == LINETO ? RDT : RMT);
        new->x = currentx;
        new->y = currenty;
        new->rx = tempx;
        new->ry = tempy;
    }
}

#define RDlineto() RDmtlt(fontinfo, LINETO)
#define RDmoveto() RDmtlt(fontinfo, MOVETO)

static void
psRDT(const ACFontInfo* fontinfo)
{
    Cd c;
    PopPCd(&c);
    tempx = c.x;
    tempy = c.y;
    DoDelta(c.x, c.y);
    RDlineto();
}

static void
psHDT(const ACFontInfo* fontinfo)
{
    Fixed dx;
    tempy = 0;
    dx = tempx = Pop();
    currentx += dx;
    RDlineto();
}

static void
psVDT(const ACFontInfo* fontinfo)
{
    Fixed dy;
    tempx = 0;
    dy = tempy = Pop();
    currenty += dy;
    RDlineto();
}

static void
psRMT(const ACFontInfo* fontinfo)
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
psHMT(const ACFontInfo* fontinfo)
{
    Fixed dx;
    tempy = 0;
    dx = tempx = Pop();
    currentx += dx;
    RDmoveto();
}

static void
psVMT(const ACFontInfo* fontinfo)
{
    Fixed dy;
    tempx = 0;
    dy = tempy = Pop();
    currenty += dy;
    RDmoveto();
}

static void
Rct(const ACFontInfo* fontinfo, Cd c1, Cd c2, Cd c3)
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
    RDcurveto(fontinfo, c1, c2, c3);
}

static void
psRCT(const ACFontInfo* fontinfo)
{
    Cd c1, c2, c3;
    PopPCd(&c3);
    PopPCd(&c2);
    PopPCd(&c1);
    Rct(fontinfo, c1, c2, c3);
}

static void
psVHCT(const ACFontInfo* fontinfo)
{
    Cd c1, c2, c3;
    c3.y = 0;
    c3.x = Pop();
    PopPCd(&c2);
    c1.y = Pop();
    c1.x = 0;
    Rct(fontinfo, c1, c2, c3);
}

static void
psHVCT(const ACFontInfo* fontinfo)
{
    Cd c1, c2, c3;
    c3.y = Pop();
    c3.x = 0;
    PopPCd(&c2);
    c1.y = 0;
    c1.x = Pop();
    Rct(fontinfo, c1, c2, c3);
}

static void
psCP(void)
{
    if (!forMultiMaster)
        AppendElement(CLOSEPATH);
    else
        AppendCharPathElement(CP);
}

static void
psMT(const ACFontInfo* fontinfo)
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
psDT(const ACFontInfo* fontinfo)
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
psCT(const ACFontInfo* fontinfo)
{
    Cd c1, c2, c3;
    tempx = currentx;
    tempy = currenty;
    PopPCd(&c3);
    PopPCd(&c2);
    PopPCd(&c1);
    RDcurveto(fontinfo, c1, c2, c3);
}

static void
psFLX(const ACFontInfo* fontinfo)
{
    Cd c0, c1, c2, c3, c4, c5;
    int32_t i;
    for (i = 0; i < 5; i++)
        (void)Pop();
    PopPCd(&c5);
    PopPCd(&c4);
    PopPCd(&c3);
    PopPCd(&c2);
    PopPCd(&c1);
    PopPCd(&c0);
    Rct(fontinfo, c0, c1, c2);
    Rct(fontinfo, c3, c4, c5);
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
               "not in\n  glyph: %s.  Please re-hint using "
               "the latest software.\n  Hints will not be included "
               "in this font.\n",
               gGlyphName);
        SetNoHints();
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
DoName(const ACFontInfo* fontinfo, const char* nm, const char* buff, int len)
{
    switch (len) {
        case 2:
            switch (nm[0]) {
                case 'c': /* ct, cp */
                    switch (nm[1]) {
                        case 't':
                            psCT(fontinfo);
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
                    psMT(fontinfo);
                    break;
                case 'd': /* dt */
                    if (nm[1] != 't')
                        goto badFile;
                    psDT(fontinfo);
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
                case 'i': /* id */
                    if (nm[1] != 'd')
                        goto badFile;
                    Pop();
                    gIdInFile = true;
                    break;
                default:
                    goto badFile;
            }
            break;
        case 3:
            switch (nm[0]) {
                case 'r': /* rdt, rmt, rct */
                    if (nm[2] != 't')
                        goto badFile;
                    switch (nm[1]) {
                        case 'd':
                            psRDT(fontinfo);
                            break;
                        case 'm':
                            psRMT(fontinfo);
                            break;
                        case 'c':
                            psRCT(fontinfo);
                            break;
                        default:
                            goto badFile;
                    }
                    break;
                case 'h': /* hdt, hmt */
                    if (nm[2] != 't')
                        goto badFile;
                    switch (nm[1]) {
                        case 'd':
                            psHDT(fontinfo);
                            break;
                        case 'm':
                            psHMT(fontinfo);
                            break;
                        default:
                            goto badFile;
                    }
                    break;
                case 'v': /* vdt, vmt */
                    if (nm[2] != 't')
                        goto badFile;
                    switch (nm[1]) {
                        case 'd':
                            psVDT(fontinfo);
                            break;
                        case 'm':
                            psVMT(fontinfo);
                            break;
                        default:
                            goto badFile;
                    }
                    break;
                case 's': /* sol, snc */
                case 'e': /* eol, enc */
                    switch (nm[1]) {
                        case 'o':
                            if (nm[2] != 'l')
                                goto badFile;
                            break;
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
                        psFLX(fontinfo);
                    else
                        goto badFile;
                    break;
                case 'd': /* div */
                    if (nm[1] == 'i' && nm[2] == 'v')
                        psDIV();
                    else
                        goto badFile;
                    break;
                default:
                    goto badFile;
            }
            break;
        case 4:
            if (nm[2] != 'c' || nm[3] != 't')
                goto badFile;
            switch (nm[0]) {
                case 'v': /* vhct */
                    if (nm[1] != 'h')
                        goto badFile;
                    psVHCT(fontinfo);
                    break;
                case 'h': /* hvct */
                    if (nm[1] != 'v')
                        goto badFile;
                    psHVCT(fontinfo);
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
                case 'n': /* newcolors */
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

    LogMsg(LOGERROR, NONFATALERROR,
           "Bad file format. Unknown operator: %s in %s character.\n", op,
           gGlyphName);
}
}

static void
ParseString(const ACFontInfo* fontinfo, const char* s)
{
    const char* s0;
    char c;
    char* c0;
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

                    while ((s[end] != ' ') && (s[end] != '\r') &&
                           (s[end] != '\n'))
                        end++;

                    strncpy(gGlyphName, s, end);
                    if (end < MAX_GLYPHNAME_LEN)
                        gGlyphName[end] = '\0';
                    else {
                        gGlyphName[MAX_GLYPHNAME_LEN - 1] = '\0';
                        LogMsg(LOGERROR, NONFATALERROR,
                               "Bad input data. Glyph name %s is "
                               "greater than %d chars.\n",
                               gGlyphName, MAX_GLYPHNAME_LEN);
                    }
                }
                while ((*s != '\n') && (*s != '\r')) {
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
                           "Bad input data.  Numbers left on stack "
                           "at end of %s file.\n",
                           gGlyphName);
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
                    DoName(fontinfo, s0, s, (int)(s - s0 - 1));
                    if (c == '\0')
                        s--;
                    continue;
                }
                LogMsg(LOGERROR, NONFATALERROR,
                       "Unexpected character in %s glyph.\n", gGlyphName);
        }
    rdnum:
        isReal = false;
        c0 = (char*)(s - 1);
        while (true) {
            c = *s++;
            if (c == '.')
                isReal = true;
            else if (c >= '0' && c <= '9')
                val = val * 10 + (c - '0');
            else if ((c == ' ') || (c == '\t')) {
                if (isReal) {
                    rval = strtod(c0, NULL);
                    rval = roundf(rval * 100) / 100; // Autohint can only
                                                     // support 2 digits of
                                                     // decimal precision.
                    /* do not need to use 'neg' to negate the value, as c0
                     * string includes the minus sign.*/
                    r = FixReal(rval); /* convert to Fixed */
                } else {
                    if (neg)
                        val = -val;
                    r = FixInt(val); /* convert to Fixed */
                }
                /* Push(r); */
                if (stkindex >= STKMAX) {
                    LogMsg(LOGERROR, NONFATALERROR,
                           "Stack overflow while reading %s glyph.\n",
                           gGlyphName);
                    return;
                }
                stk[stkindex] = r;
                stkindex++;
                goto nxtChar;
            } else {
                LogMsg(LOGERROR, NONFATALERROR,
                       "Illegal number terminator while reading %s glyph.\n",
                       gGlyphName);
                return;
            }
        } /*end while true */
    }
}

bool
ReadGlyph(const ACFontInfo* fontinfo, const char* srcglyph, bool forBlendData,
          bool readHints)
{
    if (!srcglyph)
        return false;

    currentx = currenty = tempx = tempy = stkindex = 0;
    flex = gIdInFile = startchar = false;
    forMultiMaster = forBlendData;
    includeHints = readHints;

    ParseString(fontinfo, srcglyph);

    return true;
}
