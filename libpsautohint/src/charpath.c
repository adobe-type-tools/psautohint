/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include <stdarg.h>

#include "charpath.h"
#include "ac.h"
#include "bbox.h"
#include "opcodes.h"
#include "optable.h"

#define DONT_COMBINE_PATHS 1

#define DMIN 50       /* device minimum (one-half of a device pixel) */
#define GROWBUFF 2048 /* Amount to grow output buffer, if necessary. */
#define FONTSTKLIMIT 22

#define MAINHINTS -1
/* The following definitions are used when determining
 if a hinting operator used the start, end, average or
 flattened coordinate values. */
/* Segments are numbered starting with 1. The "ghost" segment is 0. */
#define STARTPT 0
#define ENDPT 1
#define AVERAGE 2
#define CURVEBBOX 3
#define FLATTEN 4
#define GHOST 5

static bool cubeLibrary = false;

static bool firstMT;
static Cd* refPtArray = NULL;
static char *outbuff;
static int16_t masterCount;
static const char** masterNames;
static size_t byteCount, buffSize;
static PPathList pathlist = NULL;
static indx hintsMasterIx = 0; /* The index of the master we read hints from */

/* Prototypes */
static void GetRelativePosition(Fixed, Fixed, Fixed, Fixed, Fixed, Fixed*);
static int16_t GetOperandCount(int16_t);
static void GetLengthandSubrIx(int16_t, int16_t*, int16_t*);

/* macros */
#define FixShift (8)
#define IntToFix(i) ((int32_t)(i) << FixShift)
#define FRnd(x) ((int32_t)(((x) + (1 << 7)) & ~0xFF))
#define FTrunc8(x) ((int32_t)((x) >> 8))
#define FixedToDouble(x) ((double)((x) / 256.0))
#define FixOne (0x100)
#define FixHalf (0x80)
#define TFMX(x) ((x))
#define TFMY(y) (-(y))
#define ITFMX(x) ((x))
#define ITFMY(y) (-(y))

#define Frac(x) ((x)&0xFF)
#define WRTNUM(i)       WriteToBuffer("%d ", (int)(i))
#define WriteStr(str)   WriteToBuffer("%s ", str)
#define WriteSubr(val)  WriteToBuffer("%d subr ", val)

/* Checks if buffer needs to grow before writing out string. */
static void
WriteToBuffer(char* format, ...)
{
    char outstr[MAXBUFFLEN + 1];
    size_t len;
    va_list va;

    va_start(va, format);
    len = vsnprintf(outstr, MAXBUFFLEN, format, va);
    va_end(va);

    if ((byteCount + len) > buffSize) {
        buffSize += GROWBUFF;
        outbuff = (char*)ReallocateMem(outbuff, buffSize, "file buffer");
    }

    sprintf(outbuff + byteCount, "%s", outstr);
    byteCount += len;
}

static void
WriteX(Fixed x)
{
    Fixed i = FRnd(x);
    WRTNUM(FTrunc8(i));
}

static void
WriteY(Fixed y)
{
    Fixed i = FRnd(y);
    WRTNUM(FTrunc8(i));
}

#define WriteCd(c)                                                             \
    {                                                                          \
        WriteX(c.x);                                                           \
        WriteY(c.y);                                                           \
    }

static void
WriteOneHintVal(Fixed val)
{
    if (Frac(val) == 0)
        WRTNUM(FTrunc8(val));
    else {
        WRTNUM(FTrunc8(FRnd(val * 100)));
        WriteStr("100 div ");
    }
}

/* Locates the first CP following the given path element. */
static int32_t
GetCPIx(indx mIx, int32_t pathIx)
{
    indx ix;

    for (ix = pathIx; ix < gPathEntries; ix++)
        if (pathlist[mIx].path[ix].type == CP)
            return ix;
    LogMsg(LOGERROR, NONFATALERROR, "No closepath in character: %s.\n",
           gGlyphName);
    return (-1);
}

/* Locates the first MT preceding the given path element. */
static int
GetMTIx(indx mIx, indx pathIx)
{
    indx ix;

    for (ix = pathIx; ix >= 0; ix--)
        if (pathlist[mIx].path[ix].type == RMT)
            return ix;
    LogMsg(LOGERROR, NONFATALERROR, "No moveto in character: %s.\n",
           gGlyphName);
    return (-1);
}

/* Locates the first MT SUCCEEDING the given path element. */
static int
GetNextMTIx(indx mIx, indx pathIx)
{
    indx ix;

    for (ix = pathIx; ix < gPathEntries; ix++)
        if (pathlist[mIx].path[ix].type == RMT)
            return ix;
    return (-1);
}

static void
GetEndPoint1(indx mIx, int32_t pathIx, Fixed* ptX, Fixed* ptY)
{
    PCharPathElt pathElt = &pathlist[mIx].path[pathIx];

retry:
    switch (pathElt->type) {
        case RMT:
        case RDT:
            *ptX = pathElt->x;
            *ptY = pathElt->y;
            break;
        case RCT:
            *ptX = pathElt->x3;
            *ptY = pathElt->y3;
            break;
        case CP:
            while (--pathIx >= 0) {
                pathElt = &pathlist[mIx].path[pathIx];
                if (pathElt->type == RMT)
                    goto retry;
            }
            LogMsg(LOGERROR, NONFATALERROR,
                   "Bad character description file: %s.\n", gGlyphName);
            break;
        default:
            LogMsg(LOGERROR, NONFATALERROR,
                   "Illegal operator in character file: %s.\n", gGlyphName);
    }
}

static void
GetEndPoints1(indx mIx, int32_t pathIx, Cd* start, Cd* end)
{
    if (pathlist[mIx].path[pathIx].type == RMT) {
        int32_t cpIx;

        GetEndPoint1(mIx, pathIx, &start->x, &start->y);
        /* Get index for closepath associated with this moveto. */
        cpIx = GetCPIx(mIx, pathIx + 1);
        GetEndPoint1(mIx, cpIx - 1, &end->x, &end->y);
    } else {
        GetEndPoint1(mIx, pathIx - 1, &start->x, &start->y);
        GetEndPoint1(mIx, pathIx, &end->x, &end->y);
    }
}

static void
GetCoordFromType(int16_t pathtype, Cd* coord, indx mIx, indx eltno)
{
    switch (pathtype) {
        case RMT:
        case RDT:
            (*coord).x = FTrunc8(FRnd(pathlist[mIx].path[eltno].x));
            (*coord).y = FTrunc8(FRnd(pathlist[mIx].path[eltno].y));
            break;
        case RCT:
            (*coord).x = FTrunc8(FRnd(pathlist[mIx].path[eltno].x3));
            (*coord).y = FTrunc8(FRnd(pathlist[mIx].path[eltno].y3));
            break;
        case CP:
            GetCoordFromType(pathlist[mIx].path[eltno - 1].type, coord, mIx,
                             eltno - 1);
            break;
    };
}

static const char* const pathTypes[] = { "moveto", "lineto", "curveto",
                                         "closepath" };

static const char*
GetPathType(int16_t pathtype)
{
    switch (pathtype) {
        case RMT:
            return pathTypes[0];
        case RDT:
            return pathTypes[1];
        case RCT:
            return pathTypes[2];
        case CP:
            return pathTypes[3];
        default:
            LogMsg(LOGERROR, NONFATALERROR,
                   "Illegal path type: %d in character: %s.\n", pathtype,
                   gGlyphName);
            return NULL;
    }
}

static void
FreePathElements(indx startix, indx stopix)
{
    indx i, j;

    for (j = startix; j < stopix; j++) {
        PHintElt hintElt, next;
        if (pathlist[j].path != NULL) {
            /* Before we can free hint elements will need to know gPathEntries
             value for char in each master because this proc can be
             called when characters are inconsistent.
             */
            for (i = 0; i < gPathEntries; i++) {
                hintElt = pathlist[j].path[i].hints;
                while (hintElt != NULL) {
                    next = hintElt->next;
                    UnallocateMem(hintElt);
                    hintElt = next;
                }
            }
        }
        UnallocateMem(pathlist[j].mainhints);
        UnallocateMem(pathlist[j].path);
    }
    UnallocateMem(pathlist);
    pathlist = NULL;
}

static void
InconsistentPointCount(indx ix, int entries1, int entries2)
{
    LogMsg(WARNING, OK, "The character: %s will not be included in the font\n "
                        "because the version in %s has a total of %d elements "
                        "and\n  the one in %s has %d elements.\n",
           gGlyphName, masterNames[0], (int)entries1, masterNames[ix],
           (int)entries2);
}

static void
InconsistentPathType(indx ix, int16_t type1, int16_t type2, indx eltno)
{
    Cd coord1, coord2;

    GetCoordFromType(type1, &coord1, 0, eltno);
    GetCoordFromType(type2, &coord2, ix, eltno);
    LogMsg(WARNING, OK,
           "The character: %s will not be included in the font\n  "
           "because the version in %s has path type %s at coord: %d "
           "%d\n  and the one in %s has type %s at coord %d %d.\n",
           gGlyphName, masterNames[0], GetPathType(type1), coord1.x, coord1.y,
           masterNames[ix], GetPathType(type2), coord2.x, coord2.y);
}

/* Returns whether changing the line to a curve is successful. */
static bool
ChangetoCurve(indx mIx, indx pathIx)
{
    Cd start = { 0, 0 }, end = { 0, 0 }, ctl1, ctl2;
    PCharPathElt pathElt = &pathlist[mIx].path[pathIx];

    if (pathElt->type == RCT)
        return true;
    /* Use the 1/3 rule to convert a line to a curve, i.e. put the control
     points
     1/3 of the total distance from each end point. */
    GetEndPoints1(mIx, pathIx, &start, &end);
    ctl1.x = FRnd((start.x * 2 + end.x + FixHalf) / 3);
    ctl1.y = FRnd((start.y * 2 + end.y + FixHalf) / 3);
    ctl2.x = FRnd((end.x * 2 + start.x + FixHalf) / 3);
    ctl2.y = FRnd((end.y * 2 + start.y + FixHalf) / 3);
    pathElt->type = RCT;
    pathElt->x1 = ctl1.x;
    pathElt->y1 = ctl1.y;
    pathElt->x2 = ctl2.x;
    pathElt->y2 = ctl2.y;
    pathElt->x3 = end.x;
    pathElt->y3 = end.y;
    pathElt->rx1 = ctl1.x - start.x;
    pathElt->ry1 = ctl1.y - start.y;
    pathElt->rx2 = pathElt->x2 - pathElt->x1;
    pathElt->ry2 = pathElt->y2 - pathElt->y1;
    pathElt->rx3 = pathElt->x3 - pathElt->x2;
    pathElt->ry3 = pathElt->y3 - pathElt->y2;
    return true;
}

