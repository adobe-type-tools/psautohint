/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. *//* AC_C_lib.c */


#include "setjmp.h"

#include "ac_C_lib.h"
#include "ac.h"
#include "bftoac.h"
#include "machinedep.h"


const char *libversion = "1.6.0";
const char *progname="AC_C_lib";
char editingResource=0;

ACFontInfo *featurefiledata;

char *FL_glyphname=0;

const char *bezstring=0;
char *bezoutput=0;
int bezoutputalloc=0;
int bezoutputactual=0;

bool scalinghints = false;

extern int compatiblemode; /*forces the definition of colors even before a color sub*/

jmp_buf aclibmark;   /* to handle exit() calls in the library version*/


#define skipblanks() while(*current=='\t' || *current=='\n' || *current==' ' || *current=='\r') current++
#define skipnonblanks() while(*current!='\t' && *current!='\n' && *current!=' ' && *current!='\r'&& *current!='\0') current++
#define skipmatrix() while(*current!='\0' && *current!=']') current++

static void skippsstring(const char **current)
{
	int parencount=0;
	
	do
	{
		if(**current=='(')
			parencount++;
		else if(**current==')')
			parencount--;
		else if(**current=='\0')
			return;
			
		(*current)++;
		
	}while(parencount>0);
}

static void
FreeFontInfoArray(ACFontInfo* fontinfo)
{
    int i;

    for (i = 0; i < fontinfo->size; i++) {
        if (fontinfo->entries[i].value[0]) {
            ACFREEMEM(fontinfo->entries[i].value);
        }
    }
    ACFREEMEM(fontinfo->entries);
    ACFREEMEM(fontinfo);
}

static ACFontInfo*
NewFontInfoArray(int size)
{
    ACFontInfo* fontinfo = (ACFontInfo*)ACNEWMEM(sizeof(ACFontInfo));
    fontinfo->size = size;
    fontinfo->entries = (FFEntry*)ACNEWMEM(size * sizeof(FFEntry));
    return fontinfo;
}

static int
ParseFontInfo(const char* fontinfo)
{
    const char *kwstart, *kwend, *tkstart, *current;
    int i;

    featurefiledata = NewFontInfoArray(34);
    featurefiledata->entries[0].key = "OrigEmSqUnits";
    featurefiledata->entries[1].key = "FontName";
    featurefiledata->entries[2].key = "FlexOK";
    /* Blue Values */
    featurefiledata->entries[3].key = "BaselineOvershoot";
    featurefiledata->entries[4].key = "BaselineYCoord";
    featurefiledata->entries[5].key = "CapHeight";
    featurefiledata->entries[6].key = "CapOvershoot";
    featurefiledata->entries[7].key = "LcHeight";
    featurefiledata->entries[8].key = "LcOvershoot";
    featurefiledata->entries[9].key = "AscenderHeight";
    featurefiledata->entries[10].key = "AscenderOvershoot";
    featurefiledata->entries[11].key = "FigHeight";
    featurefiledata->entries[12].key = "FigOvershoot";
    featurefiledata->entries[13].key = "Height5";
    featurefiledata->entries[14].key = "Height5Overshoot";
    featurefiledata->entries[15].key = "Height6";
    featurefiledata->entries[16].key = "Height6Overshoot";
    /* Other Values */
    featurefiledata->entries[17].key = "Baseline5Overshoot";
    featurefiledata->entries[18].key = "Baseline5";
    featurefiledata->entries[19].key = "Baseline6Overshoot";
    featurefiledata->entries[20].key = "Baseline6";
    featurefiledata->entries[21].key = "SuperiorOvershoot";
    featurefiledata->entries[22].key = "SuperiorBaseline";
    featurefiledata->entries[23].key = "OrdinalOvershoot";
    featurefiledata->entries[24].key = "OrdinalBaseline";
    featurefiledata->entries[25].key = "DescenderOvershoot";
    featurefiledata->entries[26].key = "DescenderHeight";

    featurefiledata->entries[27].key = "DominantV";
    featurefiledata->entries[28].key = "StemSnapV";
    featurefiledata->entries[29].key = "DominantH";
    featurefiledata->entries[30].key = "StemSnapH";
    featurefiledata->entries[31].key = "VCounterChars";
    featurefiledata->entries[32].key = "HCounterChars";
    /* later addenda */
    featurefiledata->entries[33].key = "BlueFuzz";

    for (i = 0; i < featurefiledata->size; i++) {
        featurefiledata->entries[i].value = "";
    }

    if (!fontinfo)
        return AC_Success;

    current = fontinfo;
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
        for (i = 0; i < featurefiledata->size; i++) {
            size_t matchLen =
              NUMMAX(kwLen, strlen(featurefiledata->entries[i].key));
            if (!strncmp(featurefiledata->entries[i].key, kwstart, matchLen)) {
                featurefiledata->entries[i].value =
                  (char*)ACNEWMEM(current - tkstart + 1);
                if (!featurefiledata->entries[i].value)
                    return AC_MemoryError;
                strncpy(featurefiledata->entries[i].value, tkstart,
                        current - tkstart);
                featurefiledata->entries[i].value[current - tkstart] = '\0';
                break;
            }
        }
        if (i == featurefiledata->size) {
            char* temp;
            temp = (char*)ACNEWMEM(tkstart - kwstart + 1);
            if (!temp)
                return AC_MemoryError;
            strncpy(temp, kwstart, tkstart - kwstart);
            temp[tkstart - kwstart] = '\0';
            /*fprintf(stderr, "Ignoring fileinfo %s...\n", temp);*/
            ACFREEMEM(temp);
        }
        skipblanks();
    }

    return AC_Success;
}

