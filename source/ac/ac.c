/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/
/* ac.c */


#include "ac.h"
#include "bftoac.h"
#include "machinedep.h"
#include "fipublic.h"

#ifndef _WIN32
extern int unlink(const char *);
#endif

#define MAXSTEMDIST 150  /* initial maximum stem width allowed for hints */

#if ALLOWCSOUTPUT && THISISACMAIN
extern bool charstringoutput;
#endif

PPathElt pathStart, pathEnd;
bool YgoesUp;
bool useV, useH, autoVFix, autoHFix, autoLinearCurveFix, editChar;
bool AutoExtraDEBUG, debugColorPath, DEBUG, logging;
bool showVs, showHs, listClrInfo;
bool reportErrors, hasFlex, flexOK, flexStrict, showClrInfo, bandError;
Fixed hBigDist, vBigDist, initBigDist, minDist, minMidPt, ghostWidth,
  ghostLength, bendLength, bandMargin, maxFlare,
  maxBendMerge, maxMerge, minColorElementLength, flexCand,
  pruneMargin;
Fixed pruneA, pruneB, pruneC, pruneD, pruneValue, bonus;
real theta, hBigDistR, vBigDistR, maxVal, minVal;
integer lenTopBands, lenBotBands, numSerifs, DMIN, DELTA, CPpercent;
integer bendTan, sCurveTan;
PClrVal Vcoloring, Hcoloring, Vprimary, Hprimary, valList;
char * fileName;
char outPrefix[MAXPATHLEN];
char inPrefix[MAXPATHLEN];
PClrSeg segLists[4];
Fixed VStems[MAXSTEMS], HStems[MAXSTEMS];
integer NumVStems, NumHStems;
Fixed topBands[MAXBLUES], botBands[MAXBLUES], serifs[MAXSERIFS];
PClrPoint pointList, *ptLstArray;
integer ptLstIndex, numPtLsts, maxPtLsts;
bool makehintslog = true;
bool writecoloredbez = true;
Fixed bluefuzz;
bool doAligns, doStems;
bool idInFile;
bool roundToInt;
static int maxStemDist = MAXSTEMDIST;


AC_REPORTFUNCPTR libReportCB = NULL;
AC_REPORTFUNCPTR libErrorReportCB = NULL;
unsigned int  allstems; /* if false, then stems defined by curves are excluded from the reporting */
AC_REPORTSTEMPTR addHStemCB = NULL;
AC_REPORTSTEMPTR addVStemCB = NULL;
AC_REPORTZONEPTR addCharExtremesCB = NULL;
AC_REPORTZONEPTR addStemExtremesCB = NULL;
AC_RETRYPTR reportRetryCB = NULL;
#define CHARSETVAR "CHARSETDIR"
#define BAKFILE "hints.log.BAK"



static void * defaultAC_memmanage(void *ctxptr, void *old, uint32_t size)
	{
#ifndef _WIN32
#pragma unused(ctxptr)	
#endif
		if (size > 0)
		{
		if (NULL == old)
			{
			return malloc((size_t)size);
			}
		else
			{
			return realloc(old, (size_t)size);
			}
		}
	else
		{
		if (NULL == old)
			return NULL;
		else
			{
			free(old);
			return NULL;
			}
		}
	}

	/* SEE MACROS in ac.h:
 ACNEWMEM(size) 
 ACREALLOCMEM(oldptr, newsize) 
 ACFREEMEM(ptr) 
	*/

AC_MEMMANAGEFUNCPTR AC_memmanageFuncPtr = defaultAC_memmanage;
void *AC_memmanageCtxPtr = NULL;

void setAC_memoryManager(void *ctxptr, AC_MEMMANAGEFUNCPTR func)
	{
	AC_memmanageFuncPtr = func;
	AC_memmanageCtxPtr = ctxptr;
	}


#define VMSIZE (1000000)
static unsigned char *vmfree, *vmlast, vm[VMSIZE];

/* sub allocator */
unsigned char * Alloc(integer sz)
	{
	unsigned char * s;
	sz = (sz + 3) & ~3; /* make size a multiple of 4 */
	s = vmfree;
	vmfree += sz;
	if (vmfree > vmlast) /* Error! need to make VMSIZE bigger */
		{
		FlushLogFiles();
		sprintf (globmsg, "Exceeded VM size for hints in file: %s.\n",
				 fileName);
		LogMsg(globmsg, LOGERROR, FATALERROR, true);
		}
	return s;
  }