static bool
ZeroLengthCP(indx mIx, indx pathIx)
{
    Cd startPt = { 0, 0 }, endPt = { 0, 0 };

    GetEndPoints1(mIx, pathIx, &startPt, &endPt);
    return (startPt.x == endPt.x && startPt.y == endPt.y);
}

/* Subtracts or adds one unit from the segment at pathIx. */
static void
AddLine(indx mIx, indx pathIx)
{
    Fixed fixTwo = IntToFix(2);
    Fixed xoffset = 0, yoffset = 0;
    Fixed xoffsetr = 0, yoffsetr = 0;
    PCharPathElt start, end, thisone;
    indx i, n;

    if (pathlist[mIx].path[pathIx].type != RCT) {
        LogMsg(WARNING, OK,
               "Please convert the point closepath in master: "
               "%s, character: %s to a line closepath.\n",
               masterNames[mIx], gGlyphName);
        return;
    }
    i = GetMTIx(mIx, pathIx) + 1;
    start = &pathlist[mIx].path[i];
    end = &pathlist[mIx].path[pathIx];
    /* Check control points to find out if x or y value should be adjusted
     in order to get a smooth curve. */
    switch (start->type) {
        case RDT:
            LogMsg(WARNING, OK,
                   "Please convert the point closepath to a line "
                   "closepath in master: %s, character: %s.\n",
                   masterNames[mIx], gGlyphName);
            return;
        case RCT:
            if ((abs(start->x1 - end->x2) < fixTwo) &&
                (abs(start->x1 - pathlist[mIx].path[i - 1].x) < fixTwo))
                yoffset = (start->y1 < end->y2 && end->y2 > 0) ||
                              (start->y1 > end->y2 && end->y2 < 0)
                            ? FixOne
                            : -FixOne;
            else if ((abs(start->y1 - end->y2) < fixTwo) &&
                     (abs(start->y1 - pathlist[mIx].path[i - 1].y) < fixTwo))
                xoffset = (start->x1 < end->x2 && end->x2 > 0) ||
                              (start->x1 > end->x2 && end->x2 < 0)
                            ? FixOne
                            : -FixOne;
            else {
                LogMsg(WARNING, OK,
                       "Could not modify point closepath in master "
                       "'%s', character: %s near (%d, %d).\n",
                       masterNames[mIx], gGlyphName, FTrunc8(end->x),
                       FTrunc8(end->y));
                return;
            }
            break;
        default:
            LogMsg(LOGERROR, NONFATALERROR,
                   "Bad character description file: %s/%s.\n", masterNames[mIx],
                   gGlyphName);
    }

    thisone = &(pathlist[mIx].path[pathIx]);
    thisone->x3 += xoffset;
    xoffsetr = (xoffset == 0) ? 0 : ((thisone->rx3 < 0) ? FixOne : -FixOne);
    thisone->rx3 += xoffsetr;

    thisone->y3 += yoffset;
    yoffsetr = (yoffset == 0) ? 0 : ((thisone->ry3 < 0) ? FixOne : -FixOne);
    thisone->ry3 += yoffsetr;

    /* Now, fix up the following MT's rx1, ry1 values
     This fixes a LOOOONG-standing bug.    renner Wed Jul 16 09:33:50 1997*/
    if ((n = GetNextMTIx(mIx, pathIx)) > 0) {
        PCharPathElt nxtone = &(pathlist[mIx].path[n]);
        nxtone->rx += (-xoffsetr);
        nxtone->ry += (-yoffsetr);
    }
}

#define PI 3.1415926535
static void
BestLine(PCharPathElt start, PCharPathElt end, Fixed* dx, Fixed* dy)
{
    double angle;
    /* control point differences */
    double cx = FixedToDouble(end->x2 - end->x3);
    double cy = FixedToDouble(end->y2 - end->y3);

    (void)start;

    *dx = *dy = 0;

    if (cy == 0.0 && cx == 0.0) {
        LogMsg(WARNING, OK, "Unexpected tangent in character path: %s.\n",
               gGlyphName);
        return;
    }

    angle = atan2(cy, cx);

#if FOUR_WAY
    /* Judy's non-Cube code only moves vertically or horizontally. */
    /* This code produces similar results. */
    if (angle < (-PI * 0.75)) {
        *dx = -FixOne;
    } else if (angle < (-PI * 0.25)) {
        *dy = -FixOne;
    } else if (angle < (PI * 0.25)) {
        *dx = FixOne;
    } else if (angle < (PI * 0.75)) {
        *dy = FixOne;
    } else {
        *dx = -FixOne;
    }
#else  /*FOUR_WAY*/

    if (angle < (-PI * 0.875)) {
        *dx = -FixOne;
    } else if (angle < (-PI * 0.625)) {
        *dy = -FixOne;
        *dx = -FixOne;
    } else if (angle < (-PI * 0.375)) {
        *dy = -FixOne;
    } else if (angle < (-PI * 0.125)) {
        *dy = -FixOne;
        *dx = FixOne;
    } else if (angle < (PI * 0.125)) {
        *dx = FixOne;
    } else if (angle < (PI * 0.375)) {
        *dx = FixOne;
        *dy = FixOne;
    } else if (angle < (PI * 0.625)) {
        *dy = FixOne;
    } else if (angle < (PI * 0.875)) {
        *dy = FixOne;
        *dx = -FixOne;
    } else {
        *dx = -FixOne;
    }
#endif /*FOUR_WAY*/
}

/* CUBE VERSION 17-Apr-94 jvz */
/* Curves: subtracts or adds one unit from the segment at pathIx. */
/* Lines: flags the segment at pathIx to be removed later; CP follows it. */
static void
AddLineCube(indx mIx, indx pathIx)
{
    /* Path element types have already been coordinated by ChangeToCurve. */
    /* Hints are only present in the hintsMasterIx. */

    if (pathlist[mIx].path[pathIx].type == RDT) {
        pathlist[mIx].path[pathIx].remove = true;
        if (pathlist[mIx].path[pathIx + 1].type != CP) {
            LogMsg(LOGERROR, NONFATALERROR, "Expected CP in path: %s.\n",
                   gGlyphName);
        }

        /* If there's another path in the character, we need to compensate */
        /* because CP does not update currentpoint. */

        if (pathIx + 2 < gPathEntries) {
            if (pathlist[mIx].path[pathIx + 2].type == RMT) {
                pathlist[mIx].path[pathIx + 2].rx +=
                  pathlist[mIx].path[pathIx].rx;
                pathlist[mIx].path[pathIx + 2].ry +=
                  pathlist[mIx].path[pathIx].ry;
            } else {
                LogMsg(LOGERROR, NONFATALERROR,
                       "Expected second RMT in path: %s.\n", gGlyphName);
            }
        }
    } else if (pathlist[mIx].path[pathIx].type == RCT) {
        Fixed dx = 0;
        Fixed dy = 0;
        PCharPathElt start;
        PCharPathElt end;
        indx mt; /* index of the moveto preceding this path */

        mt = GetMTIx(mIx, pathIx);
        start = &pathlist[mIx].path[mt + 1];
        end = &pathlist[mIx].path[pathIx];

        /* find nearest grid point we can move to */
        BestLine(start, end, &dx, &dy);

        /* note that moving rx2, ry2 will also move rx3, ry3 */
        pathlist[mIx].path[pathIx].x2 += dx;
        pathlist[mIx].path[pathIx].x3 += dx;
        pathlist[mIx].path[pathIx].rx2 += dx;

        pathlist[mIx].path[pathIx].y2 += dy;
        pathlist[mIx].path[pathIx].y3 += dy;
        pathlist[mIx].path[pathIx].ry2 += dy;
    } else {
        /* Not a RCT or RDT - error - unexpected path element type. */
        LogMsg(LOGERROR, NONFATALERROR, "Bad character description file: %s.\n",
               gGlyphName);
    }
}

/*
 Look for zero length closepaths.  If true then add a small
 one unit line to each design.  Because blending can cause
 small arithmetic errors at each point in the path the
 accumulation of such errors might cause the path to not end
 up at the same point where it started.  This can cause a sharp
 angle segment that may cause spike problems in old rasterizers,
 and in the qreducer (used for filling very large size characters).
 The one unit line, which is drawn by the closepath, takes up the
 slack for the arithmetic errors and avoids the spike problem.
 */
static void
CheckForZeroLengthCP(void)
{
    indx ix, pathIx;

    for (ix = 0; ix < masterCount; ix++) {
        for (pathIx = 0; pathIx < gPathEntries; pathIx++) {
            if (pathlist[ix].path[pathIx].type == CP &&
                ZeroLengthCP(ix, pathIx)) {
                if (cubeLibrary)
                    AddLineCube(ix, pathIx - 1);
                else
                    AddLine(ix, pathIx - 1);
            }
        }
    }
}

/* Checks that character paths for multiple masters have the same
 number of points and in the same path order.  If this isn't the
 case the character is not included in the font. */
