/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"
#include "bbox.h"
#include "charpath.h"
#include "opcodes.h"
#include "optable.h"

#define DONT_COMBINE_PATHS 1

#define DMIN 50       /* device minimum (one-half of a device pixel) */
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

static bool firstMT;
static Cd* refPtArray = NULL;
static ACBuffer* outbuff;
static int masterCount;
static const char** masterNames;
static PathList* pathlist = NULL;
static indx hintsMasterIx = 0; /* The index of the master we read hints from */

/* Prototypes */
static void GetRelativePosition(Fixed, Fixed, Fixed, Fixed, Fixed, Fixed*);
static int16_t GetOperandCount(int16_t);
static void GetLengthandSubrIx(int16_t, int16_t*, int16_t*);

/* macros */
#define WriteToBuffer(...) ACBufferWriteF(outbuff, __VA_ARGS__)
#define WRTNUM(i) WriteToBuffer("%d ", (int)(i))
#define WRTNUMA(i) WriteToBuffer("%0.2f ", round((double)(i)*100) / 100)
#define WriteSubr(val) WriteToBuffer("%d subr ", val)

static void
WriteX(Fixed x)
{
    Fixed i = FRnd(x);
    WRTNUM(FTrunc(i));
}

static void
WriteY(Fixed y)
{
    Fixed i = FRnd(y);
    WRTNUM(FTrunc(i));
}

#define WriteCd(c)                                                             \
    {                                                                          \
        WriteX(c.x);                                                           \
        WriteY(c.y);                                                           \
    }

static void
WriteOneHintVal(Fixed val)
{
    if (FracPart(val) == 0)
        WRTNUM(FTrunc(val));
    else
        WRTNUMA(FIXED2FLOAT(val));
}

/* Locates the first CP following the given path element. */
static int32_t
GetCPIx(indx mIx, int32_t pathIx)
{
    indx ix;

    for (ix = pathIx; ix < gPathEntries; ix++)
        if (pathlist[mIx].path[ix].type == CP)
            return ix;
    LogMsg(LOGERROR, NONFATALERROR, "No closepath.");
    return (-1);
}

static void
GetEndPoint1(indx mIx, int32_t pathIx, Fixed* ptX, Fixed* ptY)
{
    GlyphPathElt* pathElt = &pathlist[mIx].path[pathIx];

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
            LogMsg(LOGERROR, NONFATALERROR, "Bad description.");
            break;
        default:
            LogMsg(LOGERROR, NONFATALERROR, "Illegal operator.");
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
            (*coord).x = FTrunc(FRnd(pathlist[mIx].path[eltno].x));
            (*coord).y = FTrunc(FRnd(pathlist[mIx].path[eltno].y));
            break;
        case RCT:
            (*coord).x = FTrunc(FRnd(pathlist[mIx].path[eltno].x3));
            (*coord).y = FTrunc(FRnd(pathlist[mIx].path[eltno].y3));
            break;
        case CP:
            GetCoordFromType(pathlist[mIx].path[eltno - 1].type, coord, mIx,
                             eltno - 1);
            break;
        default:
            LogMsg(LOGERROR, NONFATALERROR, "Unrecognized path type");
            memset(coord, 0, sizeof(Cd));
            break;
    }
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
            LogMsg(LOGERROR, NONFATALERROR, "Illegal path type: %d.", pathtype);
            return NULL;
    }
}

static void
FreeHints(HintElt* hints)
{
    HintElt* next;

    while (hints != NULL) {
        next = hints->next;
        UnallocateMem(hints);
        hints = next;
    }
}

static void
FreePathElements(indx stopix)
{
    indx i, j;

    for (j = 0; j < stopix; j++) {
        if (pathlist[j].path != NULL) {
            /* Before we can free hint elements will need to know gPathEntries
             value for char in each master because this proc can be
             called when glyphs are inconsistent.
             */
            for (i = 0; i < gPathEntries; i++)
                FreeHints(pathlist[j].path[i].hints);
        }
        FreeHints(pathlist[j].mainhints);
        UnallocateMem(pathlist[j].path);
    }
    UnallocateMem(pathlist);
    pathlist = NULL;
}

static void
InconsistentPointCount(indx ix, int entries1, int entries2)
{
    LogMsg(WARNING, OK,
           "Glyph will not be included in the font "
           "because the version in %s has a total of %d elements "
           "and the one in %s has %d elements.",
           masterNames[0], entries1, masterNames[ix], entries2);
}

