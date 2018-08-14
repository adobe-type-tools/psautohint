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
static void
ParseStems(const ACFontInfo* fontinfo, char* kw, Fixed* stems, int32_t* pnum)
{
    int istems[MAXSTEMS], i;
    ParseIntStems(fontinfo, kw, OPTIONAL, MAXSTEMS, istems, pnum);
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
        *value = (int32_t)atol(fontinfostr);
    }
    return;
}

static void
GetKeyFixedValue(const ACFontInfo* fontinfo, char* keyword, bool optional,
                 Fixed* value)
{
    char* fontinfostr;

    fontinfostr = GetFontInfo(fontinfo, keyword, optional);

    if ((fontinfostr != NULL) && (fontinfostr[0] != 0)) {
        float tempValue = strtod(fontinfostr, NULL);
        *value = (Fixed)tempValue * (1 << FixShift);
    }
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
    bool ORDINARYHINTING = !gScalingHints && gWriteHintedBez;

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
    if (!gScalingHints) {
        ParseStems(fontinfo, "StemSnapH", gHStems, &gNumHStems);
        ParseStems(fontinfo, "StemSnapV", gVStems, &gNumVStems);
        if (gNumHStems == 0) {
            ParseStems(fontinfo, "DominantH", gHStems, &gNumHStems);
            ParseStems(fontinfo, "DominantV", gVStems, &gNumVStems);
        }
    }
    fontinfostr = GetFontInfo(fontinfo, "FlexOK", !ORDINARYHINTING);
    gFlexOK = (fontinfostr != NULL) && (fontinfostr[0] != '\0') &&
              strcmp(fontinfostr, "false");

    fontinfostr = GetFontInfo(fontinfo, "FlexStrict", true);
    if (fontinfostr != NULL)
        gFlexStrict = strcmp(fontinfostr, "false");

    /* get bluefuzz. It is already set to its default value in ac.c::InitData().
    GetKeyFixedValue does not change the value if it's not present in fontinfo.
    */
    GetKeyFixedValue(fontinfo, "BlueFuzz", OPTIONAL, &gBlueFuzz);

    /* Check for counter hinting glyphs. */
    fontinfostr = GetFontInfo(fontinfo, "VCounterChars", OPTIONAL);
    if (fontinfostr != NULL)
        gNumVHints = AddCounterHintGlyphs(fontinfostr, gVHintList);
    fontinfostr = GetFontInfo(fontinfo, "HCounterChars", OPTIONAL);
    if (fontinfostr != NULL)
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
        gBotBands[gLenBotBands++] =
          ScaleAbs(fontinfo, FixInt(BaselineYCoord + BaselineOvershoot));
        gBotBands[gLenBotBands++] = ScaleAbs(fontinfo, FixInt(BaselineYCoord));
    }
    if (Baseline5 != UNDEFINED && Baseline5Overshoot != UNDEFINED) {
        gBotBands[gLenBotBands++] =
          ScaleAbs(fontinfo, FixInt(Baseline5 + Baseline5Overshoot));
        gBotBands[gLenBotBands++] = ScaleAbs(fontinfo, FixInt(Baseline5));
    }
    if (Baseline6 != UNDEFINED && Baseline6Overshoot != UNDEFINED) {
        gBotBands[gLenBotBands++] =
          ScaleAbs(fontinfo, FixInt(Baseline6 + Baseline6Overshoot));
        gBotBands[gLenBotBands++] = ScaleAbs(fontinfo, FixInt(Baseline6));
    }
    if (SuperiorBaseline != UNDEFINED && SuperiorOvershoot != UNDEFINED) {
        gBotBands[gLenBotBands++] =
          ScaleAbs(fontinfo, FixInt(SuperiorBaseline + SuperiorOvershoot));
        gBotBands[gLenBotBands++] =
          ScaleAbs(fontinfo, FixInt(SuperiorBaseline));
    }
    if (OrdinalBaseline != UNDEFINED && OrdinalOvershoot != UNDEFINED) {
        gBotBands[gLenBotBands++] =
          ScaleAbs(fontinfo, FixInt(OrdinalBaseline + OrdinalOvershoot));
        gBotBands[gLenBotBands++] = ScaleAbs(fontinfo, FixInt(OrdinalBaseline));
    }
    if (DescenderHeight != UNDEFINED && DescenderOvershoot != UNDEFINED) {
        gBotBands[gLenBotBands++] =
          ScaleAbs(fontinfo, FixInt(DescenderHeight + DescenderOvershoot));
        gBotBands[gLenBotBands++] = ScaleAbs(fontinfo, FixInt(DescenderHeight));
    }
    if (CapHeight != UNDEFINED && CapOvershoot != UNDEFINED) {
        gTopBands[gLenTopBands++] = ScaleAbs(fontinfo, FixInt(CapHeight));
        gTopBands[gLenTopBands++] =
          ScaleAbs(fontinfo, FixInt(CapHeight + CapOvershoot));
    }
    if (LcHeight != UNDEFINED && LcOvershoot != UNDEFINED) {
        gTopBands[gLenTopBands++] = ScaleAbs(fontinfo, FixInt(LcHeight));
        gTopBands[gLenTopBands++] =
          ScaleAbs(fontinfo, FixInt(LcHeight + LcOvershoot));
    }
    if (AscenderHeight != UNDEFINED && AscenderOvershoot != UNDEFINED) {
        gTopBands[gLenTopBands++] = ScaleAbs(fontinfo, FixInt(AscenderHeight));
        gTopBands[gLenTopBands++] =
          ScaleAbs(fontinfo, FixInt(AscenderHeight + AscenderOvershoot));
    }
    if (FigHeight != UNDEFINED && FigOvershoot != UNDEFINED) {
        gTopBands[gLenTopBands++] = ScaleAbs(fontinfo, FixInt(FigHeight));
        gTopBands[gLenTopBands++] =
          ScaleAbs(fontinfo, FixInt(FigHeight + FigOvershoot));
    }
    if (Height5 != UNDEFINED && Height5Overshoot != UNDEFINED) {
        gTopBands[gLenTopBands++] = ScaleAbs(fontinfo, FixInt(Height5));
        gTopBands[gLenTopBands++] =
          ScaleAbs(fontinfo, FixInt(Height5 + Height5Overshoot));
    }
    if (Height6 != UNDEFINED && Height6Overshoot != UNDEFINED) {
        gTopBands[gLenTopBands++] = ScaleAbs(fontinfo, FixInt(Height6));
        gTopBands[gLenTopBands++] =
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
    size_t i;

    if (!fontinfo) {
        LogMsg(LOGERROR, NONFATALERROR, "Fontinfo is NULL");
        return NULL;
    }

    for (i = 0; i < fontinfo->length; i++) {
        if (fontinfo->entries[i].key &&
            !strcmp(fontinfo->entries[i].key, keyword)) {
            return fontinfo->entries[i].value;
        }
    }

    if (!optional) {
        LogMsg(LOGERROR, NONFATALERROR,
               "Fontinfo: Couldn't find fontinfo for %s.", keyword);
    }

    return NULL;
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

        if (sscanf(line, " %d", &val) == EOF)
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

    for (i = 0; i < fontinfo->length; i++) {
        if (fontinfo->entries[i].value[0]) {
            UnallocateMem(fontinfo->entries[i].value);
        }
    }
    UnallocateMem(fontinfo->entries);
    UnallocateMem(fontinfo);
}