static bool
CompareCharPaths(const ACFontInfo* fontinfo, const char** glyphs)
{
    indx mIx, ix, i;
    int32_t totalPathElt, minPathLen;
    bool ok = true;
    int16_t type1, type2;

    totalPathElt = minPathLen = MAXINT;
    if (pathlist == NULL) {
        pathlist = (PPathList)AllocateMem(masterCount, sizeof(PathList),
                                          "character path list");
    }

    for (mIx = 0; mIx < masterCount; mIx++) {
        ResetMaxPathEntries();
        SetCurrPathList(&pathlist[mIx]);
        gPathEntries = 0;

        if (hintsMasterIx == mIx) {
            /* read char data and hints from bez file */
            if (!ReadGlyph(fontinfo, glyphs[mIx], true, gAddHints))
                return false;
        } else {
            /* read char data only */
            if (!ReadGlyph(fontinfo, glyphs[mIx], true, false))
                return false;
        }

        if (mIx == 0)
            totalPathElt = gPathEntries;
        else if (gPathEntries != totalPathElt) {
            InconsistentPointCount(mIx, totalPathElt, gPathEntries);
            ok = false;
        }
        minPathLen = NUMMIN(NUMMIN(gPathEntries, totalPathElt), minPathLen);
    }

    for (mIx = 1; mIx < masterCount; mIx++) {
        for (i = 0; i < minPathLen; i++) {
            if ((type1 = pathlist[0].path[i].type) !=
                (type2 = pathlist[mIx].path[i].type)) {

                if ((type1 == RDT) &&
                    (type2 == RCT)) { /* Change this element in all previous
                                         masters to a curve. */
                    ix = mIx - 1;
                    do {
                        ok = ok && ChangetoCurve(ix, i);
                        ix--;
                    } while (ix >= 0);
                } else if ((type1 == RCT) && (type2 == RDT))
                    ok = ok && ChangetoCurve(mIx, i);
                else {
                    InconsistentPathType(mIx, pathlist[0].path[i].type,
                                         pathlist[mIx].path[i].type, i);
                    ok = false;
                    /* skip to next subpath */
                    while (++i < minPathLen && (pathlist[0].path[i].type != CP))
                        ;
                }
            }
        }
    }
    return ok;
}

static void
SetSbandWidth(void)
{
    indx mIx;

    for (mIx = 0; mIx < masterCount; mIx++) {
        pathlist[mIx].sb = 0;
        pathlist[mIx].width = 1000;
    }
}

static void
WriteSbandWidth(void)
{
    int16_t subrix, length, opcount = GetOperandCount(SBX);
    indx ix, j, startix = 0;
    bool writeSubrOnce, sbsame = true, wsame = true;

    for (ix = 1; ix < masterCount; ix++) {
        sbsame = sbsame && (pathlist[ix].sb == pathlist[ix - 1].sb);
        wsame = wsame && (pathlist[ix].width == pathlist[ix - 1].width);
    }
    if (sbsame && wsame) {
        WriteToBuffer("%d %d ", pathlist[0].sb, pathlist[0].width);
    } else if (sbsame) {
        WriteToBuffer("%d ", pathlist[0].sb);
        for (j = 0; j < masterCount; j++) {
            WriteToBuffer("%d ",
                    (j == 0) ? pathlist[j].width
                             : pathlist[j].width - pathlist[0].width);
        }
        GetLengthandSubrIx(1, &length, &subrix);
        WriteSubr(subrix);
    } else if (wsame) {
        for (j = 0; j < masterCount; j++) {
            WriteToBuffer("%d ",
                    (j == 0) ? pathlist[j].sb
                             : pathlist[j].sb - pathlist[0].sb);
        }
        GetLengthandSubrIx(1, &length, &subrix);
        WriteSubr(subrix);
        WriteToBuffer("%d ", pathlist[0].width);
    } else {
        GetLengthandSubrIx(opcount, &length, &subrix);
        if ((writeSubrOnce = (length == opcount))) {
            WriteToBuffer("%d %d ", pathlist[0].sb, pathlist[0].width);
            length = startix = 1;
        }
        for (ix = 0; ix < opcount; ix += length) {
            for (j = startix; j < masterCount; j++) {
                WriteToBuffer("%d ",
                        (ix == 0)
                          ? (j == 0) ? pathlist[j].sb
                                     : pathlist[j].sb - pathlist[0].sb
                          : (j == 0) ? (int32_t)pathlist[j].width
                                     : (int32_t)(pathlist[j].width -
                                                 pathlist[0].width));
            }
            if (!writeSubrOnce || (ix == (opcount - 1)))
                WriteSubr(subrix);
        }
    }
    WriteToBuffer("sbx\n");
}

static bool
CurveBBox(indx mIx, int16_t hinttype, int32_t pathIx, Fixed* value)
{
    Cd startPt, endPt;
    Fixed llx, lly, urx, ury, minval = 0, maxval = 0;
    Fixed p1 = 0, p2 = 0, *minbx = 0, *maxbx = 0;
    CharPathElt pathElt;

    *value = IntToFix(10000);
    pathElt = pathlist[mIx].path[pathIx];
    GetEndPoints1(mIx, pathIx, &startPt, &endPt);
    switch (hinttype) {
        case RB:
        case RV + ESCVAL:
            minval = TFMY(NUMMIN(startPt.y, endPt.y));
            maxval = TFMY(NUMMAX(startPt.y, endPt.y));
            p1 = TFMY(pathElt.y1);
            p2 = TFMY(pathElt.y2);
            minbx = &lly;
            maxbx = &ury;
            break;
        case RY:
        case RM + ESCVAL:
            minval = TFMX(NUMMIN(startPt.x, endPt.x));
            maxval = TFMX(NUMMAX(startPt.x, endPt.x));
            p1 = TFMX(pathElt.x1);
            p2 = TFMX(pathElt.x2);
            minbx = &llx;
            maxbx = &urx;
            break;
        default:
            LogMsg(LOGERROR, NONFATALERROR,
                   "Illegal hint type in character: %s.\n", gGlyphName);
    }
    if (p1 - maxval >= FixOne || p2 - maxval >= FixOne ||
        p1 - minval <= FixOne || p2 - minval <= FixOne) {
        /* Transform coordinates so I get the same value that AC would give. */
        FindCurveBBox(TFMX(startPt.x), TFMY(startPt.y), TFMX(pathElt.x1),
                      TFMY(pathElt.y1), TFMX(pathElt.x2), TFMY(pathElt.y2),
                      TFMX(endPt.x), TFMY(endPt.y), &llx, &lly, &urx, &ury);
        if (*maxbx > maxval || minval > *minbx) {
            if (minval - *minbx > *maxbx - maxval)
                *value = (hinttype == RB || hinttype == RV + ESCVAL)
                           ? ITFMY(*minbx)
                           : ITFMX(*minbx);
            else
                *value = (hinttype == RB || hinttype == RV + ESCVAL)
                           ? ITFMY(*maxbx)
                           : ITFMX(*maxbx);
            return true;
        }
    }
    return false;
}
#if __CENTERLINE__
static char*
_HintType_(int typ)
{
    switch (typ) {
        case GHOST:
            return ("Ghost");
            break;
        case AVERAGE:
            return ("Average");
            break;
        case CURVEBBOX:
            return ("Curvebbox");
            break;
        case ENDPT:
            return ("Endpt");
            break;
        case STARTPT:
            return ("Startpt");
            break;
        case FLATTEN:
            return ("Flatten");
            break;
    }
    return ("Unknown");
}
static char*
_HintKind_(int hinttype)
{
    switch (hinttype) {
        case RB:
            return ("RB");
            break;
        case RV + ESCVAL:
            return ("RV");
            break;
        case RY:
            return ("RY");
            break;
        case RM + ESCVAL:
            return ("RM");
            break;
    }
    return ("??");
}
static char*
_elttype_(int indx)
{
    switch (pathlist[hintsMasterIx].path[indx].type) {
        case RMT:
            return ("Moveto");
            break;
        case RDT:
            return ("Lineto");
            break;
        case RCT:
            return ("Curveto");
            break;
        case CP:
            return ("Closepath");
            break;
    }
    return ("Unknown");
}

#endif

static bool
nearlyequal_(Fixed a, Fixed b, Fixed tolerance)
{
    return (abs(a - b) <= tolerance);
}

/* Returns whether the hint values are derived from the start,
 average, end or flattened curve with an inflection point of
 the specified path element. Since path element numbers in
 character files start from one and the path array starts
 from zero we need to subtract one from the path index. */
static int16_t
GetPointType(int16_t hinttype, Fixed value, int32_t* pathEltIx)
{
    Cd startPt, endPt;
    Fixed startval = 0, endval = 0, loc;
    int16_t pathtype;
    bool tryAgain = true;
    int32_t pathIx = *pathEltIx - 1;

#if __CENTERLINE__
    if (TRACE) {
        fprintf(stderr, "Enter GetPointType: Hinttype=%s @(%.2f) curr "
                        "patheltix=%d pathIx=%d  <%s>",
                _HintKind_(hinttype), FIXED2FLOAT(value), *pathEltIx, pathIx,
                _elttype_(pathIx));
    }
#endif

retry:
    GetEndPoints1(hintsMasterIx, pathIx, &startPt, &endPt);
    switch (hinttype) {
        case RB:
        case RV + ESCVAL:
            startval = startPt.y;
            endval = endPt.y;
#if __CENTERLINE__
            if (TRACE)
                fprintf(stderr, "Startval Y=%.2f EndVal Y=%.2f ",
                        FIXED2FLOAT(startval), FIXED2FLOAT(endval));
#endif
            break;
        case RY:
        case RM + ESCVAL:
            startval = startPt.x;
            endval = endPt.x;
#if __CENTERLINE__
            if (TRACE)
                fprintf(stderr, "Startval X=%.2f EndVal X=%.2f ",
                        FIXED2FLOAT(startval), FIXED2FLOAT(endval));
#endif
            break;
        default:
            LogMsg(LOGERROR, NONFATALERROR,
                   "Illegal hint type in character: %s.\n", gGlyphName);
    }

    /* Check for exactly equal first, in case endval = startval + 1. jvz 1nov95
     */
    /* Certain cases are still ambiguous. */

    if (value == startval) {
#if __CENTERLINE__
        if (TRACE)
            fprintf(stderr, "==> StartPt\n");
#endif
        return STARTPT;
    } else if (value == endval) {
#if __CENTERLINE__
        if (TRACE)
            fprintf(stderr, "==> EndPt\n");
#endif
        return ENDPT;
    } else if (nearlyequal_(value, startval, FixOne)) {
#if __CENTERLINE__
        if (TRACE)
            fprintf(stderr, "==> ~StartPt\n");
#endif
        return STARTPT;
    } else if (nearlyequal_(value, endval, FixOne)) {
#if __CENTERLINE__
        if (TRACE)
            fprintf(stderr, "==> ~EndPt\n");
#endif
        return ENDPT;
    } else if (value == (loc = FixHalfMul(startval + endval)) ||
               nearlyequal_(value, loc, FixOne)) {
#if __CENTERLINE__
        if (TRACE)
            fprintf(stderr, "==> Average\n");
#endif
        return AVERAGE;
    }

    pathtype = pathlist[hintsMasterIx].path[pathIx].type;
    if (tryAgain && (pathIx + 1 < gPathEntries) &&
        (pathtype != CP)) { /* try looking at other end of line or curve */
        pathIx++;
        *pathEltIx += 1;
        tryAgain = false;
#if __CENTERLINE__
        if (TRACE)
            fprintf(stderr, " (Retry w/PathEltix=%d) ", *pathEltIx);
#endif
        goto retry;
    }
    if (!tryAgain) /* reset pathEltIx to original value */ {
        *pathEltIx -= 1;
#if __CENTERLINE__
        if (TRACE)
            fprintf(stderr, " (reset PathEltix to %d) ", *pathEltIx);
#endif
    }
    if (CurveBBox(hintsMasterIx, hinttype, *pathEltIx - 1, &loc) &&
        nearlyequal_(value, loc, FixOne)) {
#if __CENTERLINE__
        if (TRACE)
            fprintf(stderr, "==> Curvebbox\n");
#endif
        return CURVEBBOX;
    }
#if __CENTERLINE__
    if (TRACE)
        fprintf(stderr, "==> Flatten fallout\n");
#endif
    return FLATTEN;
}

