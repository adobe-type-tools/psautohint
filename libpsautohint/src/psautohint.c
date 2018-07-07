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
#include "psautohint.h"
#include "version.h"

ACBuffer* gBezOutput = NULL;

bool gScalingHints = false;

static jmp_buf aclibmark; /* to handle exit() calls in the library version*/

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

static ACBuffer*
NewBuffer(size_t size)
{
    ACBuffer* buffer;

    if (size == 0)
        return NULL;

    buffer = (ACBuffer*)AllocateMem(1, sizeof(ACBuffer), "out buffer");
    if (!buffer)
        return NULL;

    buffer->data = AllocateMem(size, 1, "out buffer data");
    if (!buffer->data) {
        UnallocateMem(buffer);
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

    UnallocateMem(buffer->data);
    UnallocateMem(buffer);
}

static int
ParseFontInfo(const char* data, ACFontInfo** fontinfo)
{
    const char *current;
    size_t i;

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
                    return AC_MemoryError;
                }
                strncpy(info->entries[i].value, tkstart, current - tkstart);
                info->entries[i].value[current - tkstart] = '\0';
                break;
            }
        }

        if (i == info->length) {
            char* temp;
            temp = AllocateMem(tkstart - kwstart + 1, 1, "no idea!");
            if (!temp) {
                FreeFontInfo(info);
                return AC_MemoryError;
            }
            strncpy(temp, kwstart, tkstart - kwstart);
            temp[tkstart - kwstart] = '\0';
            /*fprintf(stderr, "Ignoring fileinfo %s...\n", temp);*/
            UnallocateMem(temp);
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
        gLibReportCB = reportCB;
    else
        gLibReportCB = NULL;

    gLibErrorReportCB = reportCB;
}

ACLIB_API void
AC_SetReportStemsCB(AC_REPORTSTEMPTR hstemCB, AC_REPORTSTEMPTR vstemCB,
                    unsigned int allStems)
{
    gAllStems = allStems;
    gAddHStemCB = hstemCB;
    gAddVStemCB = vstemCB;
    gDoStems = true;

    gAddCharExtremesCB = NULL;
    gAddStemExtremesCB = NULL;
    gDoAligns = false;
}

ACLIB_API void
AC_SetReportZonesCB(AC_REPORTZONEPTR charCB, AC_REPORTZONEPTR stemCB)
{
    gAddCharExtremesCB = charCB;
    gAddStemExtremesCB = stemCB;
    gDoAligns = true;

    gAddHStemCB = NULL;
    gAddVStemCB = NULL;
    gDoStems = false;
}

ACLIB_API void
AC_SetReportRetryCB(AC_RETRYPTR retryCB)
{
    gReportRetryCB = retryCB;
}

/*
 * This is our error handler, it gets called by LogMsg() whenever the log level
 * is LOGERROR (see logging.c for the exact condition). The call to longjmp()
 * will transfer the control to the point where setjmp() is called below. So
 * effectively whenever LogMsg() is called for an error the execution of the
 * calling function will end and we will return back to AutoColorString().
 */
static int
error_handler(int16_t code)
{
    if (code == FATALERROR || code == NONFATALERROR)
        longjmp(aclibmark, -1);
    else
        longjmp(aclibmark, 1);

    return 0; /* we don't actually ever get here */
}

