/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "fontinfo.h"
#include "ac.h"

#define UNDEFINED (INT32_MAX)

int32_t gNumHHints, gNumVHints;

static void ParseIntStems(const ACFontInfo* fontinfo, char* kw, bool optional,
                          int32_t maxstems, int* stems, int32_t* pnum);

static char* GetFontInfo(const ACFontInfo*, char*, bool);

static void
ParseStems(const ACFontInfo* fontinfo, char* kw, Fixed* stems, int32_t* pnum)
{
    int istems[MAXSTEMS], i;
    memset(istems, 0, MAXSTEMS * sizeof(int));
    ParseIntStems(fontinfo, kw, OPTIONAL, MAXSTEMS, istems, pnum);
    for (i = 0; i < *pnum; i++)
        stems[i] = FixInt(istems[i]);
}

static void
GetKeyValue(const ACFontInfo* fontinfo, char* keyword, bool optional,
            int32_t* value)
{
    char* fontinfostr = GetFontInfo(fontinfo, keyword, optional);
    if (strlen(fontinfostr) != 0) {
        *value = (int32_t)atol(fontinfostr);
    }
    return;
}

static void
GetKeyFixedValue(const ACFontInfo* fontinfo, char* keyword, bool optional,
                 Fixed* value)
{
    char* fontinfostr = GetFontInfo(fontinfo, keyword, optional);
    if (strlen(fontinfostr) != 0)
        *value = FixInt(strtod(fontinfostr, NULL));
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
    bool ORDINARYHINTING = gWriteHintedBez;

    AscenderHeight = AscenderOvershoot = BaselineYCoord = BaselineOvershoot =
      Baseline5 = Baseline5Overshoot = Baseline6 = Baseline6Overshoot =
        CapHeight = CapOvershoot = DescenderHeight = DescenderOvershoot =
          FigHeight = FigOvershoot = Height5 = Height5Overshoot = Height6 =
            Height6Overshoot = LcHeight = LcOvershoot = OrdinalBaseline =
              OrdinalOvershoot = SuperiorBaseline = SuperiorOvershoot =
                UNDEFINED; /* mark as undefined */
    gNumHStems = gNumVStems = 0;
    gNumHHints = gNumVHints = 0;
    gLenBotBands = gLenTopBands = 0;

    /* check for FlexOK, AuxHStems, AuxVStems */
    /* for intelligent scaling, it's too hard to check these */
    ParseStems(fontinfo, "StemSnapH", gHStems, &gNumHStems);
    ParseStems(fontinfo, "StemSnapV", gVStems, &gNumVStems);
    if (gNumHStems == 0) {
        ParseStems(fontinfo, "DominantH", gHStems, &gNumHStems);
        ParseStems(fontinfo, "DominantV", gVStems, &gNumVStems);
    }
    fontinfostr = GetFontInfo(fontinfo, "FlexOK", !ORDINARYHINTING);
    gFlexOK = strcmp(fontinfostr, "false");

    fontinfostr = GetFontInfo(fontinfo, "FlexStrict", true);
    gFlexStrict = strcmp(fontinfostr, "false");

    /* get bluefuzz. It is already set to its default value in ac.c::InitData().
    GetKeyFixedValue does not change the value if it's not present in fontinfo.
    */
    GetKeyFixedValue(fontinfo, "BlueFuzz", OPTIONAL, &gBlueFuzz);

    /* Check for counter hinting glyphs. */
    fontinfostr = GetFontInfo(fontinfo, "VCounterChars", OPTIONAL);
    gNumVHints = AddCounterHintGlyphs(fontinfostr, gVHintList);
    fontinfostr = GetFontInfo(fontinfo, "HCounterChars", OPTIONAL);
    gNumHHints = AddCounterHintGlyphs(fontinfostr, gHHintList);

    GetKeyValue(fontinfo, "AscenderHeight", OPTIONAL, &AscenderHeight);
    GetKeyValue(fontinfo, "AscenderOvershoot", OPTIONAL, &AscenderOvershoot);
    GetKeyValue(fontinfo, "BaselineYCoord", !ORDINARYHINTING, &BaselineYCoord);
    GetKeyValue(fontinfo, "BaselineOvershoot", !ORDINARYHINTING,
                &BaselineOvershoot);
    GetKeyValue(fontinfo, "Baseline5", OPTIONAL, &Baseline5);
    GetKeyValue(fontinfo, "Baseline5Overshoot", OPTIONAL, &Baseline5Overshoot);
    GetKeyValue(fontinfo, "Baseline6", OPTIONAL, &Baseline6);
    GetKeyValue(fontinfo, "Baseline6Overshoot", OPTIONAL, &Baseline6Overshoot);
    GetKeyValue(fontinfo, "CapHeight", !ORDINARYHINTING, &CapHeight);
    GetKeyValue(fontinfo, "CapOvershoot", !ORDINARYHINTING, &CapOvershoot);
    GetKeyValue(fontinfo, "DescenderHeight", OPTIONAL, &DescenderHeight);
    GetKeyValue(fontinfo, "DescenderOvershoot", OPTIONAL, &DescenderOvershoot);
    GetKeyValue(fontinfo, "FigHeight", OPTIONAL, &FigHeight);
    GetKeyValue(fontinfo, "FigOvershoot", OPTIONAL, &FigOvershoot);
    GetKeyValue(fontinfo, "Height5", OPTIONAL, &Height5);
    GetKeyValue(fontinfo, "Height5Overshoot", OPTIONAL, &Height5Overshoot);
    GetKeyValue(fontinfo, "Height6", OPTIONAL, &Height6);
    GetKeyValue(fontinfo, "Height6Overshoot", OPTIONAL, &Height6Overshoot);
    GetKeyValue(fontinfo, "LcHeight", OPTIONAL, &LcHeight);
    GetKeyValue(fontinfo, "LcOvershoot", OPTIONAL, &LcOvershoot);
    GetKeyValue(fontinfo, "OrdinalBaseline", OPTIONAL, &OrdinalBaseline);
    GetKeyValue(fontinfo, "OrdinalOvershoot", OPTIONAL, &OrdinalOvershoot);
    GetKeyValue(fontinfo, "SuperiorBaseline", OPTIONAL, &SuperiorBaseline);
    GetKeyValue(fontinfo, "SuperiorOvershoot", OPTIONAL, &SuperiorOvershoot);

    gLenBotBands = gLenTopBands = 0;
    if (BaselineYCoord != UNDEFINED && BaselineOvershoot != UNDEFINED) {
        gBotBands[gLenBotBands++] = FixInt(BaselineYCoord + BaselineOvershoot);
        gBotBands[gLenBotBands++] = FixInt(BaselineYCoord);
    }
    if (Baseline5 != UNDEFINED && Baseline5Overshoot != UNDEFINED) {
        gBotBands[gLenBotBands++] = FixInt(Baseline5 + Baseline5Overshoot);
        gBotBands[gLenBotBands++] = FixInt(Baseline5);
    }
    if (Baseline6 != UNDEFINED && Baseline6Overshoot != UNDEFINED) {
        gBotBands[gLenBotBands++] = FixInt(Baseline6 + Baseline6Overshoot);
        gBotBands[gLenBotBands++] = FixInt(Baseline6);
    }
    if (SuperiorBaseline != UNDEFINED && SuperiorOvershoot != UNDEFINED) {
        gBotBands[gLenBotBands++] =
          FixInt(SuperiorBaseline + SuperiorOvershoot);
        gBotBands[gLenBotBands++] = FixInt(SuperiorBaseline);
    }
    if (OrdinalBaseline != UNDEFINED && OrdinalOvershoot != UNDEFINED) {
        gBotBands[gLenBotBands++] = FixInt(OrdinalBaseline + OrdinalOvershoot);
        gBotBands[gLenBotBands++] = FixInt(OrdinalBaseline);
    }
    if (DescenderHeight != UNDEFINED && DescenderOvershoot != UNDEFINED) {
        gBotBands[gLenBotBands++] =
          FixInt(DescenderHeight + DescenderOvershoot);
        gBotBands[gLenBotBands++] = FixInt(DescenderHeight);
    }
    if (CapHeight != UNDEFINED && CapOvershoot != UNDEFINED) {
        gTopBands[gLenTopBands++] = FixInt(CapHeight);
        gTopBands[gLenTopBands++] = FixInt(CapHeight + CapOvershoot);
    }
    if (LcHeight != UNDEFINED && LcOvershoot != UNDEFINED) {
        gTopBands[gLenTopBands++] = FixInt(LcHeight);
        gTopBands[gLenTopBands++] = FixInt(LcHeight + LcOvershoot);
    }
    if (AscenderHeight != UNDEFINED && AscenderOvershoot != UNDEFINED) {
        gTopBands[gLenTopBands++] = FixInt(AscenderHeight);
        gTopBands[gLenTopBands++] = FixInt(AscenderHeight + AscenderOvershoot);
    }
    if (FigHeight != UNDEFINED && FigOvershoot != UNDEFINED) {
        gTopBands[gLenTopBands++] = FixInt(FigHeight);
        gTopBands[gLenTopBands++] = FixInt(FigHeight + FigOvershoot);
    }
    if (Height5 != UNDEFINED && Height5Overshoot != UNDEFINED) {
        gTopBands[gLenTopBands++] = FixInt(Height5);
        gTopBands[gLenTopBands++] = FixInt(Height5 + Height5Overshoot);
    }
    if (Height6 != UNDEFINED && Height6Overshoot != UNDEFINED) {
        gTopBands[gLenTopBands++] = FixInt(Height6);
        gTopBands[gLenTopBands++] = FixInt(Height6 + Height6Overshoot);
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

/* Looks up the value of the specified keyword in the fontinfo file. If the
 * keyword doesn't exist and this is an optional key, returns an empty string.
 * Otherwise, returns the value string. */
static char*
GetFontInfo(const ACFontInfo* fontinfo, char* keyword, bool optional)
{
    size_t i;

    if (!fontinfo) {
        LogMsg(LOGERROR, NONFATALERROR, "Fontinfo is NULL");
        return "";
    }

    for (i = 0; i < fontinfo->length; i++) {
        if (fontinfo->keys[i] && !strcmp(fontinfo->keys[i], keyword)) {
            return fontinfo->values[i];
        }
    }

    if (!optional) {
        LogMsg(LOGERROR, NONFATALERROR,
               "Fontinfo: Couldn't find fontinfo for %s.", keyword);
    }

    return "";
}

/*
 * This procedure parses the various fontinfo file stem keywords:
 * StemSnap{H,V}, Dominant{H,V}.
 * ParseIntStems guarantees that stem values are unique and in ascending order.
 */
static void
ParseIntStems(const ACFontInfo* fontinfo, char* kw, bool optional,
              int32_t maxstems, int* stems, int32_t* pnum)
{
    int i, j, count = 0;
    bool singleint = false;
    char* initline;
    char* line;

    *pnum = 0;

    initline = GetFontInfo(fontinfo, kw, optional);
    if (strlen(initline) == 0)
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
                   "Cannot have more than %d values in fontinfo array: %s",
                   (int)maxstems, initline);
        }

        if (val < 1) {
            LogMsg(LOGERROR, NONFATALERROR,
                   "Cannot have a value < 1 in fontinfo file array: %s", line);
        }

        stems[count++] = val;

        if (singleint)
            break;

        while (misdigit(*line))
            line++; /* skip past the number */
    }

    /* ensure they are in order */
    for (i = 0; i < count; i++) {
        for (j = i + 1; j < count; j++) {
            if (stems[i] > stems[j]) {
                int temp = stems[i];
                stems[i] = stems[j];
                stems[j] = temp;
            }
        }
    }

    /* ensure they are unique - note: complaint for too many might precede
       guarantee of uniqueness */
    for (i = 0; i < count - 1; i++) {
        if (stems[i] == stems[i + 1]) {
            for (j = (i + 2); j < count; j++)
                stems[j - 1] = stems[j];
            count--;
        }
    }

    *pnum = count;
}