static void
GetRelPos(int32_t pathIx, int16_t hinttype, Fixed hintVal, Cd* startPt,
          Cd* endPt, Fixed* val)
{
    Cd origStart, origEnd;

    GetEndPoints1(hintsMasterIx, pathIx, &origStart, &origEnd);
    if (hinttype == RB || hinttype == (RV + ESCVAL))
        GetRelativePosition(endPt->y, startPt->y, origEnd.y, origStart.y,
                            hintVal, val);
    else
        GetRelativePosition(endPt->x, startPt->x, origEnd.x, origStart.x,
                            hintVal, val);
}

/* Calculates the relative position of hintVal between its endpoints and
 gets new hint value between currEnd and currStart. */
static void
GetRelativePosition(Fixed currEnd, Fixed currStart, Fixed end, Fixed start,
                    Fixed hintVal, Fixed* fixedRelValue)
{
    if ((end - start) == 0)
        *fixedRelValue = (Fixed)LROUND((float)(hintVal - start) + currStart);
    else {
        float relVal = (float)(hintVal - start) / (float)(end - start);
        *fixedRelValue =
          (Fixed)LROUND(((currEnd - currStart) * relVal) + currStart);
    }
}

/* For each base design, excluding HintsDir, include the
 hint information at the specified path element. type1
 and type2 indicates whether to use the start, end, avg.,
 curvebbox or flattened curve.  If a curve is to be flattened
 check if this is an "s" curve and use the inflection point.  If not
 then use the same relative position between the two endpoints as
 in the main hints dir.
 hinttype is either RB, RY, RM or RV. pathEltIx
 is the index into the path array where the new hint should
 be stored.  pathIx is the index of the path segment used to
 calculate this particular hint. */
static void
InsertHint(PHintElt currHintElt, indx pathEltIx, int16_t type1, int16_t type2)
{
    indx ix, j;
    Cd startPt, endPt;
    PHintElt *hintElt, newEntry;
    CharPathElt pathElt;
    int32_t pathIx;
    int16_t pathtype, hinttype = currHintElt->type;
    Fixed *value, ghostVal = 0, tempVal;

#if __CENTERLINE__
    if (TRACE) {
        fprintf(stderr, "InsertHint: ");
        fprintf(stderr, "Type1=%s Type2=%s", _HintType_(type1),
                _HintType_(type2));
        fprintf(stderr, " PathEltIndex= ");
        if (pathEltIx == MAINHINTS) {
            fprintf(stderr, "MainHints ");
        } else {
            Fixed tx, ty;
            fprintf(stderr, "%d ", pathEltIx);
            if (type1 != GHOST) {
                GetEndPoint1(hintsMasterIx, currHintElt->pathix1 - 1, &tx, &ty);
                fprintf(stderr, "Start attached to (%.2f %.2f)",
                        FIXED2FLOAT(tx), FIXED2FLOAT(ty));
            }
            if (type2 != GHOST) {
                GetEndPoint1(hintsMasterIx, currHintElt->pathix2 - 1, &tx, &ty);
                fprintf(stderr, "End attached to (%.2f %.2f)", FIXED2FLOAT(tx),
                        FIXED2FLOAT(ty));
            }
        }
        fprintf(stderr, "\n");
    }
#endif

    if (type1 == GHOST || type2 == GHOST)
        /* ghostVal should be -20 or -21 */
        ghostVal = currHintElt->rightortop - currHintElt->leftorbot;
    for (ix = 0; ix < masterCount; ix++) {
        if (ix == hintsMasterIx)
            continue;
        newEntry = (PHintElt)AllocateMem(1, sizeof(HintElt), "hint element");
        newEntry->type = hinttype;
        hintElt =
          (pathEltIx == MAINHINTS ? &pathlist[ix].mainhints
                                  : &pathlist[ix].path[pathEltIx].hints);
        while (*hintElt != NULL && (*hintElt)->next != NULL)
            hintElt = &(*hintElt)->next;
        if (*hintElt == NULL)
            *hintElt = newEntry;
        else
            (*hintElt)->next = newEntry;
        for (j = 0; j < 2; j++) {
            if (j == 0) {
                pathIx = currHintElt->pathix1 - 1;
                pathtype = type1;
                value = &newEntry->leftorbot;
            } else {
                pathIx = currHintElt->pathix2 - 1;
                pathtype = type2;
                value = &newEntry->rightortop;
            }
            if (pathtype != GHOST)
                GetEndPoints1(ix, pathIx, &startPt, &endPt);
            switch (pathtype) {
                case AVERAGE:
                    *value = ((hinttype == RB || hinttype == (RV + ESCVAL))
                                ? FixHalfMul(startPt.y + endPt.y)
                                : FixHalfMul(startPt.x + endPt.x));
                    break;
                case CURVEBBOX:
                    if (!CurveBBox(ix, hinttype, pathIx, value)) {
                        GetRelPos(pathIx, hinttype,
                                  ((j == 0) ? currHintElt->leftorbot
                                            : currHintElt->rightortop),
                                  &startPt, &endPt, &tempVal);
                        *value = FRnd(tempVal);
                    }
                    break;
                case ENDPT:
                    *value =
                      ((hinttype == RB || hinttype == (RV + ESCVAL)) ? endPt.y
                                                                     : endPt.x);
                    break;
                case FLATTEN:
                    pathElt = pathlist[ix].path[pathIx];
                    if (pathElt.type != RCT) {
                        LogMsg(LOGERROR, NONFATALERROR,
                               "Malformed path list: %s, master: %s, "
                               "element: %d, type: %s != curveto.\n",
                               gGlyphName, masterNames[ix], pathIx,
                               GetPathType(pathElt.type));
                    }
                    if (!GetInflectionPoint(startPt.x, startPt.y, pathElt.x1,
                                            pathElt.y1, pathElt.x2, pathElt.y2,
                                            pathElt.x3, pathElt.y3,
                                            value)) { /* no flat spot found */
                        /* Get relative position of value in currHintElt. */
                        GetRelPos(pathIx, hinttype,
                                  ((j == 0) ? currHintElt->leftorbot
                                            : currHintElt->rightortop),
                                  &startPt, &endPt, &tempVal);
                        *value = FRnd(tempVal);
                    }
                    break;
                case GHOST:
                    if (j == 1)
                        *value = newEntry->leftorbot + ghostVal;
                    break;
                case STARTPT:
                    *value = ((hinttype == RB || hinttype == (RV + ESCVAL))
                                ? startPt.y
                                : startPt.x);
                    break;
                default:
                    LogMsg(LOGERROR, NONFATALERROR,
                           "Illegal point type in character: %s.\n",
                           gGlyphName);
            }
            /* Assign correct value for bottom band if first path element
             is a ghost band. */
            if (j == 1 && type1 == GHOST)
                newEntry->leftorbot = newEntry->rightortop - ghostVal;
        }
    }
}

static void
ReadHints(PHintElt hintElt, indx pathEltIx)
{
    PHintElt currElt = hintElt;
    int16_t pointtype1, pointtype2;

    while (currElt != NULL) {
        if (currElt->pathix1 != 0)
            pointtype1 = GetPointType(currElt->type, currElt->leftorbot,
                                      &(currElt->pathix1));
        else
            pointtype1 = GHOST;
        if (currElt->pathix2 != 0)
            pointtype2 = GetPointType(currElt->type, currElt->rightortop,
                                      &(currElt->pathix2));
        else
            pointtype2 = GHOST;
        InsertHint(currElt, pathEltIx, pointtype1, pointtype2);
        currElt = currElt->next;
    }
}

/* Reads hints from hints master path list and assigns corresponding
 hints to other designs. */
static bool
ReadandAssignHints(void)
{
    indx ix;

    /* Check for main hints first, i.e. global to character. */
    if (pathlist[hintsMasterIx].mainhints != NULL)
        ReadHints(pathlist[hintsMasterIx].mainhints, MAINHINTS);
    /* Now check for local hint values. */
    for (ix = 0; ix < gPathEntries; ix++) {
        if (pathlist[hintsMasterIx].path == NULL)
            return false;
        if (pathlist[hintsMasterIx].path[ix].hints != NULL)
            ReadHints(pathlist[hintsMasterIx].path[ix].hints, ix);
    }
    return true;
}

static bool
DoubleCheckFlexVals(indx dirnum, indx eltix, indx hintdirnum)
{
    bool vert = (pathlist[hintdirnum].path[eltix].x ==
                 pathlist[hintdirnum].path[eltix + 1].x3);
    if (vert) {
        return (pathlist[dirnum].path[eltix].x ==
                pathlist[dirnum].path[eltix + 1].x3);
    } else {
        return (pathlist[dirnum].path[eltix].y ==
                pathlist[dirnum].path[eltix + 1].y3);
    }
}

