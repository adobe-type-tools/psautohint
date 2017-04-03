/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "setjmp.h"

#include "ac.h"
#include "ac_C_lib.h"
#include "machinedep.h"

const char* libversion = "1.6.0";
const char* progname = "AC_C_lib";
char editingResource = 0;

ACBuffer* bezoutput = NULL;

bool scalinghints = false;

extern int
  compatiblemode; /*forces the definition of colors even before a color sub*/

jmp_buf aclibmark; /* to handle exit() calls in the library version*/

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

static void
FreeFontInfo(ACFontInfo* fontinfo)
{
    int i;

    if (!fontinfo)
        return;

    for (i = 0; i < fontinfo->length; i++) {
        if (fontinfo->entries[i].value[0]) {
            ACFREEMEM(fontinfo->entries[i].value);
        }
    }
    ACFREEMEM(fontinfo->entries);
    ACFREEMEM(fontinfo);
}

static ACFontInfo*
NewFontInfo(int length)
{
    ACFontInfo* fontinfo;

    if (length <= 0)
        return NULL;

    fontinfo = (ACFontInfo*)ACNEWMEM(sizeof(ACFontInfo));
    if (!fontinfo)
        return NULL;

    fontinfo->entries = (FFEntry*)ACNEWMEM(length * sizeof(FFEntry));
    if (!fontinfo->entries) {
        ACFREEMEM(fontinfo);
        return NULL;
    }

    fontinfo->length = length;

    return fontinfo;
}

static ACBuffer*
NewBuffer(int size)
{
    ACBuffer* buffer;

    if (size <= 0)
        return NULL;

    buffer = (ACBuffer*)ACNEWMEM(sizeof(ACBuffer));
    if (!buffer)
        return NULL;

    buffer->data = (char*)ACNEWMEM(size);
    if (!buffer->data) {
        ACFREEMEM(buffer);
        return NULL;
    }

    buffer->data[0] = '\0';
    buffer->capacity = size;
    buffer->length = 0;

    return buffer;
}

static void
FreeBuffer(ACBuffer* buffer)
{
    if (!buffer)
        return;

    ACFREEMEM(buffer->data);
    ACFREEMEM(buffer);
}

static int
ParseFontInfo(const char* data, ACFontInfo** fontinfo)
{
    const char *kwstart, *kwend, *tkstart, *current;
    int i;

    ACFontInfo* info = *fontinfo = NewFontInfo(34);
    if (!info)
        return AC_MemoryError;

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
        return AC_Success;

    current = data;
    while (*current) {
        size_t kwLen;
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

        kwLen = (int)(kwend - kwstart);
        for (i = 0; i < info->length; i++) {
            size_t matchLen = NUMMAX(kwLen, strlen(info->entries[i].key));
            if (!strncmp(info->entries[i].key, kwstart, matchLen)) {
                info->entries[i].value = (char*)ACNEWMEM(current - tkstart + 1);
                if (!info->entries[i].value) {
                    FreeFontInfo(info);
                    return AC_MemoryError;
                }
                strncpy(info->entries[i].value, tkstart, current - tkstart);
                info->entries[i].value[current - tkstart] = '\0';
                break;
            }
        }

        if (i == info->length) {
            char* temp;
            temp = (char*)ACNEWMEM(tkstart - kwstart + 1);
            if (!temp) {
                FreeFontInfo(info);
                return AC_MemoryError;
            }
            strncpy(temp, kwstart, tkstart - kwstart);
            temp[tkstart - kwstart] = '\0';
            /*fprintf(stderr, "Ignoring fileinfo %s...\n", temp);*/
            ACFREEMEM(temp);
        }
        skipblanks();
    }

    return AC_Success;
}

ACLIB_API void
AC_SetMemManager(void* ctxptr, AC_MEMMANAGEFUNCPTR func)
{
    setAC_memoryManager(ctxptr, func);
}

ACLIB_API void
AC_SetReportCB(AC_REPORTFUNCPTR reportCB, int verbose)
{
    if (verbose)
        libReportCB = reportCB;
    else
        libReportCB = NULL;

    libErrorReportCB = reportCB;
}

ACLIB_API void
AC_SetReportStemsCB(AC_REPORTSTEMPTR hstemCB, AC_REPORTSTEMPTR vstemCB,
                    unsigned int allStems)
{
    allstems = allStems;
    addHStemCB = hstemCB;
    addVStemCB = vstemCB;
    doStems = 1;

    addCharExtremesCB = NULL;
    addStemExtremesCB = NULL;
    doAligns = 0;
}

ACLIB_API void
AC_SetReportZonesCB(AC_REPORTZONEPTR charCB, AC_REPORTZONEPTR stemCB)
{
    addCharExtremesCB = charCB;
    addStemExtremesCB = stemCB;
    doAligns = 1;

    addHStemCB = NULL;
    addVStemCB = NULL;
    doStems = 0;
}

int
cleanup(int16_t code)
{
    if (code == FATALERROR || code == NONFATALERROR)
        longjmp(aclibmark, -1);
    else
        longjmp(aclibmark, 1);

    return 0; /* we dont actually ever get here */
}

ACLIB_API int
AutoColorString(const char* srcbezdata, const char* fontinfodata,
                char* dstbezdata, int* length, int allowEdit, int allowHintSub,
                int roundCoords, int debug)
{
    int value, result;
    ACFontInfo* fontinfo = NULL;

    if (!srcbezdata)
        return AC_InvalidParameterError;

    if (ParseFontInfo(fontinfodata, &fontinfo))
        return AC_FontinfoParseFail;

    set_errorproc(cleanup);
    value = setjmp(aclibmark);

    if (value == -1) {
        /* a fatal error occurred soemwhere. */
        FreeFontInfo(fontinfo);
        return AC_FatalError;
    } else if (value == 1) {
        /* AutoColor was called succesfully */
        FreeFontInfo(fontinfo);
        if (bezoutput->length < *length) {
            *length = bezoutput->length + 1;
            strncpy(dstbezdata, bezoutput->data, *length);
            FreeBuffer(bezoutput);
            return AC_Success;
        } else {
            *length = bezoutput->length + 1;
            FreeBuffer(bezoutput);
            return AC_DestBuffOfloError;
        }
    }

    bezoutput = NewBuffer(*length);
    if (!bezoutput) {
        FreeFontInfo(fontinfo);
        return AC_MemoryError;
    }

    result = AutoColor(fontinfo,     /* font info */
                       srcbezdata,   /* input glyph */
                       false,        /* fixStems */
                       debug,        /* debug */
                       allowHintSub, /* extracolor*/
                       allowEdit,    /* editChars */
                       roundCoords);
    /* result == true is good */

    /* The following call to cleanup() always returns control to just after the
     * setjmp() function call above, but with value set to 1 if success, or -1
     * if not */
    cleanup((result == true) ? OK : NONFATALERROR);

    /* Shouldn't get here */
    return AC_UnknownError;
}

ACLIB_API void
AC_initCallGlobals(void)
{
    libReportCB = NULL;
    libErrorReportCB = NULL;
    addCharExtremesCB = NULL;
    addStemExtremesCB = NULL;
    doAligns = 0;
    addHStemCB = NULL;
    addVStemCB = NULL;
    doStems = 0;
}

ACLIB_API const char*
AC_getVersion(void)
{
    return libversion;
}
