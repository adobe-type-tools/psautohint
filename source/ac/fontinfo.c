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

#define UNDEFINED (INT32_MAX)

int32_t NumHColors, NumVColors;

static void
ParseStems(const ACFontInfo* fontinfo, char* kw, Fixed* stems, int32_t* pnum)
{
    int istems[MAXSTEMS], i;
    ParseIntStems(fontinfo, kw, ACOPTIONAL, MAXSTEMS, istems, pnum);
    for (i = 0; i < *pnum; i++)
        stems[i] = FixInt(istems[i]);
}

static void
GetKeyValue(const ACFontInfo* fontinfo, char* keyword, bool optional,
            int32_t* value)
{
    char* fontinfostr;

    fontinfostr = GetFontInfo(fontinfo, keyword, optional);

    if ((fontinfostr != NULL) && (fontinfostr[0] != 0)) {
        *value = atol(fontinfostr);
        UnallocateMem(fontinfostr);
    }
    return;
}

static void
GetKeyFixedValue(const ACFontInfo* fontinfo, char* keyword, bool optional,
                 Fixed* value)
{
    char* fontinfostr;
    float tempValue;

    fontinfostr = GetFontInfo(fontinfo, keyword, optional);

    if ((fontinfostr != NULL) && (fontinfostr[0] != 0)) {
        sscanf(fontinfostr, "%g", &tempValue);
        *value = (Fixed)tempValue * (1 << FixShift);
        UnallocateMem(fontinfostr);
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
    fontinfostr = GetFontInfo(fontinfo, "FlexOK", !ORDINARYCOLORING);
    flexOK = (fontinfostr != NULL) && (fontinfostr[0] != '\0') &&
             strcmp(fontinfostr, "false");

    UnallocateMem(fontinfostr);
    fontinfostr = GetFontInfo(fontinfo, "FlexStrict", true);
    if (fontinfostr != NULL)
        flexStrict = strcmp(fontinfostr, "false");
    UnallocateMem(fontinfostr);

    /* get bluefuzz. It is already set to its default value in ac.c::InitData().
    GetKeyFixedValue does nto change the value if it is not present in fontinfo.
    */
    GetKeyFixedValue(fontinfo, "BlueFuzz", ACOPTIONAL, &bluefuzz);

    /* Check for counter coloring characters. */
    if ((fontinfostr = GetFontInfo(fontinfo, "VCounterChars", ACOPTIONAL)) !=
        NULL) {
        NumVColors = AddCounterColorChars(fontinfostr, VColorList);
        UnallocateMem(fontinfostr);
    };
    if ((fontinfostr = GetFontInfo(fontinfo, "HCounterChars", ACOPTIONAL)) !=
        NULL) {
        NumHColors = AddCounterColorChars(fontinfostr, HColorList);
        UnallocateMem(fontinfostr);
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

static int
misspace(int c)
{
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
        return 1;
    return 0;
}

static int
misdigit(int c)
{
    return c >= '0' && c <= '9';
}

/* Looks up the value of the specified keyword in the fontinfo
   file.  If the keyword doesn't exist and this is an optional
   key, returns a NULL.  Otherwise, returns the value string. */
char*
GetFontInfo(const ACFontInfo* fontinfo, char* keyword, bool optional)
{
    char* returnstring = NULL;
    int i;

    if (!fontinfo) {
        LogMsg(LOGERROR, NONFATALERROR, "Fontinfo is NULL");
        return NULL;
    }

    for (i = 0; i < fontinfo->length; i++) {
        if (fontinfo->entries[i].key &&
            !strcmp(fontinfo->entries[i].key, keyword)) {
            returnstring = AllocateMem(strlen(fontinfo->entries[i].value) + 1,
                                       sizeof(char), "GetFontInfo return str");
            strcpy(returnstring, fontinfo->entries[i].value);
            return returnstring;
        }
    }

    if (!optional) {
        LogMsg(LOGERROR, NONFATALERROR,
               "ERROR: Fontinfo: Couldn't find fontinfo for %s\n", keyword);
    }

    return NULL;
}

/* Appends Aux{H,V}Stems which is optional to StemSnap{H,V} respectively. */
static char*
GetHVStems(const ACFontInfo* fontinfo, char* kw, bool optional)
{
    char *fistr1, *fistr2, *newfistr;
    char *end, *start;

    fistr1 = GetFontInfo(
      fontinfo, strcmp(kw, "AuxHStems") ? "StemSnapV" : "StemSnapH", optional);
    fistr2 = GetFontInfo(fontinfo, kw, ACOPTIONAL);
    if (fistr2 == NULL)
        return fistr1;
    if (fistr1 == NULL)
        return fistr2;
    /* Merge two arrays. */
    newfistr = AllocateMem(strlen(fistr1) + strlen(fistr2) + 1, sizeof(char),
                           "Aux stem value");
    end = (char*)strrchr(fistr1, ']');
    end[0] = '\0';
    start = (char*)strchr(fistr2, '[');
    start[0] = ' ';
    snprintf(newfistr, strlen(fistr1) + strlen(fistr2), "%s%s", fistr1, fistr2);
    UnallocateMem(fistr1);
    UnallocateMem(fistr2);
    return newfistr;
}

/* This procedure parses the various fontinfo file stem keywords:
   StemSnap{H,V}, Dominant{H,V} and Aux{H,V}Stems.  If Aux{H,V}Stems
   is specified then the StemSnap{H,V} values are automatically
   added to the stem array.  ParseIntStems guarantees that stem values
   are unique and in ascending order.
 */
void
ParseIntStems(const ACFontInfo* fontinfo, char* kw, bool optional,
              int32_t maxstems, int* stems, int32_t* pnum)
{
    int i, j, count = 0;
    bool singleint = false;
    char* initline;
    char* line;

    *pnum = 0;

    if (strcmp(kw, "AuxHStems") == 0 || strcmp(kw, "AuxVStems") == 0)
        initline = GetHVStems(fontinfo, kw, optional);
    else
        initline = GetFontInfo(fontinfo, kw, optional);

    if (initline == NULL || strlen(initline) == 0)
        return; /* optional keyword not found */

    line = initline;

    if (strchr(line, '[') == NULL)
        singleint = true; /* A single integer instead of a matrix. */
    else
        line = strchr(line, '[') + 1; /* A matrix, skip past first "[". */

    while (*line != ']') {
        int val;

        while (misspace(*line))
            line++; /* skip past any blanks */

        if (sscanf(line, " %d", &val) < 1)
            break;

        if (count >= maxstems) {
            LogMsg(LOGERROR, NONFATALERROR,
                   "Cannot have more than %d values in fontinfo array:\n  %s\n",
                   (int)maxstems, initline);
        }

        if (val < 1) {
            LogMsg(LOGERROR, NONFATALERROR,
                   "Cannot have a value < 1 in fontinfo file array: \n  %s\n",
                   line);
        }

        stems[count++] = val;

        if (singleint)
            break;

        while (misdigit(*line))
            line++; /* skip past the number */
    }

    /* insure they are in order */
    for (i = 0; i < count; i++) {
        for (j = i + 1; j < count; j++) {
            if (stems[i] > stems[j]) {
                int temp = stems[i];
                stems[i] = stems[j];
                stems[j] = temp;
            }
        }
    }

    /* insure they are unique - note: complaint for too many might precede
       guarantee of uniqueness */
    for (i = 0; i < count - 1; i++) {
        if (stems[i] == stems[i + 1]) {
            for (j = (i + 2); j < count; j++)
                stems[j - 1] = stems[j];
            count--;
        }
    }

    *pnum = count;
    UnallocateMem(initline);
}