static bool
CheckFlexOK(indx ix)
{
    indx i;
    bool flexOK = pathlist[hintsMasterIx].path[ix].isFlex;
    PCharPathElt end;

    for (i = 0; i < masterCount; i++) {
        if (i == hintsMasterIx)
            continue;
        if (flexOK && (!pathlist[i].path[ix].isFlex)) {
            if (!DoubleCheckFlexVals(i, ix, hintsMasterIx)) {
                end = &pathlist[i].path[ix];
                LogMsg(WARNING, OK,
                       "Flex will not be included in character: %s "
                       "in '%s' at element %d near (%d, %d) because "
                       "the character does not have flex in each "
                       "design.\n",
                       gGlyphName, masterNames[i], (int)ix, FTrunc8(end->x),
                       FTrunc8(end->y));
                return false;
            } else {
                pathlist[i].path[ix].isFlex = flexOK;
            }
        }
    }
    return flexOK;
}

static void
OptimizeCT(indx ix)
{
    int16_t newtype = 0;
    bool vhct = true, hvct = true;
    indx i;

    for (i = 0; i < masterCount; i++)
        if (pathlist[i].path[ix].rx1 != 0 || pathlist[i].path[ix].ry3 != 0) {
            vhct = false;
            break;
        }
    for (i = 0; i < masterCount; i++)
        if (pathlist[i].path[ix].ry1 != 0 || pathlist[i].path[ix].rx3 != 0) {
            hvct = false;
            break;
        }
    if (vhct)
        newtype = VHCT;
    else if (hvct)
        newtype = HVCT;
    if (vhct || hvct)
        for (i = 0; i < masterCount; i++)
            pathlist[i].path[ix].type = newtype;
}

static void
MtorDt(Cd coord, indx startix, int16_t length)
{
    if (length == 2) {
        WriteCd(coord);
    } else if (startix == 0)
        WriteX(coord.x);
    else
        WriteY(coord.y);
}

static void
Hvct(Cd coord1, Cd coord2, Cd coord3, indx startix, int16_t length)
{
    indx ix;
    indx lastix = startix + length;

    for (ix = startix; ix < lastix; ix++)
        switch (ix) {
            case 0:
                WriteX(coord1.x);
                break;
            case 1:
                WriteX(coord2.x);
                break;
            case 2:
                WriteY(coord2.y);
                break;
            case 3:
                WriteY(coord3.y);
                break;
            default:
                LogMsg(LOGERROR, NONFATALERROR,
                       "Invalid index value: %d defined for curveto "
                       "command1 in character: %s.\n",
                       (int)ix, gGlyphName);
                break;
        }
}

static void
Vhct(Cd coord1, Cd coord2, Cd coord3, indx startix, int16_t length)
{
    indx ix;
    indx lastix = startix + length;

    for (ix = startix; ix < lastix; ix++)
        switch (ix) {
            case 0:
                WriteY(coord1.y);
                break;
            case 1:
                WriteX(coord2.x);
                break;
            case 2:
                WriteY(coord2.y);
                break;
            case 3:
                WriteX(coord3.x);
                break;
            default:
                LogMsg(LOGERROR, NONFATALERROR,
                       "Invalid index value: %d defined for curveto "
                       "command2 in character:%s.\n",
                       (int)ix, gGlyphName);
                break;
        }
}

/* length can only be 1, 2, 3 or 6 */
static void
Ct(Cd coord1, Cd coord2, Cd coord3, indx startix, int16_t length)
{
    indx ix;
    indx lastix = startix + length;

    for (ix = startix; ix < lastix; ix++)
        switch (ix) {
            case 0:
                WriteX(coord1.x);
                break;
            case 1:
                WriteY(coord1.y);
                break;
            case 2:
                WriteX(coord2.x);
                break;
            case 3:
                WriteY(coord2.y);
                break;
            case 4:
                WriteX(coord3.x);
                break;
            case 5:
                WriteY(coord3.y);
                break;
            default:
                LogMsg(LOGERROR, NONFATALERROR,
                       "Invalid index value: %d defined for curveto "
                       "command3 in character: %s.\n",
                       (int)ix, gGlyphName);
                break;
        }
}

static void
ReadHorVStem3Values(indx pathIx, int16_t eltno, int16_t hinttype,
                    bool* errormsg)
{
    indx ix;
    PHintElt* hintElt = NULL;
    int16_t count;
    bool ok = true;
    Fixed min, dmin, mid, dmid, max, dmax;

    for (ix = 0; ix < masterCount; ix++) {
        count = 1;
        if (ix == hintsMasterIx)
            continue;
        hintElt = (pathIx == MAINHINTS ? &pathlist[ix].mainhints
                                       : &pathlist[ix].path[pathIx].hints);
        /* Find specified hint element. */
        while (*hintElt != NULL && count != eltno) {
            hintElt = &(*hintElt)->next;
            count++;
        }
        /* Check that RM or RV type is in pairs of threes. */
        if (*hintElt == NULL || (*hintElt)->next == NULL ||
            (*hintElt)->next->next == NULL) {
            LogMsg(LOGERROR, NONFATALERROR,
                   "Invalid format for hint operator: hstem3 or "
                   "vstem3 in character: %s/%s.\n",
                   masterNames[ix], gGlyphName);
        }
        if ((*hintElt)->type != hinttype ||
            (*hintElt)->next->type != hinttype ||
            (*hintElt)->next->next->type != hinttype) {
            LogMsg(LOGERROR, NONFATALERROR,
                   "Invalid format for hint operator: hstem3 or "
                   "vstem3 in character: %s in '%s'.\n",
                   gGlyphName, masterNames[ix]);
        }
        min = (*hintElt)->leftorbot;
        dmin = (*hintElt)->rightortop - min;
        mid = (*hintElt)->next->leftorbot;
        dmid = (*hintElt)->next->rightortop - mid;
        max = (*hintElt)->next->next->leftorbot;
        dmax = (*hintElt)->next->next->rightortop - max;
        /* Check that counters are the same width and stems are the same width.
         */
        if (dmin != dmax || (((mid + dmid / 2) - (min + dmin / 2)) !=
                             ((max + dmax / 2) - (mid + dmid / 2)))) {
            ok = false;
            break;
        }
    }
    if (!ok) { /* change RM's to RY's or RV's to RB's for this element in each
                  master */
        int16_t newhinttype = (hinttype == (RM + ESCVAL) ? RY : RB);
        if (*errormsg) {
            LogMsg(WARNING, OK,
                   "Near miss for using operator: %s in character: "
                   "%s in '%s'. (min=%d..%d[delta=%d], "
                   "mid=%d..%d[delta=%d], max=%d..%d[delta=%d])\n",
                   (hinttype == (RM + ESCVAL)) ? "vstem3" : "hstem3",
                   gGlyphName, masterNames[ix], FTrunc8((*hintElt)->leftorbot),
                   FTrunc8((*hintElt)->rightortop),
                   FTrunc8((*hintElt)->rightortop - (*hintElt)->leftorbot),
                   FTrunc8((*hintElt)->next->leftorbot),
                   FTrunc8((*hintElt)->next->rightortop),
                   FTrunc8((*hintElt)->next->rightortop -
                           (*hintElt)->next->leftorbot),
                   FTrunc8((*hintElt)->next->next->leftorbot),
                   FTrunc8((*hintElt)->next->next->rightortop),
                   FTrunc8((*hintElt)->next->next->rightortop -
                           (*hintElt)->next->next->leftorbot));
            *errormsg = false;
        }
        for (ix = 0; ix < masterCount; ix++) {
            count = 1;
            hintElt = (pathIx == MAINHINTS ? &pathlist[ix].mainhints
                                           : &pathlist[ix].path[pathIx].hints);
            /* Find specified hint element. */
            while (*hintElt != NULL && count != eltno) {
                hintElt = &(*hintElt)->next;
                count++;
            }
            /* Already checked that hintElt->next, etc. exists,
             so don't need to do it again. */
            (*hintElt)->type = newhinttype;
            (*hintElt)->next->type = newhinttype;
            (*hintElt)->next->next->type = newhinttype;
        }
    }
}

/* Go through each hint element and check that all rm's and rv's
 meet the necessary criteria. */
static void
FindHandVStem3(PHintElt* hintElt, indx pathIx, bool* errormsg)
{
    int16_t count = 1;

    while (*hintElt != NULL) {
        if ((*hintElt)->type == (RM + ESCVAL) ||
            (*hintElt)->type == (RV + ESCVAL)) {
            ReadHorVStem3Values(pathIx, count, (*hintElt)->type, errormsg);
            /* RM's and RV's are in pairs of 3 */
            hintElt = &(*hintElt)->next->next->next;
            count += 3;
        } else {
            hintElt = &(*hintElt)->next;
            count++;
        }
    }
}

static void
CheckHandVStem3(void)
{
    indx ix;
    bool errormsg = true;

    FindHandVStem3(&pathlist[hintsMasterIx].mainhints, MAINHINTS, &errormsg);
    for (ix = 0; ix < gPathEntries; ix++)
        FindHandVStem3(&pathlist[hintsMasterIx].path[ix].hints, ix, &errormsg);
}

