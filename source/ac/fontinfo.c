/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"
#include "fipublic.h"

#define UNDEFINED (INT32_MAX)

int32_t NumHColors, NumVColors;

static void
ParseStems(const ACFontInfo* fontinfo, char* kw, Fixed* stems, int32_t* pnum)
{
    int istems[MAXSTEMS], i;
    ParseIntStems(fontinfo, kw, ACOPTIONAL, MAXSTEMS, istems, pnum, NULL);
    for (i = 0; i < *pnum; i++)
        stems[i] = FixInt(istems[i]);
}

static void
GetKeyValue(const ACFontInfo* fontinfo, char* keyword, bool optional,
            int32_t* value)
{
    char* fontinfostr;

    fontinfostr = GetFntInfo(fontinfo, keyword, optional);

    if ((fontinfostr != NULL) && (fontinfostr[0] != 0)) {
        *value = atol(fontinfostr);
        ACFREEMEM(fontinfostr);
    }
    return;
}

static void
GetKeyFixedValue(const ACFontInfo* fontinfo, char* keyword, bool optional,
                 Fixed* value)
{
    char* fontinfostr;
    float tempValue;

    fontinfostr = GetFntInfo(fontinfo, keyword, optional);

    if ((fontinfostr != NULL) && (fontinfostr[0] != 0)) {
        sscanf(fontinfostr, "%g", &tempValue);
        *value = (Fixed)tempValue * (1 << FixShift);
        ACFREEMEM(fontinfostr);
    }
    return;
}