static ACFontInfo*
NewFontInfo(size_t length)
{
    ACFontInfo* fontinfo;

    if (length == 0)
        return NULL;

    fontinfo = (ACFontInfo*)AllocateMem(1, sizeof(ACFontInfo), "fontinfo");
    if (!fontinfo)
        return NULL;

    fontinfo->entries =
      (FFEntry*)AllocateMem(length, sizeof(FFEntry), "fontinfo entry");
    if (!fontinfo->entries) {
        UnallocateMem(fontinfo);
        return NULL;
    }

    fontinfo->length = length;

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

    ACFontInfo* info = NewFontInfo(34);
    if (!info)
        return NULL;

    info->entries[0].key = "OrigEmSqUnits";
    info->entries[1].key = "FontName";
    info->entries[2].key = "FlexOK";
    /* Blue Values */
    info->entries[3].key = "BaselineOvershoot";
    info->entries[4].key = "BaselineYCoord";
    info->entries[5].key = "CapHeight";
    info->entries[6].key = "CapOvershoot";
    info->entries[7].key = "LcHeight";
    info->entries[8].key = "LcOvershoot";
    info->entries[9].key = "AscenderHeight";
    info->entries[10].key = "AscenderOvershoot";
    info->entries[11].key = "FigHeight";
    info->entries[12].key = "FigOvershoot";
    info->entries[13].key = "Height5";
    info->entries[14].key = "Height5Overshoot";
    info->entries[15].key = "Height6";
    info->entries[16].key = "Height6Overshoot";
    /* Other Values */
    info->entries[17].key = "Baseline5Overshoot";
    info->entries[18].key = "Baseline5";
    info->entries[19].key = "Baseline6Overshoot";
    info->entries[20].key = "Baseline6";
    info->entries[21].key = "SuperiorOvershoot";
    info->entries[22].key = "SuperiorBaseline";
    info->entries[23].key = "OrdinalOvershoot";
    info->entries[24].key = "OrdinalBaseline";
    info->entries[25].key = "DescenderOvershoot";
    info->entries[26].key = "DescenderHeight";

    info->entries[27].key = "DominantV";
    info->entries[28].key = "StemSnapV";
    info->entries[29].key = "DominantH";
    info->entries[30].key = "StemSnapH";
    info->entries[31].key = "VCounterChars";
    info->entries[32].key = "HCounterChars";
    /* later addenda */
    info->entries[33].key = "BlueFuzz";

    for (i = 0; i < info->length; i++) {
        info->entries[i].value = "";
    }

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
            size_t matchLen = NUMMAX(kwLen, strlen(info->entries[i].key));
            if (!strncmp(info->entries[i].key, kwstart, matchLen)) {
                info->entries[i].value =
                  AllocateMem(current - tkstart + 1, 1, "fontinfo entry value");
                if (!info->entries[i].value) {
                    FreeFontInfo(info);
                    return NULL;
                }
                strncpy(info->entries[i].value, tkstart, current - tkstart);
                info->entries[i].value[current - tkstart] = '\0';
                break;
            }
        }
        skipblanks();
    }

    return info;
}