static void
CheckFlexValues(int16_t* operator, indx eltix, indx flexix, bool* xequal,
                bool* yequal)
{
    indx ix;
    Cd coord = { 0, 0 };

    *operator= RMT;
    if (flexix < 2)
        return;

    *xequal = *yequal = true;
    for (ix = 1; ix < masterCount; ix++)
        switch (flexix) {
            case 2:
                if ((coord.x = pathlist[ix].path[eltix].rx2) !=
                    pathlist[ix - 1].path[eltix].rx2)
                    *xequal = false;
                if ((coord.y = pathlist[ix].path[eltix].ry2) !=
                    pathlist[ix - 1].path[eltix].ry2)
                    *yequal = false;
                break;
            case 3:
                if ((coord.x = pathlist[ix].path[eltix].rx3) !=
                    pathlist[ix - 1].path[eltix].rx3)
                    *xequal = false;
                if ((coord.y = pathlist[ix].path[eltix].ry3) !=
                    pathlist[ix - 1].path[eltix].ry3)
                    *yequal = false;
                break;
            case 4:
                if ((coord.x = pathlist[ix].path[eltix + 1].rx1) !=
                    pathlist[ix - 1].path[eltix + 1].rx1)
                    *xequal = false;
                if ((coord.y = pathlist[ix].path[eltix + 1].ry1) !=
                    pathlist[ix - 1].path[eltix + 1].ry1)
                    *yequal = false;
                break;
            case 5:
                if ((coord.x = pathlist[ix].path[eltix + 1].rx2) !=
                    pathlist[ix - 1].path[eltix + 1].rx2)
                    *xequal = false;
                if ((coord.y = pathlist[ix].path[eltix + 1].ry2) !=
                    pathlist[ix - 1].path[eltix + 1].ry2)
                    *yequal = false;
                break;
            case 6:
                if ((coord.x = pathlist[ix].path[eltix + 1].rx3) !=
                    pathlist[ix - 1].path[eltix + 1].rx3)
                    *xequal = false;
                if ((coord.y = pathlist[ix].path[eltix + 1].ry3) !=
                    pathlist[ix - 1].path[eltix + 1].ry3)
                    *yequal = false;
                break;
            case 7:
                if ((coord.x = pathlist[ix].path[eltix + 1].x3) !=
                    pathlist[ix - 1].path[eltix + 1].x3)
                    *xequal = false;
                if ((coord.y = pathlist[ix].path[eltix + 1].y3) !=
                    pathlist[ix - 1].path[eltix + 1].y3)
                    *yequal = false;
                break;
        }
    if (!(*xequal) && !(*yequal))
        return;

    if (*xequal && (coord.x == 0)) {
        *operator= VMT;
        *xequal = false;
    }
    if (*yequal && (coord.y == 0)) {
        *operator= HMT;
        *yequal = false;
    }
}

static void
GetFlexCoord(indx rmtCt, indx mIx, indx eltix, Cd* coord)
{
    switch (rmtCt) {
        case 0:
            (*coord).x = refPtArray[mIx].x - pathlist[mIx].path[eltix].x;
            (*coord).y = refPtArray[mIx].y - pathlist[mIx].path[eltix].y;
            break;
        case 1:
            (*coord).x = pathlist[mIx].path[eltix].x1 - refPtArray[mIx].x;
            (*coord).y = pathlist[mIx].path[eltix].y1 - refPtArray[mIx].y;
            break;
        case 2:
            (*coord).x = pathlist[mIx].path[eltix].rx2;
            (*coord).y = pathlist[mIx].path[eltix].ry2;
            break;
        case 3:
            (*coord).x = pathlist[mIx].path[eltix].rx3;
            (*coord).y = pathlist[mIx].path[eltix].ry3;
            break;
        case 4:
            (*coord).x = pathlist[mIx].path[eltix + 1].rx1;
            (*coord).y = pathlist[mIx].path[eltix + 1].ry1;
            break;
        case 5:
            (*coord).x = pathlist[mIx].path[eltix + 1].rx2;
            (*coord).y = pathlist[mIx].path[eltix + 1].ry2;
            break;
        case 6:
            (*coord).x = pathlist[mIx].path[eltix + 1].rx3;
            (*coord).y = pathlist[mIx].path[eltix + 1].ry3;
            break;
        case 7:
            (*coord).x = pathlist[mIx].path[eltix + 1].x3;
            (*coord).y = pathlist[mIx].path[eltix + 1].y3;
            break;
    }
}

/*  NOTE:  Mathematical transformations are applied to the multi-master
 data in order to decrease the computation during font execution.
 See /user/foley/atm/blendfont1.910123 for the definition of this
 transformation.  This transformation is applied whenever charstring
 data is written  (i.e. sb and width, flex, hints, dt, ct, mt) AND
 an OtherSubr 7 - 11 will be called. */
static void
WriteFlex(indx eltix)
{
    bool vert = (pathlist[hintsMasterIx].path[eltix].x ==
                 pathlist[hintsMasterIx].path[eltix + 1].x3);
    Cd coord, coord0; /* array of reference points */
    bool xsame, ysame, writeSubrOnce;
    int16_t optype;
    indx ix, j, opix, startix;
    int16_t subrIx, length;

    refPtArray =
      (Cd*)AllocateMem(masterCount, sizeof(Cd), "reference point array");
    for (ix = 0; ix < masterCount; ix++) {
        refPtArray[ix].x =
          (vert ? pathlist[ix].path[eltix].x : pathlist[ix].path[eltix].x3);
        refPtArray[ix].y =
          (vert ? pathlist[ix].path[eltix].y3 : pathlist[ix].path[eltix].y);
    }
    WriteToBuffer("1 subr\n");
    for (j = 0; j < 8; j++) {
        int16_t opcount;
        if (j == 7)
            WRTNUM(DMIN);
        xsame = ysame = false;
        CheckFlexValues(&optype, eltix, j, &xsame, &ysame);
        opcount = GetOperandCount(optype);
        if ((xsame && !ysame) || (!xsame && ysame))
            GetLengthandSubrIx((opcount = 1), &length, &subrIx);
        else
            GetLengthandSubrIx(opcount, &length, &subrIx);
        GetFlexCoord(j, 0, eltix, &coord);
        coord0.x = coord.x;
        coord0.y = coord.y;
        if (j == 7) {
            if (!xsame && (optype == VMT))
                WriteX(coord.x); /* usually=0. cf bottom of "CheckFlexValues" */
        }
        if (xsame && ysame) {
            WriteCd(coord);
        } else if (xsame) {
            WriteX(coord.x);
            if (optype != HMT) {
                for (ix = 0; ix < masterCount; ix++) {
                    GetFlexCoord(j, ix, eltix, &coord);
                    WriteY((ix == 0 ? coord.y : coord.y - coord0.y));
                }
                WriteSubr(subrIx);
            }
        } else if (ysame) {
            if (optype != VMT) {
                for (ix = 0; ix < masterCount; ix++) {
                    GetFlexCoord(j, ix, eltix, &coord);
                    WriteX((ix == 0 ? coord.x : coord.x - coord0.x));
                }
                WriteSubr(subrIx);
            }
            WriteY(coord.y);
        } else {
            startix = 0;
            if ((writeSubrOnce = (length == opcount))) {
                if (optype == HMT)
                    WriteX(coord.x);
                else if (optype == VMT)
                    WriteY(coord.y);
                else
                    WriteCd(coord);
                length = startix = 1;
            }
            for (opix = 0; opix < opcount; opix += length) {
                for (ix = startix; ix < masterCount; ix++) {
                    GetFlexCoord(j, ix, eltix, &coord);
                    if (ix != 0) {
                        coord.x -= coord0.x;
                        coord.y -= coord0.y;
                    }
                    switch (optype) {
                        case HMT:
                            WriteX(coord.x);
                            break;
                        case VMT:
                            WriteY(coord.y);
                            break;
                        case RMT:
                            MtorDt(coord, opix, length);
                            break;
                    }
                }
                if (!writeSubrOnce || (opix == (opcount - 1)))
                    WriteSubr(subrIx);
            } /* end of for opix */
        }     /* end of last else clause */
        if (j != 7) {
            WriteToBuffer("%s 2 subr\n", GetOperator(optype));
        }
        if (j == 7) {
            if (!ysame && (optype == HMT))
                WriteY(coord.y); /* usually=0. cf bottom of "CheckFlexValues" */
        }
    } /* end of j for loop */
    WriteToBuffer("0 subr\n");
    UnallocateMem(refPtArray);
}

static void
WriteUnmergedHints(indx pathEltIx, indx mIx)
{
    PHintElt hintList;

    /* hintArray contains the pointers to the beginning of the linked list of
     * hints for each design at pathEltIx. */
    hintList = (PHintElt)AllocateMem(1, sizeof(HintElt*),
                                     "hint element array");
    /* Initialize hint list. */
    if (pathEltIx == MAINHINTS)
        hintList = pathlist[mIx].mainhints;
    else
        hintList = pathlist[mIx].path[pathEltIx].hints;

    if (pathEltIx != MAINHINTS)
        WriteToBuffer("beginsubr snc\n");

    while (hintList != NULL) {
        int16_t hinttype = hintList->type;
        hintList->rightortop -=
        hintList->leftorbot; /* relativize */
        if ((hinttype == RY || hinttype == (RM + ESCVAL)) && !cubeLibrary)
        /* If it is a cube library, sidebearings are considered to be
         * zero. for normal fonts, translate vstem hints left by
         * sidebearing. */
            hintList->leftorbot -= IntToFix(pathlist[mIx].sb);

        WriteOneHintVal(hintList->leftorbot);
        WriteOneHintVal(hintList->rightortop);
        switch (hinttype) {
            case RB:
                WriteToBuffer("rb\n");
                break;
            case RV + ESCVAL:
                WriteToBuffer("rv\n");
                break;
            case RY:
                WriteToBuffer("ry\n");
                break;
            case RM + ESCVAL:
                    WriteToBuffer("rm\n");
                break;
            default:
                LogMsg(LOGERROR, NONFATALERROR,
                       "Illegal hint type: %d in character: %s.\n", hinttype,
                       gGlyphName);
        }

        if (hintList->next == NULL)
            hintList = NULL;
        else
            hintList = hintList->next;
    } /* end of while */

    if (pathEltIx != MAINHINTS)
        WriteToBuffer("endsubr enc\nnewcolors\n");

    UnallocateMem(hintList);
}