static void
InconsistentPathType(indx ix, int16_t type1, int16_t type2, indx eltno)
{
    Cd coord1, coord2;

    GetCoordFromType(type1, &coord1, 0, eltno);
    GetCoordFromType(type2, &coord2, ix, eltno);
    LogMsg(WARNING, OK,
           "Glyph will not be included in the font "
           "because the version in %s has path type %s at coord: %d "
           "%d and the one in %s has type %s at coord %d %d.",
           masterNames[0], GetPathType(type1), coord1.x, coord1.y,
           masterNames[ix], GetPathType(type2), coord2.x, coord2.y);
}

/* Returns whether changing the line to a curve is successful. */
static bool
ChangetoCurve(indx mIx, indx pathIx)
{
    Cd start = { 0, 0 }, end = { 0, 0 }, ctl1, ctl2;
    GlyphPathElt* pathElt = &pathlist[mIx].path[pathIx];

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

/* Checks that glyph paths for multiple masters have the same
 number of points and in the same path order.  If this isn't the
 case the glyph is not included in the font. */
static bool
CompareGlyphPaths(const char** glyphs)
{
    indx mIx, ix, i;
    int32_t totalPathElt, minPathLen;
    bool ok = true;
    int16_t type1, type2;

    totalPathElt = minPathLen = MAXINT;
    if (pathlist == NULL) {
        pathlist = (PathList*)AllocateMem(masterCount, sizeof(PathList),
                                          "glyph path list");
    }

    for (mIx = 0; mIx < masterCount; mIx++) {
        ResetMaxPathEntries();
        SetCurrPathList(&pathlist[mIx]);
        gPathEntries = 0;

        if (hintsMasterIx == mIx) {
            /* read char data and hints from bez file */
            if (!ReadGlyph(glyphs[mIx], true, gAddHints))
                return false;
        } else {
            /* read char data only */
            if (!ReadGlyph(glyphs[mIx], true, false))
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
            WriteToBuffer("%d ", (j == 0)
                                   ? pathlist[j].width
                                   : pathlist[j].width - pathlist[0].width);
        }
        GetLengthandSubrIx(1, &length, &subrix);
        WriteSubr(subrix);
    } else if (wsame) {
        for (j = 0; j < masterCount; j++) {
            WriteToBuffer("%d ", (j == 0) ? pathlist[j].sb
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
                WriteToBuffer(
                  "%d ", (ix == 0) ? (j == 0) ? pathlist[j].sb
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
    GlyphPathElt pathElt;

    *value = FixInt(10000);
    pathElt = pathlist[mIx].path[pathIx];
    GetEndPoints1(mIx, pathIx, &startPt, &endPt);
    switch (hinttype) {
        case RB:
        case RV + ESCVAL:
            minval = -NUMMIN(startPt.y, endPt.y);
            maxval = -NUMMAX(startPt.y, endPt.y);
            p1 = -pathElt.y1;
            p2 = -pathElt.y2;
            minbx = &lly;
            maxbx = &ury;
            break;
        case RY:
        case RM + ESCVAL:
            minval = NUMMIN(startPt.x, endPt.x);
            maxval = NUMMAX(startPt.x, endPt.x);
            p1 = pathElt.x1;
            p2 = pathElt.x2;
            minbx = &llx;
            maxbx = &urx;
            break;
        default:
            LogMsg(LOGERROR, NONFATALERROR, "Illegal hint type.");
            return false;
    }
    if (p1 - maxval >= FixOne || p2 - maxval >= FixOne ||
        p1 - minval <= FixOne || p2 - minval <= FixOne) {
        /* Transform coordinates so I get the same value that AC would give. */
        FindCurveBBox(startPt.x, -startPt.y, pathElt.x1, -pathElt.y1,
                      pathElt.x2, -pathElt.y2, endPt.x, -endPt.y, &llx, &lly,
                      &urx, &ury);
        if (*maxbx > maxval || minval > *minbx) {
            if (minval - *minbx > *maxbx - maxval)
                *value = (hinttype == RB || hinttype == RV + ESCVAL) ? -*minbx
                                                                     : *minbx;
            else
                *value = (hinttype == RB || hinttype == RV + ESCVAL) ? -*maxbx
                                                                     : *maxbx;
            return true;
        }
    }
    return false;
}

static bool
nearlyequal_(Fixed a, Fixed b, Fixed tolerance)
{
    return (abs(a - b) <= tolerance);
}

/* Returns whether the hint values are derived from the start,
 average, end or flattened curve with an inflection point of
 the specified path element. Since path element numbers in
 glyph files start from one and the path array starts
 from zero we need to subtract one from the path index. */
static int16_t
GetPointType(int16_t hinttype, Fixed value, int32_t* pathEltIx)
{
    Cd startPt, endPt;
    Fixed startval = 0, endval = 0, loc;
    int16_t pathtype;
    bool tryAgain = true;
    int32_t pathIx = *pathEltIx - 1;

retry:
    GetEndPoints1(hintsMasterIx, pathIx, &startPt, &endPt);
    switch (hinttype) {
        case RB:
        case RV + ESCVAL:
            startval = startPt.y;
            endval = endPt.y;
            break;
        case RY:
        case RM + ESCVAL:
            startval = startPt.x;
            endval = endPt.x;
            break;
        default:
            LogMsg(LOGERROR, NONFATALERROR, "Illegal hint type.");
    }

    /* Check for exactly equal first, in case endval = startval + 1.
     * Certain cases are still ambiguous. */

    if (value == startval)
        return STARTPT;
    else if (value == endval)
        return ENDPT;
    else if (nearlyequal_(value, startval, FixOne))
        return STARTPT;
    else if (nearlyequal_(value, endval, FixOne))
        return ENDPT;
    else if (value == (loc = FixHalfMul(startval + endval)) ||
             nearlyequal_(value, loc, FixOne))
        return AVERAGE;

    pathtype = pathlist[hintsMasterIx].path[pathIx].type;
    /* try looking at other end of line or curve */
    if (tryAgain && (pathIx + 1 < gPathEntries) && (pathtype != CP)) {
        pathIx++;
        *pathEltIx += 1;
        tryAgain = false;
        goto retry;
    }

    /* reset pathEltIx to original value */
    if (!tryAgain)
        *pathEltIx -= 1;

    if (CurveBBox(hintsMasterIx, hinttype, *pathEltIx - 1, &loc) &&
        nearlyequal_(value, loc, FixOne))
        return CURVEBBOX;

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

/* For each base design, excluding hints master, include the
 hint information at the specified path element. type1
 and type2 indicates whether to use the start, end, avg.,
 curvebbox or flattened curve.  If a curve is to be flattened
 check if this is an "s" curve and use the inflection point.  If not
 then use the same relative position between the two endpoints as
 in the main hints master.
 hinttype is either RB, RY, RM or RV. pathEltIx
 is the index into the path array where the new hint should
 be stored.  pathIx is the index of the path segment used to
 calculate this particular hint. */
static void
InsertHint(HintElt* currHintElt, indx pathEltIx, int16_t type1, int16_t type2)
{
    indx ix, j;
    Cd startPt, endPt;
    HintElt **hintElt, *newEntry;
    GlyphPathElt pathElt;
    int32_t pathIx;
    int16_t pathtype, hinttype = currHintElt->type;
    Fixed *value, ghostVal = 0, tempVal;

    if (type1 == GHOST || type2 == GHOST)
        /* ghostVal should be -20 or -21 */
        ghostVal = currHintElt->rightortop - currHintElt->leftorbot;
    for (ix = 0; ix < masterCount; ix++) {
        if (ix == hintsMasterIx)
            continue;
        newEntry = (HintElt*)AllocateMem(1, sizeof(HintElt), "hint element");
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
                               "Malformed path list in master: %s, "
                               "element: %d, type: %s != curveto.",
                               masterNames[ix], pathIx,
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
                    LogMsg(LOGERROR, NONFATALERROR, "Illegal point type.");
            }
            /* Assign correct value for bottom band if first path element
             is a ghost band. */
            if (j == 1 && type1 == GHOST)
                newEntry->leftorbot = newEntry->rightortop - ghostVal;
        }
    }
}

static void
ReadHints(HintElt* hintElt, indx pathEltIx)
{
    HintElt* currElt = hintElt;
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

    /* Check for main hints first, i.e. global to glyph. */
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
DoubleCheckFlexVals(indx masternum, indx eltix, indx hintmasternum)
{
    bool vert = (pathlist[hintmasternum].path[eltix].x ==
                 pathlist[hintmasternum].path[eltix + 1].x3);
    if (vert) {
        return (pathlist[masternum].path[eltix].x ==
                pathlist[masternum].path[eltix + 1].x3);
    } else {
        return (pathlist[masternum].path[eltix].y ==
                pathlist[masternum].path[eltix + 1].y3);
    }
}

static bool
CheckFlexOK(indx ix)
{
    indx i;
    bool flexOK = pathlist[hintsMasterIx].path[ix].isFlex;
    GlyphPathElt* end;

    for (i = 0; i < masterCount; i++) {
        if (i == hintsMasterIx)
            continue;
        if (flexOK && (!pathlist[i].path[ix].isFlex)) {
            if (!DoubleCheckFlexVals(i, ix, hintsMasterIx)) {
                end = &pathlist[i].path[ix];
                LogMsg(WARNING, OK,
                       "Flex will not be included in the glyph, "
                       "in '%s' at element %d near (%d, %d) because "
                       "the glyph does not have flex in each "
                       "design.",
                       masterNames[i], (int)ix, FTrunc(end->x), FTrunc(end->y));
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
                       "command1.",
                       (int)ix);
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
                       "command2.",
                       (int)ix);
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
                       "command3.",
                       (int)ix);
                break;
        }
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
    HintElt* hintList;

    /* hintArray contains the pointers to the beginning of the linked list of
     * hints for each design at pathEltIx. */
    /* Initialize hint list. */
    if (pathEltIx == MAINHINTS)
        hintList = pathlist[mIx].mainhints;
    else
        hintList = pathlist[mIx].path[pathEltIx].hints;

    if (pathEltIx != MAINHINTS)
        WriteToBuffer("beginsubr snc\n");

    while (hintList != NULL) {
        int16_t hinttype = hintList->type;
        hintList->rightortop -= hintList->leftorbot; /* relativize */
        if ((hinttype == RY || hinttype == (RM + ESCVAL)))
            /* If it is a cube library, sidebearings are considered to be
             * zero. for normal fonts, translate vstem hints left by
             * sidebearing. */
            hintList->leftorbot -= FixInt(pathlist[mIx].sb);

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
                LogMsg(LOGERROR, NONFATALERROR, "Illegal hint type: %d",
                       hinttype);
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

#define BREAK_ON_NULL_HINTARRAY_IX if(hintArray[ix] == NULL) break

static void
WriteHints(indx pathEltIx)
{
    indx ix, opix;
    int16_t opcount, subrIx, length;
    HintElt** hintArray;
    bool writeSubrOnce;

    /* hintArray contains the pointers to the beginning of the linked list of
     hints for
     each design at pathEltIx. */
    hintArray = (HintElt**)AllocateMem(masterCount, sizeof(HintElt*),
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
            BREAK_ON_NULL_HINTARRAY_IX;
            hintArray[ix]->rightortop -=
              hintArray[ix]->leftorbot; /* relativize */
            if ((hinttype == RY || hinttype == (RM + ESCVAL)))
                /* if it is a cube library, sidebearings are considered to be
                 * zero */
                /* for normal fonts, translate vstem hints left by sidebearing
                 */
                hintArray[ix]->leftorbot -= FixInt(pathlist[ix].sb);
        }
        lbsame = rtsame = true;
        for (ix = 1; ix < masterCount; ix++) {
            BREAK_ON_NULL_HINTARRAY_IX;
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
            for (ix = 0; ix < masterCount; ix++) {
                BREAK_ON_NULL_HINTARRAY_IX;
                WriteOneHintVal((ix == 0 ? hintArray[ix]->rightortop
                                         : hintArray[ix]->rightortop -
                                             hintArray[0]->rightortop));
            }
            GetLengthandSubrIx(1, &length, &subrIx);
            WriteSubr(subrIx);
        } else if (rtsame) {
            for (ix = 0; ix < masterCount; ix++) {
                BREAK_ON_NULL_HINTARRAY_IX;
                WriteOneHintVal((ix == 0 ? hintArray[ix]->leftorbot
                                         : hintArray[ix]->leftorbot -
                                             hintArray[0]->leftorbot));
            }
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
                    BREAK_ON_NULL_HINTARRAY_IX;
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
                LogMsg(LOGERROR, NONFATALERROR, "Illegal hint type: %d.",
                       hinttype);
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
    GlyphPathElt *path, *path0;

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
            LogMsg(LOGERROR, NONFATALERROR, "Illegal path operator %d found.",
                   pathType);
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
CoordsEqual(indx master1, indx master2, indx opIx, indx eltIx, int16_t op)
{
    GlyphPathElt *path1 = &pathlist[master1].path[eltIx],
                 *path2 = &pathlist[master2].path[eltIx];

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
                   "command4. Op=%d, master=%s near "
                   "(%d %d).",
                   (int)opIx, (int)op, masterNames[master1], FTrunc(path1->x),
                   FTrunc(path1->y));
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
    /*  GlyphPathElt* path0 = &pathlist[0].path[eltIx]; */
    bool same = true;

    for (ix = 0; ix < length; ix++) {
        for (mIx = 1; mIx < masterCount; mIx++)
            if (!(same = same && CoordsEqual(mIx, 0, startIx, eltIx, op)))
                return false;
        startIx++;
    }
    return true;
}

/* Takes multiple path descriptions for the same glyph name and
 combines them into a single path description using new subroutine
 calls 7 - 11. */
static void
WritePaths(ACBuffer** outBuffers)
{
    indx ix, eltix, opix, startIx, mIx;
    int16_t length, subrIx, opcount, op;
    bool xequal, yequal;

#if DONT_COMBINE_PATHS
    for (mIx = 0; mIx < masterCount; mIx++) {
        PathList path = pathlist[mIx];

        outbuff = outBuffers[mIx];

        WriteToBuffer("%% %s\n", gGlyphName);

        if (gAddHints && (pathlist[hintsMasterIx].mainhints != NULL))
            WriteUnmergedHints(MAINHINTS, mIx);

        WriteToBuffer("sc\n");
        for (eltix = 0; eltix < gPathEntries; eltix++) {
            GlyphPathElt elt = path.path[eltix];
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

            WriteToBuffer("%s\n", GetOperator(op));
        }
        WriteToBuffer("ed\n");
    }
    return;
#endif /* DONT_COMBINE_PATHS */

    /* The code below is not used, but were are not ifdef'ing it and the code
     * it calls so it keep compiling and does not bitrot. */

    WriteToBuffer("%% %s\n", gGlyphName);

    WriteSbandWidth();
    if (gAddHints && (pathlist[hintsMasterIx].mainhints != NULL))
        WriteHints(MAINHINTS);
    WriteToBuffer("sc\n");
    firstMT = true;
    for (eltix = 0; eltix < gPathEntries; eltix++) {
        xequal = yequal = false;
        if (gAddHints && (pathlist[hintsMasterIx].path[eltix].hints != NULL))
            WriteHints(eltix);
        switch (pathlist[0].path[eltix].type) {
            case RMT:
                /* translate by sidebearing value */
                if (firstMT) {
                    for (ix = 0; ix < masterCount; ix++)
                        pathlist[ix].path[eltix].rx -= FixInt(pathlist[ix].sb);
                }
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
                LogMsg(LOGERROR, NONFATALERROR, "Unknown operator.");
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
        WriteToBuffer("%s\n", GetOperator(op));
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
                LogMsg(LOGERROR, NONFATALERROR, "Unknown operator.");
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
        LogMsg(LOGERROR, NONFATALERROR, "Font stack limit exceeded");
    }
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
            LogMsg(LOGERROR, NONFATALERROR, "Illegal operand length.");
            break;
    }
}

/**********
 Normal MM fonts have their dimensionality wired into the subrs.
 That is, the contents of subr 7-11 are computed on a per-font basis.
 Cube fonts can be of 1-4 dimensions on a per-glyph basis.
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
MergeGlyphPaths(const char** srcglyphs, int nmasters, const char** masters,
                ACBuffer** outbuffers)
{
    bool ok;
    /* This requires that  master  hintsMasterIx has already been hinted with
     * AutoHint().  See comments in psautohint,c::AutoHintStringMM() */
    masterCount = nmasters;
    masterNames = masters;

    ok = CompareGlyphPaths(srcglyphs);
    if (ok) {
        SetSbandWidth();
        if (gAddHints && hintsMasterIx >= 0 && gPathEntries > 0) {
            if (!ReadandAssignHints()) {
                LogMsg(LOGERROR, FATALERROR,
                       "Path problem in ReadAndAssignHints");
            }
        }
        WritePaths(outbuffers);
    }
    FreePathElements(masterCount);

    return ok;
}