void
FreeFontInfo(ACFontInfo* fontinfo)
{
    size_t i;

    if (!fontinfo)
        return;

    if(fontinfo->values) {
        for (i = 0; i < fontinfo->length; i++) {
            if (fontinfo->values[i][0]) {
                UnallocateMem(fontinfo->values[i]);
            }
        }
        UnallocateMem(fontinfo->values);
    }
    UnallocateMem(fontinfo);
}

static char* fontinfo_keys[] = {
    "OrigEmSqUnits", "FontName", "FlexOK",
    /* Blue Values */
    "BaselineOvershoot", "BaselineYCoord", "CapHeight", "CapOvershoot",
    "LcHeight", "LcOvershoot", "AscenderHeight", "AscenderOvershoot",
    "FigHeight", "FigOvershoot", "Height5", "Height5Overshoot", "Height6",
    "Height6Overshoot",
    /* Other Values */
    "Baseline5Overshoot", "Baseline5", "Baseline6Overshoot", "Baseline6",
    "SuperiorOvershoot", "SuperiorBaseline", "OrdinalOvershoot",
    "OrdinalBaseline", "DescenderOvershoot", "DescenderHeight",

    "DominantV", "StemSnapV", "DominantH", "StemSnapH", "VCounterChars",
    "HCounterChars",
    /* later addenda */
    "BlueFuzz",

    NULL
};

