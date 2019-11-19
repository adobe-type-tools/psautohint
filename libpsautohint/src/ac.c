/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"
#include "fontinfo.h"

#define MAXSTEMDIST 150 /* initial maximum stem width allowed for hints */

PathElt *gPathStart, *gPathEnd;
bool gUseV, gUseH, gAutoLinearCurveFix, gEditGlyph;
bool gHasFlex, gFlexOK, gFlexStrict, gBandError;
Fixed gHBigDist, gVBigDist, gInitBigDist, gMinDist, gGhostWidth, gGhostLength,
  gBendLength, gBandMargin, gMaxFlare, gMaxBendMerge, gMaxMerge,
  gMinHintElementLength, gFlexCand;
Fixed gPruneA, gPruneB, gPruneC, gPruneD, gPruneValue, gBonus;
float gTheta, gHBigDistR, gVBigDistR, gMaxVal, gMinVal;
int32_t gLenTopBands, gLenBotBands, gNumSerifs, gDMin, gDelta, gCPpercent;
int32_t gBendTan, gSCurveTan;
HintVal *gVHinting, *gHHinting, *gVPrimary, *gHPrimary, *gValList;
HintSeg* gSegLists[4];
Fixed gVStems[MAXSTEMS], gHStems[MAXSTEMS];
int32_t gNumVStems, gNumHStems;
Fixed gTopBands[MAXBLUES], gBotBands[MAXBLUES], gSerifs[MAXSERIFS];
HintPoint *gPointList, **gPtLstArray;
int32_t gPtLstIndex, gNumPtLsts, gMaxPtLsts;
bool gWriteHintedBez = true;
Fixed gBlueFuzz;
bool gDoAligns = false, gDoStems = false;
bool gRoundToInt;
static int maxStemDist = MAXSTEMDIST;

/* if false, then stems defined by curves are excluded from the reporting */
unsigned int gAllStems = false;
AC_REPORTSTEMPTR gAddHStemCB = NULL;
AC_REPORTSTEMPTR gAddVStemCB = NULL;
AC_REPORTZONEPTR gAddGlyphExtremesCB = NULL;
AC_REPORTZONEPTR gAddStemExtremesCB = NULL;
AC_RETRYPTR gReportRetryCB = NULL;
void* gAddStemUserData = NULL;
void* gAddExtremesUserData = NULL;
void* gReportRetryUserData = NULL;

#define VMSIZE (1000000)
static unsigned char *vmfree, *vmlast, vm[VMSIZE];

/* sub allocator */
void*
Alloc(int32_t sz)
{
    unsigned char* s;
    sz = (sz + 3) & ~3; /* make size a multiple of 4 */
    s = vmfree;
    vmfree += sz;
    if (vmfree > vmlast) /* Error! need to make VMSIZE bigger */
    {
        LogMsg(LOGERROR, FATALERROR, "Exceeded VM size for hints.");
    }
    return s;
}

void
InitData(int32_t reason)
{
    float tmp;

    gGlyphName[0] = '\0';

    switch (reason) {
        case STARTUP:
            gDMin = 50;
            gDelta = 0;
            gInitBigDist = PSDist(maxStemDist);
            /* must be <= 168 for ITC Garamond Book Italic p, q, thorn */
            gMinDist = PSDist(7);
            gGhostWidth = PSDist(20);
            gGhostLength = PSDist(4);
            gBendLength = PSDist(2);
            gBendTan = 577;      /* 30 sin 30 cos div abs == .57735 */
            gTheta = (float).38; /* must be <= .38 for Ryumin-Light-32 c49*/
            gPruneA = FixInt(50);
            gPruneC = 100;
            gPruneD = FixOne;
            tmp = (float)10.24; /* set to 1024 times the threshold value */
            gPruneValue = gPruneB = acpflttofix(&tmp);
            /* pruneB must be <= .01 for Yakout/Light/heM */
            /* pruneValue must be <= .01 for Yakout/Light/heM */
            gCPpercent = 40;
            /* must be < 46 for Americana-Bold d bowl vs stem hinting */
            gBandMargin = PSDist(30);
            gMaxFlare = PSDist(10);
            gMaxBendMerge = PSDist(6);
            gMaxMerge = PSDist(2); /* must be < 3 for Cushing-BookItalic z */
            gMinHintElementLength = PSDist(12);
            gFlexCand = PSDist(4);
            gSCurveTan = 25;
            gMaxVal = 8000000.0;
            gMinVal = 1.0f / (float)(FixOne);
            gEditGlyph = true;
            gRoundToInt = true;
            /* Default is to change a curve with collinear points into a line.
             */
            gAutoLinearCurveFix = true;
            gFlexOK = false;
            gFlexStrict = true;
            gBlueFuzz = DEFAULTBLUEFUZZ;
        /* fall through */
        case RESTART:
            memset((void*)vm, 0x0, VMSIZE);
            vmfree = vm;
            vmlast = vm + VMSIZE;

            /* ?? Does this cause a leak ?? */
            gPointList = NULL;
            gMaxPtLsts = 128;
            gPtLstArray = (HintPoint**)Alloc(gMaxPtLsts * sizeof(HintPoint*));
            gPtLstIndex = 0;
            gPtLstArray[0] = NULL;
            gNumPtLsts = 1;
            gAddHints = true;
            gVHinting = NULL;
            gHHinting = NULL;

            /*     if (glyphName != NULL && glyphName[0] == 'g')
                   showHintInfo = showHs = showVs = listHintInfo = true; */
    }
}

/* Returns whether hinting was successful. */
bool
AutoHint(const ACFontInfo* fontinfo, const char* srcbezdata, bool extrahint,
         bool changeGlyph, bool roundCoords)
{
    InitAll(STARTUP);

    if (!ReadFontInfo(fontinfo))
        return false;

    gEditGlyph = changeGlyph;
    gRoundToInt = roundCoords;
    gAutoLinearCurveFix = gEditGlyph;

    return AutoHintGlyph(srcbezdata, extrahint);
}