ACLIB_API void  AC_SetMemManager(void *ctxptr, AC_MEMMANAGEFUNCPTR func)
{
	setAC_memoryManager(ctxptr, func);
}

ACLIB_API void  AC_SetReportCB(AC_REPORTFUNCPTR reportCB, int verbose)
{
	if (verbose)
		libReportCB = reportCB;
	else
		libReportCB = NULL;
	
	libErrorReportCB = reportCB;
}


ACLIB_API void  AC_SetReportStemsCB(AC_REPORTSTEMPTR hstemCB, AC_REPORTSTEMPTR vstemCB, unsigned int allStems)
{
	allstems = allStems;
	addHStemCB = hstemCB;
	addVStemCB = vstemCB;
	doStems = 1;

	addCharExtremesCB = NULL;
	addStemExtremesCB = NULL;
	doAligns = 0;
}

ACLIB_API void  AC_SetReportZonesCB(AC_REPORTZONEPTR charCB, AC_REPORTZONEPTR stemCB)
{
	addCharExtremesCB = charCB;
	addStemExtremesCB = stemCB;
	doAligns = 1;

	addHStemCB = NULL;
	addVStemCB = NULL;
	doStems = 0;
}


int cleanup(int16_t code)
{
	if (code==FATALERROR || code==NONFATALERROR)
		longjmp(aclibmark, -1);
	else
		longjmp(aclibmark, 1);

	return 0; /* we dont actually ever get here */
}

ACLIB_API int
AutoColorString(const char* srcbezdata, const char* fontinfo, char* dstbezdata,
                int* length, int allowEdit, int allowHintSub, int roundCoords,
                int debug)
{
    int value, result;

    if (!srcbezdata)
        return AC_InvalidParameterError;

    if (ParseFontInfo(fontinfo))
        return AC_FontinfoParseFail;

    set_errorproc(cleanup);
    value = setjmp(aclibmark);

    if (value == -1) {
        /* a fatal error occurred soemwhere. */
        FreeFontInfoArray(featurefiledata);
        return AC_FatalError;
    } else if (value == 1) {
        /* AutoColor was called succesfully */
        FreeFontInfoArray(featurefiledata);
        if (bezoutputactual < *length) {
            strncpy(dstbezdata, bezoutput, bezoutputactual + 1);
            *length = bezoutputactual + 1;
            ACFREEMEM(bezoutput);
            bezoutputalloc = 0;
            return AC_Success;
        } else {
            *length = bezoutputactual + 1;
            ACFREEMEM(bezoutput);
            bezoutputalloc = 0;
            return AC_DestBuffOfloError;
        }
    }

    bezstring = srcbezdata;

    bezoutputalloc = *length;
    bezoutputactual = 0;
    bezoutput = (char*)ACNEWMEM(bezoutputalloc);
    if (!bezoutput) {
        FreeFontInfoArray(featurefiledata);
        return AC_MemoryError;
    }
    *bezoutput = 0;

    result = AutoColor(false,        /*fixStems*/
                       (bool)debug,  /*debug*/
                       allowHintSub, /* extracolor*/
                       allowEdit,    /*editChars*/
                       roundCoords);
    /* result == true is good */
    /* The following call to cleanup() always returns control to just after the
    setjmp() function call above,,
    but with value set to 1 if success, or -1 if not */
    cleanup((result == true) ? OK : NONFATALERROR);

    return AC_UnknownError; /*Shouldn't get here*/
}

ACLIB_API void AC_initCallGlobals(void)
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

ACLIB_API const char *AC_getVersion(void)
{
	return libversion;
}