void InitData(integer reason)
 {
  register char *s;
  real tmp, origEmSquare;

  switch (reason) {
    case STARTUP:
      DEBUG = false;
	  DMIN = 50;
      DELTA = 0;
      YgoesUp = (dtfmy(FixOne) > 0) ? true : false;
      initBigDist = PSDist(maxStemDist);
      /* must be <= 168 for ITC Garamond Book Italic p, q, thorn */
      minDist = PSDist(7);
      ghostWidth = PSDist(20);
      ghostLength = PSDist(4);
      bendLength = PSDist(2);
      bendTan = 577; /* 30 sin 30 cos div abs == .57735 */
      theta = (float).38; /* must be <= .38 for Ryumin-Light-32 c49*/
      pruneA = FixInt(50);
      pruneC = 100;
      pruneD = FixOne;
      tmp = (float)10.24; /* set to 1024 times the threshold value */
      pruneValue = pruneB = acpflttofix(&tmp);
        /* pruneB must be <= .01 for Yakout/Light/heM */
        /* pruneValue must be <= .01 for Yakout/Light/heM */
      CPpercent = 40;
        /* must be < 46 for Americana-Bold d bowl vs stem coloring */
      bandMargin = PSDist(30);
      maxFlare = PSDist(10);
      pruneMargin = PSDist(10);
      maxBendMerge = PSDist(6);
      maxMerge = PSDist(2); /* must be < 3 for Cushing-BookItalic z */
      minColorElementLength = PSDist(12);
      flexCand = PSDist(4);
      sCurveTan = 25;
      maxVal = 8000000.0;
      minVal = 1.0 / (real)(FixOne);
      autoHFix = autoVFix = false;
      editChar = true;
      roundToInt = true;
      /* Default is to change a curve with collinear points into a line. */
      autoLinearCurveFix = true;
      flexOK = false;
      flexStrict = true;
      AutoExtraDEBUG = DEBUG;
      logging = DEBUG;
      debugColorPath = false;
      showClrInfo = DEBUG;
      showHs = showVs = DEBUG;
      listClrInfo = DEBUG;
      if (scalinghints)
      {
        SetFntInfoFileName (SCALEDHINTSINFO);
        s = GetFntInfo("OrigEmSqUnits", MANDATORY);
        sscanf(s, "%g", &origEmSquare);
		ACFREEMEM(s);
        bluefuzz = (Fixed) (origEmSquare / 2000.0); /* .5 pixel */
      }
      else 
      {
        ResetFntInfoFileName();
        bluefuzz = DEFAULTBLUEFUZZ;
     }
      /* fall through */
    case RESTART:
		memset((void *)vm, 0x0, VMSIZE);
		vmfree = vm; vmlast = vm + VMSIZE;

		/* ?? Does this cause a leak ?? */
      pointList = NULL;
      maxPtLsts = 5;
      ptLstArray = (PClrPoint *)Alloc(maxPtLsts*sizeof(PClrPoint));
      ptLstIndex = 0;
      ptLstArray[0] = NULL;
      numPtLsts = 1;

/*     if (fileName != NULL && fileName[0] == 'g')
       showClrInfo = showHs = showVs = listClrInfo = true; */
    }
  }


static void setOutputPrefix (prefix)
char *prefix;
{
  get_filename(outPrefix, prefix, "");
}

void SetMaxStemDist(int dist)
{
  maxStemDist = dist;
}


/* Returns whether coloring was successful. */  
bool AutoColor(
			 bool release,
			 bool fixStems,
			 bool debug,
			 bool extracolor,
			 bool changeChar,
			 int16_t total_files,
			 char *fileNamePtr[],
			 bool quiet,
             bool doAll,
             bool roundCoords,
			 bool doLog)
{
  bool result, renameLog = false;
  char *tempstring;

	makehintslog=doLog;
  (void) InitAll(STARTUP);

#if ALLOWCSOUTPUT && THISISACMAIN
  if (charstringoutput) {
	setOutputPrefix("CharString");
	DirExists("CharString", false, true, false);
  }
#endif
  if (!ReadFontInfo()) return false;
  editChar = changeChar;
  roundToInt = roundCoords;
  if ((editChar) && fixStems)
    autoVFix = autoHFix = fixStems;
  autoLinearCurveFix = editChar;
  if (debug)
    DEBUG = showClrInfo = showHs = showVs = listClrInfo = true;
  if ((tempstring = getenv(CHARSETVAR)) != NULL)
    set_charsetdir(tempstring);
  else set_charsetdir("\0");
  if(doLog)
	  OpenLogFiles();
  /* It is possible to do both DoArgs and DoAll when called from
     BuildFont.  The derived character names are hinted by calling
     DoArgs since they should always be rehinted, even in release mode. */
  if (total_files > 0) /* user specified file names */
    {
    	if (fileNamePtr[0][0]=='\0')
    		result = DoArgsIgnoreTime(total_files, fileNamePtr, extracolor, &renameLog, release);
    
    }
#ifndef IS_GGL
	if (doAll)
   	 if (scalinghints)
      result = DoAllIgnoreTime(extracolor, IncludeFile);
#endif

  return(result);
}