static void
WriteHints(indx pathEltIx)
{
    indx ix, opix;
    int16_t opcount, subrIx, length;
    PHintElt* hintArray;
    bool writeSubrOnce;

    /* hintArray contains the pointers to the beginning of the linked list of
     hints for
     each design at pathEltIx. */
    hintArray = (PHintElt*)AllocateMem(masterCount, sizeof(HintElt*),
                                       "hint element array");
    /* Initialize hint array. */
    for (ix = 0; ix < masterCount; ix++)
        hintArray[ix] =
          (pathEltIx == MAINHINTS ? pathlist[ix].mainhints
                                  : pathlist[ix].path[pathEltIx].hints);
    if (pathEltIx != MAINHINTS)
        WriteToBuffer("beginsubr snc\n");
    while (hintArray[0] != NULL) {
        bool lbsame, rtsame;
        indx startix = 0;
        int16_t hinttype = hintArray[hintsMasterIx]->type;
        for (ix = 0; ix < masterCount; ix++) {
            hintArray[ix]->rightortop -=
              hintArray[ix]->leftorbot; /* relativize */
            if ((hinttype == RY || hinttype == (RM + ESCVAL)) && !cubeLibrary)
                /* if it is a cube library, sidebearings are considered to be
                 * zero */
                /* for normal fonts, translate vstem hints left by sidebearing
                 */
                hintArray[ix]->leftorbot -= IntToFix(pathlist[ix].sb);
        }
        lbsame = rtsame = true;
        for (ix = 1; ix < masterCount; ix++) {
            if (hintArray[ix]->leftorbot != hintArray[ix - 1]->leftorbot)
                lbsame = false;
            if (hintArray[ix]->rightortop != hintArray[ix - 1]->rightortop)
                rtsame = false;
        }
        if (lbsame && rtsame) {
            WriteOneHintVal(hintArray[0]->leftorbot);
            WriteOneHintVal(hintArray[0]->rightortop);
        } else if (lbsame) {
            WriteOneHintVal(hintArray[0]->leftorbot);
            for (ix = 0; ix < masterCount; ix++)
                WriteOneHintVal((ix == 0 ? hintArray[ix]->rightortop
                                         : hintArray[ix]->rightortop -
                                             hintArray[0]->rightortop));
            GetLengthandSubrIx(1, &length, &subrIx);
            WriteSubr(subrIx);
        } else if (rtsame) {
            for (ix = 0; ix < masterCount; ix++)
                WriteOneHintVal((ix == 0 ? hintArray[ix]->leftorbot
                                         : hintArray[ix]->leftorbot -
                                             hintArray[0]->leftorbot));
            GetLengthandSubrIx(1, &length, &subrIx);
            WriteSubr(subrIx);
            WriteOneHintVal(hintArray[0]->rightortop);
        } else {
            opcount = GetOperandCount(hinttype);
            GetLengthandSubrIx(opcount, &length, &subrIx);
            if ((writeSubrOnce = (length == opcount))) {
                WriteOneHintVal(hintArray[0]->leftorbot);
                WriteOneHintVal(hintArray[0]->rightortop);
                length = startix = 1;
            }
            for (opix = 0; opix < opcount; opix += length) {
                for (ix = startix; ix < masterCount; ix++) {
                    if (opix == 0)
                        WriteOneHintVal((ix == 0 ? hintArray[ix]->leftorbot
                                                 : hintArray[ix]->leftorbot -
                                                     hintArray[0]->leftorbot));
                    else
                        WriteOneHintVal((ix == 0 ? hintArray[ix]->rightortop
                                                 : hintArray[ix]->rightortop -
                                                     hintArray[0]->rightortop));
                }
                if (!writeSubrOnce || (opix == (opcount - 1)))
                    WriteSubr(subrIx);
            }
        }
        switch (hinttype) {
            case RB:
                WriteToBuffer("rb\n");
                break;
            case RV + ESCVAL:
                    WriteToBuffer("rv\n");
                break;
            case RY:
                WriteToBuffer("ry\n");
                break;
            case RM + ESCVAL:
                    WriteToBuffer("rm\n");
                break;
            default:
                LogMsg(LOGERROR, NONFATALERROR,
                       "Illegal hint type: %d in character: %s.\n", hinttype,
                       gGlyphName);
        }
        for (ix = 0; ix < masterCount; ix++)
            hintArray[ix] =
              (hintArray[ix]->next == NULL) ? NULL : hintArray[ix]->next;
    } /* end of while */
    if (pathEltIx != MAINHINTS)
        WriteToBuffer("endsubr enc\nnewcolors\n");
    UnallocateMem(hintArray);
}

static void
WritePathElt(indx mIx, indx eltIx, int16_t pathType, indx startix,
             int16_t length)
{
    Cd c1, c2, c3;
    PCharPathElt path, path0;

    path = &pathlist[mIx].path[eltIx];
    path0 = &pathlist[0].path[eltIx];

    switch (pathType) {
        case HDT:
        case HMT:
            WriteX((mIx == 0 ? path->rx : path->rx - path0->rx));
            break;
        case VDT:
        case VMT:
            WriteY((mIx == 0 ? path->ry : path->ry - path0->ry));
            break;
        case RDT:
        case RMT:
        case DT:
        case MT:
            if (pathType == DT || pathType == MT) {
                c1.x = path->x;
                c1.y = path->y;
            } else {
                c1.x = (mIx == 0 ? path->rx : path->rx - path0->rx);
                c1.y = (mIx == 0 ? path->ry : path->ry - path0->ry);
            }
            MtorDt(c1, startix, length);
            break;
        case HVCT:
        case VHCT:
        case RCT:
        case CT:
            if (pathType == CT) {
                c1.x = path->x1;
                c1.y = path->y1;
                c2.x = path->x2;
                c2.y = path->y2;
                c3.x = path->x3;
                c3.y = path->y3;
            } else if (mIx == 0) {
                c1.x = path->rx1;
                c1.y = path->ry1;
                c2.x = path->rx2;
                c2.y = path->ry2;
                c3.x = path->rx3;
                c3.y = path->ry3;
            } else {
                c1.x = path->rx1 - path0->rx1;
                c1.y = path->ry1 - path0->ry1;
                c2.x = path->rx2 - path0->rx2;
                c2.y = path->ry2 - path0->ry2;
                c3.x = path->rx3 - path0->rx3;
                c3.y = path->ry3 - path0->ry3;
            }
            if (pathType == RCT || pathType == CT)
                Ct(c1, c2, c3, startix, length);
            else if (pathType == HVCT)
                Hvct(c1, c2, c3, startix, length);
            else
                Vhct(c1, c2, c3, startix, length);
            break;
        case CP:
            break;
        default: {
            LogMsg(LOGERROR, NONFATALERROR,
                   "Illegal path operator %d found in character: %s.\n",
                   (int)pathType, gGlyphName);
        }
    }
}

static void
OptimizeMtorDt(indx eltix, int16_t* op, bool* xequal, bool* yequal)
{
    indx ix;

    *xequal = *yequal = true;
    for (ix = 1; ix < masterCount; ix++) {
        *xequal = *xequal && (pathlist[ix].path[eltix].rx ==
                              pathlist[ix - 1].path[eltix].rx);
        *yequal = *yequal && (pathlist[ix].path[eltix].ry ==
                              pathlist[ix - 1].path[eltix].ry);
    }
    if (*xequal && pathlist[0].path[eltix].rx == 0) {
        *op = (*op == RMT) ? VMT : VDT;
        *xequal = false;
    } else if (*yequal && pathlist[0].path[eltix].ry == 0) {
        *op = (*op == RMT) ? HMT : HDT;
        *yequal = false;
    }
}

static bool
CoordsEqual(indx dir1, indx dir2, indx opIx, indx eltIx, int16_t op)
{
    PCharPathElt path1 = &pathlist[dir1].path[eltIx],
                 path2 = &pathlist[dir2].path[eltIx];

    switch (opIx) {
        case 0:
            if (op == RCT || op == HVCT)
                return (path1->rx1 == path2->rx1);
            else /* op == VHCT */
                return (path1->ry1 == path2->ry1);
        case 1:
            if (op == RCT)
                return (path1->ry1 == path2->ry1);
            else
                return (path1->rx2 == path2->rx2);
        case 2:
            if (op == RCT)
                return (path1->rx2 == path2->rx2);
            else
                return (path1->ry2 == path2->ry2);
        case 3:
            if (op == RCT)
                return (path1->ry2 == path2->ry2);
            else if (op == HVCT)
                return (path1->ry3 == path2->ry3);
            else /* op == VHCT */
                return (path1->rx3 == path2->rx3);
        case 4:
            return (path1->rx3 == path2->rx3);
        case 5:
            return (path1->ry3 == path2->ry3);
        default:
            LogMsg(LOGERROR, NONFATALERROR,
                   "Invalid index value: %d defined for curveto "
                   "command4 in character: %s. Op=%d, dir=%s near "
                   "(%d %d).\n",
                   (int)opIx, gGlyphName, (int)op, masterNames[dir1], FTrunc8(path1->x),
                   FTrunc8(path1->y));
            break;
    }

    return 0;
}

/* Checks if path element values are the same for the RCT, HVCT and VHCT
 operators in each master master between operands startIx to
 startIx + length.  Returns true if they are the same and false otherwise. */
static bool
SamePathValues(indx eltIx, int16_t op, indx startIx, int16_t length)
{
    indx ix, mIx;
    /*  PCharPathElt path0 = &pathlist[0].path[eltIx]; */
    bool same = true;

    for (ix = 0; ix < length; ix++) {
        for (mIx = 1; mIx < masterCount; mIx++)
            if (!(same = same && CoordsEqual(mIx, 0, startIx, eltIx, op)))
                return false;
        startIx++;
    }
    return true;
}

/* Takes multiple path descriptions for the same character name and
 combines them into a single path description using new subroutine
 calls 7 - 11. */