ACLIB_API int
AutoColorString(const char* srcbezdata, const char* fontinfodata,
                char** dstbezdata, size_t* length, int allowEdit,
                int allowHintSub, int roundCoords, int debug)
{
    int value, result;
    ACFontInfo* fontinfo = NULL;

    if (!srcbezdata)
        return AC_InvalidParameterError;

    if (ParseFontInfo(fontinfodata, &fontinfo))
        return AC_FontinfoParseFail;

    set_errorproc(error_handler);
    value = setjmp(aclibmark);

    /* We will return here whenever an error occurs during the execution of
     * AutoColor(), or after it finishes execution. See the error_handler
     * comments above and below. */

    if (value == -1) {
        /* a fatal error occurred somewhere. */
        FreeFontInfo(fontinfo);
        return AC_FatalError;
    } else if (value == 1) {
        /* AutoColor was called successfully */
        FreeFontInfo(fontinfo);

        if (gBezOutput->length >= *length)
            *dstbezdata = ReallocateMem(*dstbezdata, gBezOutput->length + 1, "Output buffer");

        *length = gBezOutput->length + 1;
        strncpy(*dstbezdata, gBezOutput->data, *length);

        FreeBuffer(gBezOutput);

        return AC_Success;
    }

    gBezOutput = NewBuffer(*length);
    if (!gBezOutput) {
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

    /* The following call to error_handler() always returns control to just
     * after the setjmp() function call above, but with value set to 1 if
     * success, or -1 if not */
    error_handler((result == true) ? OK : NONFATALERROR);

    /* Shouldn't get here */
    return AC_UnknownError;
}

ACLIB_API int
AutoColorStringMM(const char** srcbezdata, const char* fontinfodata,
                  int nmasters, const char** masters, char** dstbezdata,
                  size_t* lengths)
{
    /* Only the master with index 'hintsMasterIx' needs to be hinted; this is
     * why only the fontinfo data for that master is needed. This function
     * expects that the master with index 'hintsMasterIx' has already been
     * hinted with AutoColor().
     *
     * The hints for the others masters are derived a very simple process. When
     * the first master was hinted, the logic recorded the path element index
     * for the path element which sets the inner or outer edge of each hint,
     * and whether it is the endpoint or startpoint which sets the edge. For
     * each other master, the logic gets the path element for the current
     * master using the same path element index as in the first master, and
     * uses the end or start point of that path element to set the edge in the
     * current master.
     *
     * Some code notes: The original hinting pass in AutoColorString() on the
     * first master records the path indicies for each path element that sets a
     * hint edge, and whether it is a start or end point; this stored in the
     * hintElt structures in the first master. There is a hintElt for the main
     * (default) hints at the start of the charstring, and there is hintElt
     * structure attached to each path element which triggers a new hint set.
     * The function charpath.c::ReadandAssignHints(), called from
     * charpath.c::MergeCharPaths(), then copies all the hintElts to the
     * current master main or path elements. (This actually happens in
     * charpath.c::InsertHint().) */
    int value, result;
    ACFontInfo* fontinfo = NULL;

    if (!srcbezdata)
        return AC_InvalidParameterError;

    if (ParseFontInfo(fontinfodata, &fontinfo))
        return AC_FontinfoParseFail;

    set_errorproc(error_handler);
    value = setjmp(aclibmark);

    /* We will return here whenever an error occurs during the execution of
     * AutoColor(), or after it finishes execution. See the error_handler
     * comments above and below. */

    if (value == -1) {
        /* a fatal error occurred somewhere. */
        FreeFontInfo(fontinfo);
        return AC_FatalError;
    } else if (value == 1) {
        /* AutoColor was called successfully */
        FreeFontInfo(fontinfo);
        return AC_Success;
    }

    /* result == true is good */
    result = MergeCharPaths(fontinfo, srcbezdata, nmasters, masters, dstbezdata,
                            lengths);

    /* The following call to error_handler() always returns control to just
     * after the setjmp() function call above, but with value set to 1 if
     * success, or -1 if not */
    error_handler((result == true) ? OK : NONFATALERROR);

    /* Shouldn't get here */
    return AC_UnknownError;
}

ACLIB_API void
AC_initCallGlobals(void)
{
    gLibReportCB = NULL;
    gLibErrorReportCB = NULL;
    gAddCharExtremesCB = NULL;
    gAddStemExtremesCB = NULL;
    gDoAligns = false;
    gAddHStemCB = NULL;
    gAddVStemCB = NULL;
    gDoStems = false;
}

ACLIB_API const char*
AC_getVersion(void)
{
    return PSAUTOHINT_VERSION;
}