bool
ReadFontInfo(const ACFontInfo* fontinfo)
{
    char* fontinfostr;
    int32_t AscenderHeight, AscenderOvershoot, BaselineYCoord,
      BaselineOvershoot, Baseline5, Baseline5Overshoot, Baseline6,
      Baseline6Overshoot, CapHeight, CapOvershoot, DescenderHeight,
      DescenderOvershoot, FigHeight, FigOvershoot, Height5, Height5Overshoot,
      Height6, Height6Overshoot, LcHeight, LcOvershoot, OrdinalBaseline,
      OrdinalOvershoot, SuperiorBaseline, SuperiorOvershoot;
    bool ORDINARYCOLORING = !scalinghints && writecoloredbez;

    AscenderHeight = AscenderOvershoot = BaselineYCoord = BaselineOvershoot =
      Baseline5 = Baseline5Overshoot = Baseline6 = Baseline6Overshoot =
        CapHeight = CapOvershoot = DescenderHeight = DescenderOvershoot =
          FigHeight = FigOvershoot = Height5 = Height5Overshoot = Height6 =
            Height6Overshoot = LcHeight = LcOvershoot = OrdinalBaseline =
              OrdinalOvershoot = SuperiorBaseline = SuperiorOvershoot =
                UNDEFINED; /* mark as undefined */
    NumHStems = NumVStems = 0;
    NumHColors = NumVColors = 0;
    lenBotBands = lenTopBands = 0;

    /* check for FlexOK, AuxHStems, AuxVStems */
    if (
      !scalinghints) /* for intelligent scaling, it's too hard to check these */
    {
        ParseStems(fontinfo, "StemSnapH", HStems, &NumHStems);
        ParseStems(fontinfo, "StemSnapV", VStems, &NumVStems);
        if (NumHStems == 0) {
            ParseStems(fontinfo, "DominantH", HStems, &NumHStems);
            ParseStems(fontinfo, "DominantV", VStems, &NumVStems);
        }
    }
    fontinfostr = GetFntInfo(fontinfo, "FlexOK", !ORDINARYCOLORING);
    flexOK = (fontinfostr != NULL) && (fontinfostr[0] != '\0') &&
             strcmp(fontinfostr, "false");

    ACFREEMEM(fontinfostr);
    fontinfostr = GetFntInfo(fontinfo, "FlexStrict", true);
    if (fontinfostr != NULL)
        flexStrict = strcmp(fontinfostr, "false");
    ACFREEMEM(fontinfostr);

    /* get bluefuzz. It is already set to its default value in ac.c::InitData().
    GetKeyFixedValue does nto change the value if it is not present in fontinfo.
    */
    GetKeyFixedValue(fontinfo, "BlueFuzz", ACOPTIONAL, &bluefuzz);

    /* Check for counter coloring characters. */
    if ((fontinfostr = GetFntInfo(fontinfo, "VCounterChars", ACOPTIONAL)) !=
        NULL) {
        NumVColors = AddCounterColorChars(fontinfostr, VColorList);
        ACFREEMEM(fontinfostr);
    };
    if ((fontinfostr = GetFntInfo(fontinfo, "HCounterChars", ACOPTIONAL)) !=
        NULL) {
        NumHColors = AddCounterColorChars(fontinfostr, HColorList);
        ACFREEMEM(fontinfostr);
    };
    GetKeyValue(fontinfo, "AscenderHeight", ACOPTIONAL, &AscenderHeight);
    GetKeyValue(fontinfo, "AscenderOvershoot", ACOPTIONAL, &AscenderOvershoot);
    GetKeyValue(fontinfo, "BaselineYCoord", !ORDINARYCOLORING, &BaselineYCoord);
    GetKeyValue(fontinfo, "BaselineOvershoot", !ORDINARYCOLORING,
                &BaselineOvershoot);
    GetKeyValue(fontinfo, "Baseline5", ACOPTIONAL, &Baseline5);
    GetKeyValue(fontinfo, "Baseline5Overshoot", ACOPTIONAL,
                &Baseline5Overshoot);
    GetKeyValue(fontinfo, "Baseline6", ACOPTIONAL, &Baseline6);
    GetKeyValue(fontinfo, "Baseline6Overshoot", ACOPTIONAL,
                &Baseline6Overshoot);
    GetKeyValue(fontinfo, "CapHeight", !ORDINARYCOLORING, &CapHeight);
    GetKeyValue(fontinfo, "CapOvershoot", !ORDINARYCOLORING, &CapOvershoot);
    GetKeyValue(fontinfo, "DescenderHeight", ACOPTIONAL, &DescenderHeight);
    GetKeyValue(fontinfo, "DescenderOvershoot", ACOPTIONAL,
                &DescenderOvershoot);
    GetKeyValue(fontinfo, "FigHeight", ACOPTIONAL, &FigHeight);
    GetKeyValue(fontinfo, "FigOvershoot", ACOPTIONAL, &FigOvershoot);
    GetKeyValue(fontinfo, "Height5", ACOPTIONAL, &Height5);
    GetKeyValue(fontinfo, "Height5Overshoot", ACOPTIONAL, &Height5Overshoot);
    GetKeyValue(fontinfo, "Height6", ACOPTIONAL, &Height6);
    GetKeyValue(fontinfo, "Height6Overshoot", ACOPTIONAL, &Height6Overshoot);
    GetKeyValue(fontinfo, "LcHeight", ACOPTIONAL, &LcHeight);
    GetKeyValue(fontinfo, "LcOvershoot", ACOPTIONAL, &LcOvershoot);
    GetKeyValue(fontinfo, "OrdinalBaseline", ACOPTIONAL, &OrdinalBaseline);
    GetKeyValue(fontinfo, "OrdinalOvershoot", ACOPTIONAL, &OrdinalOvershoot);
    GetKeyValue(fontinfo, "SuperiorBaseline", ACOPTIONAL, &SuperiorBaseline);
    GetKeyValue(fontinfo, "SuperiorOvershoot", ACOPTIONAL, &SuperiorOvershoot);

    lenBotBands = lenTopBands = 0;
    if (BaselineYCoord != UNDEFINED && BaselineOvershoot != UNDEFINED) {
        botBands[lenBotBands++] =
          ScaleAbs(fontinfo, FixInt(BaselineYCoord + BaselineOvershoot));
        botBands[lenBotBands++] = ScaleAbs(fontinfo, FixInt(BaselineYCoord));
    }
    if (Baseline5 != UNDEFINED && Baseline5Overshoot != UNDEFINED) {
        botBands[lenBotBands++] =
          ScaleAbs(fontinfo, FixInt(Baseline5 + Baseline5Overshoot));
        botBands[lenBotBands++] = ScaleAbs(fontinfo, FixInt(Baseline5));
    }
    if (Baseline6 != UNDEFINED && Baseline6Overshoot != UNDEFINED) {
        botBands[lenBotBands++] =
          ScaleAbs(fontinfo, FixInt(Baseline6 + Baseline6Overshoot));
        botBands[lenBotBands++] = ScaleAbs(fontinfo, FixInt(Baseline6));
    }
    if (SuperiorBaseline != UNDEFINED && SuperiorOvershoot != UNDEFINED) {
        botBands[lenBotBands++] =
          ScaleAbs(fontinfo, FixInt(SuperiorBaseline + SuperiorOvershoot));
        botBands[lenBotBands++] = ScaleAbs(fontinfo, FixInt(SuperiorBaseline));
    }
    if (OrdinalBaseline != UNDEFINED && OrdinalOvershoot != UNDEFINED) {
        botBands[lenBotBands++] =
          ScaleAbs(fontinfo, FixInt(OrdinalBaseline + OrdinalOvershoot));
        botBands[lenBotBands++] = ScaleAbs(fontinfo, FixInt(OrdinalBaseline));
    }
    if (DescenderHeight != UNDEFINED && DescenderOvershoot != UNDEFINED) {
        botBands[lenBotBands++] =
          ScaleAbs(fontinfo, FixInt(DescenderHeight + DescenderOvershoot));
        botBands[lenBotBands++] = ScaleAbs(fontinfo, FixInt(DescenderHeight));
    }
    if (CapHeight != UNDEFINED && CapOvershoot != UNDEFINED) {
        topBands[lenTopBands++] = ScaleAbs(fontinfo, FixInt(CapHeight));
        topBands[lenTopBands++] =
          ScaleAbs(fontinfo, FixInt(CapHeight + CapOvershoot));
    }
    if (LcHeight != UNDEFINED && LcOvershoot != UNDEFINED) {
        topBands[lenTopBands++] = ScaleAbs(fontinfo, FixInt(LcHeight));
        topBands[lenTopBands++] =
          ScaleAbs(fontinfo, FixInt(LcHeight + LcOvershoot));
    }
    if (AscenderHeight != UNDEFINED && AscenderOvershoot != UNDEFINED) {
        topBands[lenTopBands++] = ScaleAbs(fontinfo, FixInt(AscenderHeight));
        topBands[lenTopBands++] =
          ScaleAbs(fontinfo, FixInt(AscenderHeight + AscenderOvershoot));
    }
    if (FigHeight != UNDEFINED && FigOvershoot != UNDEFINED) {
        topBands[lenTopBands++] = ScaleAbs(fontinfo, FixInt(FigHeight));
        topBands[lenTopBands++] =
          ScaleAbs(fontinfo, FixInt(FigHeight + FigOvershoot));
    }
    if (Height5 != UNDEFINED && Height5Overshoot != UNDEFINED) {
        topBands[lenTopBands++] = ScaleAbs(fontinfo, FixInt(Height5));
        topBands[lenTopBands++] =
          ScaleAbs(fontinfo, FixInt(Height5 + Height5Overshoot));
    }
    if (Height6 != UNDEFINED && Height6Overshoot != UNDEFINED) {
        topBands[lenTopBands++] = ScaleAbs(fontinfo, FixInt(Height6));
        topBands[lenTopBands++] =
          ScaleAbs(fontinfo, FixInt(Height6 + Height6Overshoot));
    }
    return true;
}