static void
WritePaths(char** outBuffers, size_t* outLengths)
{
    indx ix, eltix, opix, startIx, mIx;
    int16_t length, subrIx, opcount, op;
    bool xequal, yequal;

#if DONT_COMBINE_PATHS
    for (mIx = 0; mIx < masterCount; mIx++) {
        PathList path = pathlist[mIx];

        byteCount = 0;
        buffSize = outLengths[mIx];
        outbuff = outBuffers[mIx];

        WriteToBuffer("%% %s\n", gGlyphName);

        if (gAddHints && (pathlist[hintsMasterIx].mainhints != NULL))
            WriteUnmergedHints(MAINHINTS, mIx);

        WriteToBuffer("sc\n");
        for (eltix = 0; eltix < gPathEntries; eltix++) {
            CharPathElt elt = path.path[eltix];
            op = elt.type;

            /* Use non-relative operators for easy comparison with input,
             * I don’t think it is really required. */
            if (op == RMT)
                op = MT;
            else if (op == RCT)
                op = CT;
            else if (op == RDT)
                op = DT;

            if (gAddHints && elt.hints != NULL)
                WriteUnmergedHints(eltix, mIx);

            opcount = GetOperandCount(op);
            GetLengthandSubrIx(opcount, &length, &subrIx);

            WritePathElt(mIx, eltix, op, 0, opcount);

            WriteToBuffer(GetOperator(op));
            WriteToBuffer("\n");
        }
        WriteToBuffer("ed\n");

        outLengths[mIx] = byteCount;
        outBuffers[mIx] = outbuff;
    }
    return;
#endif /* DONT_COMBINE_PATHS */

    /* The code below is not used, but were are not ifdef'ing it and the code
     * it calls so it keep compiling and does not bitrot. */

    WriteToBuffer("%% %s\n", gGlyphName);

    if (!cubeLibrary)
        WriteSbandWidth();
    if (gAddHints && (pathlist[hintsMasterIx].mainhints != NULL))
        WriteHints(MAINHINTS);
    WriteToBuffer("sc\n");
    firstMT = true;
    for (eltix = 0; eltix < gPathEntries; eltix++) {
        /* A RDT may be tagged 'remove' because it is followed by a point CP. */
        /* See AddLineCube(). */
        if (pathlist[0].path[eltix].remove)
            continue;

        xequal = yequal = false;
        if (gAddHints && (pathlist[hintsMasterIx].path[eltix].hints != NULL))
            WriteHints(eltix);
        switch (pathlist[0].path[eltix].type) {
            case RMT:
                if (firstMT &&
                    !cubeLibrary) /* translate by sidebearing value */
                                  /* don't want this for cube */
                    for (ix = 0; ix < masterCount; ix++)
                        pathlist[ix].path[eltix].rx -=
                          IntToFix(pathlist[ix].sb);
                firstMT = false;
            case RDT:
            case CP:
                break;
            case RCT:
                if (CheckFlexOK(eltix)) {
                    WriteFlex(eltix);
                    /* Since we know the next element is a flexed curve and
                     has been written out we skip it. */
                    eltix++;
                    continue;
                }
                /* Try to use optimized operators. */
                if ((pathlist[0].path[eltix].rx1 == 0 &&
                     pathlist[0].path[eltix].ry3 == 0) ||
                    (pathlist[0].path[eltix].ry1 == 0 &&
                     pathlist[0].path[eltix].rx3 == 0))
                    OptimizeCT(eltix);
                break;
            default:
                LogMsg(LOGERROR, NONFATALERROR,
                       "Unknown operator in character: %s.\n", gGlyphName);
        }
        op = pathlist[0].path[eltix].type;
        if (op != RCT && op != HVCT && op != VHCT && op != CP)
            /* Try to use optimized operators. */
            OptimizeMtorDt(eltix, &op, &xequal, &yequal);
        startIx = 0;
        opcount = GetOperandCount(op);
        GetLengthandSubrIx(opcount, &length, &subrIx);
        if (xequal && yequal) {
            WritePathElt(0, eltix, op, 0, opcount);
        } else if (xequal) {
            WriteX(pathlist[0].path[eltix].rx);
            if (op != HMT && op != HDT) {
                for (ix = 0; ix < masterCount; ix++)
                    WriteY(((ix == 0) ? pathlist[ix].path[eltix].ry
                                      : pathlist[ix].path[eltix].ry -
                                          pathlist[0].path[eltix].ry));
                GetLengthandSubrIx(1, &length, &subrIx);
                WriteSubr(subrIx);
            }
        } else if (yequal) {
            if (op != VMT && op != VDT) {
                for (ix = 0; ix < masterCount; ix++)
                    WriteX(((ix == 0) ? pathlist[ix].path[eltix].rx
                                      : pathlist[ix].path[eltix].rx -
                                          pathlist[0].path[eltix].rx));
                GetLengthandSubrIx(1, &length, &subrIx);
                WriteSubr(subrIx);
            }
            WriteY(pathlist[0].path[eltix].ry);
        } else
            for (opix = 0; opix < opcount; opix += length) {
                WritePathElt(0, eltix, op, opix, length);
                startIx = opix;
                if (op == RCT || op == HVCT || op == VHCT)
                    if (SamePathValues(eltix, op, startIx, length))
                        continue;
                for (ix = 0; ix < length; ix++) {
                    for (mIx = 1; mIx < masterCount; mIx++)
                        WritePathElt(mIx, eltix, op, startIx, 1);
                    startIx++;
                }
                if (subrIx >= 0 && op != CP)
                    WriteSubr(subrIx);
            } /* end of for opix */
        WriteStr(GetOperator(op));
        WriteToBuffer("\n");
    } /* end of for eltix */
    WriteToBuffer("ed\n");
}

/* Returns number of operands for the given operator. */
static int16_t
GetOperandCount(int16_t op)
{
    int16_t count = 0;

    if (op < ESCVAL)
        switch (op) {
            case CP:
            case HDT:
            case HMT:
            case VDT:
            case VMT:
                count = 1;
                break;
            case RMT:
            case MT:
            case RDT:
            case DT:
            case RB:
            case RY:
            case SBX:
                count = 2;
                break;
            case HVCT:
            case VHCT:
                count = 4;
                break;
            case RCT:
            case CT:
                count = 6;
                break;
            default:
                LogMsg(LOGERROR, NONFATALERROR,
                       "Unknown operator in character: %s.\n", gGlyphName);
                break;
        }
    else /* handle escape operators */
        switch (op - ESCVAL) {
            case RM:
            case RV:
                count = 2;
                break;
        }
    return count;
}

/* Returns the subr number to use for a given operator in subrIx and
 checks that the argument length of each subr call does not
 exceed the font interpreter stack limit. */
static void
GetLengthandSubrIx(int16_t opcount, int16_t* length, int16_t* subrIx)
{

    if (((opcount * masterCount) > FONTSTKLIMIT) && opcount != 1)
        if ((opcount / 2 * masterCount) > FONTSTKLIMIT)
            if ((2 * masterCount) > FONTSTKLIMIT)
                *length = 1;
            else
                *length = 2;
        else
            *length = opcount / 2;
    else
        *length = opcount;
    if (((*length) * masterCount) > FONTSTKLIMIT) {
        LogMsg(LOGERROR, NONFATALERROR,
               "Font stack limit exceeded for character: %s.\n", gGlyphName);
    }
    if (!cubeLibrary) {
        switch (*length) {
            case 1:
                *subrIx = 7;
                break;
            case 2:
                *subrIx = 8;
                break;
            case 3:
                *subrIx = 9;
                break;
            case 4:
                *subrIx = 10;
                break;
            case 6:
                *subrIx = 11;
                break;
            default:
                LogMsg(LOGERROR, NONFATALERROR,
                       "Illegal operand length for character: %s.\n",
                       gGlyphName);
                break;
        }
    } else { /* CUBE */
        switch (masterCount) {
            case 2:
                switch (*length) {
                    case 1:
                        *subrIx = 7;
                        break;
                    case 2:
                        *subrIx = 8;
                        break;
                    case 3:
                        *subrIx = 9;
                        break;
                    case 4:
                        *subrIx = 10;
                        break;
                    case 6:
                        *subrIx = 11;
                        break;
                    default:
                        LogMsg(LOGERROR, NONFATALERROR,
                               "Illegal operand length for character: %s.\n",
                               gGlyphName);
                        break;
                }
                break;

            case 4:
                switch (*length) {
                    case 1:
                        *subrIx = 12;
                        break;
                    case 2:
                        *subrIx = 13;
                        break;
                    case 3:
                        *subrIx = 14;
                        break;
                    case 4:
                        *subrIx = 15;
                        break;
                    default:
                        LogMsg(LOGERROR, NONFATALERROR,
                               "Illegal operand length for character: %s.\n",
                               gGlyphName);
                        break;
                }
                break;

            case 8:
                switch (*length) {
                    case 1:
                        *subrIx = 16;
                        break;
                    case 2:
                        *subrIx = 17;
                        break;
                    default:
                        LogMsg(LOGERROR, NONFATALERROR,
                               "Illegal operand length for character: %s.\n",
                               gGlyphName);
                        break;
                }
                break;

            case 16:
                switch (*length) {
                    case 1:
                        *subrIx = 18;
                        break;
                    default:
                        LogMsg(LOGERROR, NONFATALERROR,
                               "Illegal operand length for character: %s.\n",
                               gGlyphName);
                        break;
                }
                break;

            default:
                LogMsg(LOGERROR, NONFATALERROR, "Illegal masterCount.\n");
                break;
        }
    }
}

/**********
 Normal MM fonts have their dimensionality wired into the subrs.
 That is, the contents of subr 7-11 are computed on a per-font basis.
 Cube fonts can be of 1-4 dimensions on a per-character basis.
 But there are only a few possible combinations of these numbers
 because we are limited by the stack size:

 dimensions  arguments      values    subr#
 1            1            2        7
 1            2            4        8
 1            3            6        9
 1            4            8       10
 1            6           12       11
 2            1            4       12
 2            2            8       13
 2            3           12       14
 2            4           16       15
 3            1            8       16
 3            2           16       17
 4            1           16       18

 *************/

bool
MergeCharPaths(const ACFontInfo* fontinfo, const char** srcglyphs, int nmasters,
               const char** masters, char** outbuffers, size_t* outlengths)
{
    bool ok;
    /* This requires that  master  hintsMasterIx has already been hinted with
     * AutoColor().  See comments in psautohint,c::AutoColorStringMM() */
    masterCount = nmasters;
    masterNames = masters;

    ok = CompareCharPaths(fontinfo, srcglyphs);
    if (ok) {
        CheckForZeroLengthCP();
        SetSbandWidth();
        if (gAddHints && hintsMasterIx >= 0 && gPathEntries > 0) {
            if (!ReadandAssignHints()) {
                LogMsg(LOGERROR, FATALERROR,
                       "Path problem in ReadAndAssignHints, character %s.\n",
                       gGlyphName);
            }
            CheckHandVStem3();
        }
        WritePaths(outbuffers, outlengths);
    }
    FreePathElements(0, masterCount);

    return ok;
}
