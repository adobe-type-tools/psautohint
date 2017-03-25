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
FreeFontInfo(ACFontInfo* fontinfo)
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
NewFontInfo(int size)
{
    ACFontInfo* fontinfo = (ACFontInfo*)ACNEWMEM(sizeof(ACFontInfo));
    fontinfo->size = size;
    fontinfo->entries = (FFEntry*)ACNEWMEM(size * sizeof(FFEntry));
    return fontinfo;
}

static ACFontInfo*
ParseFontInfo(const char* data)
{
    const char *kwstart, *kwend, *tkstart, *current;
    int i;

    ACFontInfo* fontinfo = NewFontInfo(34);

    fontinfo->entries[0].key = "OrigEmSqUnits";
    fontinfo->entries[1].key = "FontName";
    fontinfo->entries[2].key = "FlexOK";
    /* Blue Values */
    fontinfo->entries[3].key = "BaselineOvershoot";
    fontinfo->entries[4].key = "BaselineYCoord";
    fontinfo->entries[5].key = "CapHeight";
    fontinfo->entries[6].key = "CapOvershoot";
    fontinfo->entries[7].key = "LcHeight";
    fontinfo->entries[8].key = "LcOvershoot";
    fontinfo->entries[9].key = "AscenderHeight";
    fontinfo->entries[10].key = "AscenderOvershoot";
    fontinfo->entries[11].key = "FigHeight";
    fontinfo->entries[12].key = "FigOvershoot";
    fontinfo->entries[13].key = "Height5";
    fontinfo->entries[14].key = "Height5Overshoot";
    fontinfo->entries[15].key = "Height6";
    fontinfo->entries[16].key = "Height6Overshoot";
    /* Other Values */
    fontinfo->entries[17].key = "Baseline5Overshoot";
    fontinfo->entries[18].key = "Baseline5";
    fontinfo->entries[19].key = "Baseline6Overshoot";
    fontinfo->entries[20].key = "Baseline6";
    fontinfo->entries[21].key = "SuperiorOvershoot";
    fontinfo->entries[22].key = "SuperiorBaseline";
    fontinfo->entries[23].key = "OrdinalOvershoot";
    fontinfo->entries[24].key = "OrdinalBaseline";
    fontinfo->entries[25].key = "DescenderOvershoot";
    fontinfo->entries[26].key = "DescenderHeight";

    fontinfo->entries[27].key = "DominantV";
    fontinfo->entries[28].key = "StemSnapV";
    fontinfo->entries[29].key = "DominantH";
    fontinfo->entries[30].key = "StemSnapH";
    fontinfo->entries[31].key = "VCounterChars";
    fontinfo->entries[32].key = "HCounterChars";
    /* later addenda */
    fontinfo->entries[33].key = "BlueFuzz";

    for (i = 0; i < fontinfo->size; i++) {
        fontinfo->entries[i].value = "";
    }

    if (!data)
        return fontinfo;

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
        for (i = 0; i < fontinfo->size; i++) {
            size_t matchLen = NUMMAX(kwLen, strlen(fontinfo->entries[i].key));
            if (!strncmp(fontinfo->entries[i].key, kwstart, matchLen)) {
                fontinfo->entries[i].value =
                  (char*)ACNEWMEM(current - tkstart + 1);
                if (!fontinfo->entries[i].value) {
                    FreeFontInfo(fontinfo);
                    return NULL;
                }
                strncpy(fontinfo->entries[i].value, tkstart, current - tkstart);
                fontinfo->entries[i].value[current - tkstart] = '\0';
                break;
            }
        }

        if (i == fontinfo->size) {
            char* temp;
            temp = (char*)ACNEWMEM(tkstart - kwstart + 1);
            if (!temp) {
                FreeFontInfo(fontinfo);
                return NULL;
            }
            strncpy(temp, kwstart, tkstart - kwstart);
            temp[tkstart - kwstart] = '\0';
            /*fprintf(stderr, "Ignoring fileinfo %s...\n", temp);*/
            ACFREEMEM(temp);
        }
        skipblanks();
    }

    return fontinfo;
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
AutoColorString(const char* srcbezdata, const char* fontinfodata,
                char* dstbezdata, int* length, int allowEdit, int allowHintSub,
                int roundCoords, int debug)
{
    int value, result;
    ACFontInfo* fontinfo = NULL;

    if (!srcbezdata)
        return AC_InvalidParameterError;

    fontinfo = ParseFontInfo(fontinfodata);
    if (!fontinfo)
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
        FreeFontInfo(fontinfo);
        return AC_MemoryError;
    }
    *bezoutput = 0;

    result = AutoColor(fontinfo,     /* font info */
                       false,        /*fixStems*/
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