static ACFontInfo*
NewFontInfo(void)
{
    size_t i;
    ACFontInfo* fontinfo;

    fontinfo = (ACFontInfo*)AllocateMem(1, sizeof(ACFontInfo), "fontinfo");
    fontinfo->length = 0;
    while (fontinfo_keys[fontinfo->length] != NULL)
        fontinfo->length++;
    fontinfo->values =
      (char**)AllocateMem(fontinfo->length, sizeof(char*), "fontinfo values");

    fontinfo->keys = fontinfo_keys;
    for (i = 0; i < fontinfo->length; i++)
        fontinfo->values[i] = "";

    return fontinfo;
}

#define skipblanks()                                                           \
    while (*current == '\t' || *current == '\n' || *current == ' ' ||          \
           *current == '\r')                                                   \
    current++
#define skipnonblanks()                                                        \
    while (*current != '\t' && *current != '\n' && *current != ' ' &&          \
           *current != '\r' && *current != '\0')                               \
    current++
#define skipmatrix()                                                           \
    while (*current != '\0' && *current != ']')                                \
    current++

static void
skippsstring(const char** current)
{
    int parencount = 0;

    do {
        if (**current == '(')
            parencount++;
        else if (**current == ')')
            parencount--;
        else if (**current == '\0')
            return;

        (*current)++;

    } while (parencount > 0);
}

ACFontInfo*
ParseFontInfo(const char* data)
{
    const char* current;
    size_t i;

    ACFontInfo* info = NewFontInfo();

    if (!data)
        return info;

    current = data;
    while (*current) {
        size_t kwLen;
        const char *kwstart, *kwend, *tkstart;
        skipblanks();
        kwstart = current;
        skipnonblanks();
        kwend = current;
        skipblanks();
        tkstart = current;

        if (*tkstart == '(') {
            skippsstring(&current);
            if (*tkstart)
                current++;
        } else if (*tkstart == '[') {
            skipmatrix();
            if (*tkstart)
                current++;
        } else
            skipnonblanks();

        kwLen = (size_t)(kwend - kwstart);
        for (i = 0; i < info->length; i++) {
            size_t matchLen = NUMMAX(kwLen, strlen(info->keys[i]));
            if (!strncmp(info->keys[i], kwstart, matchLen)) {
                info->values[i] =
                  AllocateMem(current - tkstart + 1, 1, "fontinfo entry value");
                strncpy(info->values[i], tkstart, current - tkstart);
                info->values[i][current - tkstart] = '\0';
                break;
            }
        }
        skipblanks();
    }

    return info;
}
