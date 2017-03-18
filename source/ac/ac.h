/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */


/* See the discussion in the function definition for:
 control.c:Blues() 
 static void Blues() 
 */
#include "ac_C_lib.h"
#include "buildfont.h"


/* widely used definitions */


#if THISISACMAIN
#define ALLOWCSOUTPUT 1
#else
#define ALLOWCSOUTPUT 0
#endif

#if defined(THINK_C)
#define L_cuserid 20 /*ASP: was defined stdio.h*/
#endif


/* number of default entries in counter color character list. */
#define COUNTERDEFAULTENTRIES 4
#define COUNTERLISTSIZE 64

/* values for ClrSeg.sType */
#define sLINE (0L)
#define sBEND (1L)
#define sCURVE (2L)
#define sGHOST (3L)

/* values for PathElt.type */
#define MOVETO (0L)
#define LINETO (1L)
#define CURVETO (2L)
#define CLOSEPATH (3L)

/* values for pathelt control points */
#define cpStart (0L)
#define cpCurve1 (1L)
#define cpCurve2 (2L)
#define cpEnd (3L)

/* widths of ghost bands */
#define botGhst (-21L)
#define topGhst (-20L)

/* structures */

typedef struct {
  short int limit;
  Fixed feps;
  void (*report)();
  Cd ll, ur;
  Fixed llx, lly;
  } FltnRec, *PFltnRec;

typedef struct _clrseg {
  struct _clrseg *sNxt;
    /* points to next ClrSeg in list */
    /* separate lists for top, bottom, left, and right segments */
  Fixed sLoc, sMax, sMin;
    /* sLoc is X loc for vertical seg, Y loc for horizontal seg */
    /* sMax and sMin give Y extent for vertical seg, X extent for horizontal */
    /* i.e., sTop=sMax, sBot=sMin, sLft=sMin, sRght=sMax. */
  Fixed sBonus;
    /* nonzero for segments in sol-eol subpaths */
    /* (probably a leftover that is no longer needed) */
  struct _clrval *sLnk;
    /* points to the best ClrVal that uses this ClrSeg */
    /* set by FindBestValForSegs in pick.c */
  struct _pthelt *sElt;
    /* points to the path element that generated this ClrSeg */
    /* set by AddSegment in gen.c */
  short int sType;
    /* tells what type of segment this is: sLINE sBEND sCURVE or sGHOST */
  } ClrSeg, *PClrSeg;

typedef struct _seglnk {
  PClrSeg seg;
  } SegLnk, *PSegLnk;

typedef struct _seglnklst {
  struct _seglnklst *next;
  PSegLnk lnk;
  } SegLnkLst;

typedef SegLnkLst *PSegLnkLst;

#if 0
typedef struct _clrrep {
  Fixed vVal, vSpc, vLoc1, vLoc2;
  struct _clrval *vBst;
  } ClrRep, *PClrRep;

typedef struct _clrval {
  struct _clrval *vNxt;
  Fixed vVal, vSpc, initVal;
  Fixed vLoc1, vLoc2;
    /* vBot=vLoc1, vTop=vLoc2, vLft=vLoc1, vRght=vLoc2 */ 
  short int vGhst:8;
  short int pruned:8;
  PClrSeg vSeg1, vSeg2;
  struct _clrval *vBst;
  PClrRep vRep;
  } ClrVal, *PClrVal;
#else
typedef struct _clrval {
  struct _clrval *vNxt;
    /* points to next ClrVal in list */
  Fixed vVal, vSpc, initVal;
    /* vVal is value given in eval.c */
    /* vSpc is nonzero for "special" ClrVals */
       /* such as those with a segment in a blue zone */
    /* initVal is the initially assigned value */
       /* used by FndBstVal in pick.c */
  Fixed vLoc1, vLoc2;
    /* vLoc1 is location corresponding to vSeg1 */
    /* vLoc2 is location corresponding to vSeg2 */
    /* for horizontal ClrVal, vBot=vLoc1 and vTop=vLoc2 */
    /* for vertical ClrVal, vLft=vLoc1 and vRght=vLoc2 */
  unsigned int vGhst:1;  /* true iff one of the ClrSegs is a sGHOST seg */
  unsigned int pruned:1;
    /* flag used by FindBestHVals and FindBestVVals */ 
    /* and by PruneVVals and PruneHVals */
  unsigned int merge:1;
    /* flag used by ReplaceVals in merge.c */
  unsigned int unused:13;
  PClrSeg vSeg1, vSeg2;
    /* vSeg1 points to the left ClrSeg in a vertical, bottom in a horizontal */
    /* vSeg2 points to the right ClrSeg in a vertical, top in a horizontal */
  struct _clrval *vBst;
    /* points to another ClrVal if this one has been merged or replaced */
  } ClrVal, *PClrVal;
#endif

typedef struct _pthelt {
  struct _pthelt *prev, *next, *conflict;
  short int type;
  PSegLnkLst Hs, Vs;
  boolean Hcopy:1, Vcopy:1, isFlex:1, yFlex:1, newCP:1, sol:1, eol:1;
  int unused:9;
  short int count, newcolors;
  Fixed x, y, x1, y1, x2, y2, x3, y3;
  } PathElt, *PPathElt;

typedef struct _clrpnt {
  struct _clrpnt *next;
  Fixed x0, y0, x1, y1;
    /* for vstem, only interested in x0 and x1 */
    /* for hstem, only interested in y0 and y1 */
  PPathElt p0, p1;
    /* p0 is source of x0,y0; p1 is source of x1,y1 */
  char c;
    /* tells what kind of coloring: 'b' 'y' 'm' or 'v' */
  boolean done;
  } ClrPoint, *PClrPoint;

typedef struct {
	char *key, *value;
} FFEntry;

/* global data */

#ifdef IS_LIB
extern FFEntry *featurefiledata;
extern int featurefilesize;

#elif defined( AC_C_LIB)
extern FFEntry *featurefiledata;
extern int featurefilesize;
#endif

extern PPathElt pathStart, pathEnd;
extern boolean YgoesUp;
extern boolean useV, useH, autoVFix, autoHFix, autoLinearCurveFix;
extern boolean AutoExtraDEBUG, debugColorPath, DEBUG, logging;
extern boolean editChar; /* whether character can be modified when adding hints */
extern boolean scalehints;
extern boolean showHs, showVs, bandError, listClrInfo;
extern boolean reportErrors, hasFlex, flexOK, flexStrict, showClrInfo;
extern Fixed hBigDist, vBigDist, initBigDist, minDist, minMidPt, ghostWidth,
  ghostLength, bendLength, bandMargin, maxFlare,
  maxBendMerge, maxMerge, minColorElementLength, flexCand,
  pruneMargin;
extern Fixed pruneA, pruneB, pruneC, pruneD, pruneValue, bonus;
extern real theta, hBigDistR, vBigDistR, maxVal, minVal;
extern integer DMIN, DELTA, CPpercent, bendTan, sCurveTan;
extern PClrVal Vcoloring, Hcoloring, Vprimary, Hprimary, valList;
extern char * fileName;
extern char outPrefix[MAXPATHLEN], *outSuffix;
extern char inPrefix[MAXPATHLEN], *inSuffix;
extern PClrSeg segLists[4]; /* left, right, top, bot */
extern PClrPoint pointList, *ptLstArray;
extern integer ptLstIndex, numPtLsts, maxPtLsts;
extern void AddStemExtremes(Fixed bot, Fixed top);

#define leftList (segLists[0])
#define rightList (segLists[1])
#define topList (segLists[2])
#define botList (segLists[3])

#define MAXFLEX (PSDist(20))
#define MAXBLUES (20)
#define MAXSERIFS (5)
extern Fixed topBands[MAXBLUES], botBands[MAXBLUES], serifs[MAXSERIFS];
extern integer lenTopBands, lenBotBands, numSerifs;
#define MAXSTEMS (20)
extern Fixed VStems[MAXSTEMS], HStems[MAXSTEMS];
extern integer NumVStems, NumHStems;
extern char *HColorList[], *VColorList[];
extern integer NumHColors, NumVColors;
extern boolean makehintslog;
extern boolean writecoloredbez;
extern Fixed bluefuzz;
extern boolean doAligns, doStems;
extern boolean idInFile;
extern boolean roundToInt;
extern char bezGlyphName[64]; /* defined in read.c; set from the glyph name at the start of the bex file. */

/* macros */

#define ac_abs(a) abs(a)

#define FixedPosInf MAXinteger
#define FixedNegInf MINinteger
#define FixShift (8)
#define FixInt(i) (((long int)(i)) << FixShift)
#define FixReal(i) ((long int)((i) *256.0))
extern long int FRnd(long int x);
#define FHalfRnd(x) ((long int)(((x)+(1<<7)) & ~0xFFL))
#define FracPart(x) ((long int)(x) & 0xFFL)
#define FTrunc(x) (((long int)(x))>>FixShift)
#define FIXED2FLOAT(x) (x/256.0)
#if SUN
#ifndef MAX
#define MAX(a,b) ((a) >= (b)? (a) : (b))
#endif
#ifndef MIN
#define MIN(a,b) ((a) <= (b)? (a) : (b))
#endif
#endif

#define FixOne (0x100L)
#define FixTwo (0x200L)
#define FixHalf (0x80L)
#define FixQuarter (0x40L)
#define FixHalfMul(f) (2*((f) >> 2)) /* DEBUG 8 BIT. Revert this to ((f) >>1) once I am confident that there are not bugs from the update to 8 bits for the Fixed fraction. */
#define FixTwoMul(f) ((f) << 1)
#define tfmx(x) ((x))
#define tfmy(y) (-(y))
#define itfmx(x) ((x))
#define itfmy(y) (-(y))
#define dtfmx(x) ((x))
#define dtfmy(y) (-(y))
#define idtfmx(x) ((x))
#define idtfmy(y) (-(y))
#define PSDist(d) ((FixInt(d)))
#define IsVertical(x1,y1,x2,y2) (VertQuo(x1,y1,x2,y2) > 0)
#define IsHorizontal(x1,y1,x2,y2) (HorzQuo(x1,y1,x2,y2) > 0)
#define SFACTOR (20L)
   /* SFACTOR must be < 25 for Gothic-Medium-22 c08 */
#define spcBonus (1000L)
#define ProdLt0(f0, f1) (((f0) < 0L && (f1) > 0L) || ((f0) > 0L && (f1) < 0L))
#define ProdGe0(f0, f1) (!ProdLt0(f0, f1))

#define DEBUG_ROUND(val) { val = ( val >=0 ) ? (2*(val/2)) : (2*((val - 1)/2));}
#define DEBUG_ROUND4(val) { val = ( val >=0 ) ? (4*(val/4)) : (4*((val - 1)/4));}

/* DEBUG_ROUND is used to force calculations to come out the same as the previous version, where coordinates used 7 bits for the Fixed fraction, rather than the current 8 bits. Once I am confident that there are no bugs in the update, I will remove all occurences of this macro, and accept the differences due to more exact division */
/* procedures */

/* The fix to float and float to fixed procs are different for ac because it
   uses 24 bit of integer and 8 bits of fraction. */
extern void acfixtopflt(Fixed x, float *pf);
extern Fixed acpflttofix(float *pv);

extern unsigned char * Alloc(integer sz); /* Sub-allocator */



extern AC_MEMMANAGEFUNCPTR AC_memmanageFuncPtr;
extern void *AC_memmanageCtxPtr;
extern void setAC_memoryManager(void *ctxptr, AC_MEMMANAGEFUNCPTR func);



#define ACNEWMEM(size) AC_memmanageFuncPtr(AC_memmanageCtxPtr, NULL, (unsigned long)(size))
#define ACREALLOCMEM(oldptr, newsize) AC_memmanageFuncPtr(AC_memmanageCtxPtr, (oldptr), (newsize))
#define ACFREEMEM(ptr) AC_memmanageFuncPtr(AC_memmanageCtxPtr, (ptr), 0)







extern int AddCounterColorChars();
extern boolean FindNameInList();
extern void ACGetVersion();
extern void PruneElementColorSegs();
extern int TestColorLst(/*lst, colorList, flg, Hflg, doLst*/);
extern PClrVal CopyClrs(/*lst*/);
extern void AutoExtraColors(/*movetoNewClrs, soleol, solWhere*/);
extern PPathElt FindSubpathBBox(/*e*/);
extern void ClrVBnds();
extern void ReClrVBnds();
extern void ClrHBnds();
extern void ReClrHBnds();
extern void AddBBoxHV(/*Hflg*/);
extern void ClrBBox();
extern void SetMaxStemDist(/* int dist */);
extern void CheckPathBBox();
extern integer SpecialCharType();
extern boolean VColorChar();
extern boolean HColorChar();
extern boolean NoBlueChar();
extern integer SolEolCharCode(/*s*/);
extern boolean SpecialSolEol();
extern boolean MoveToNewClrs();
extern void CheckSmooth();
extern void CheckBBoxEdge(/*e, vrt, lc, pf, pl*/);
extern boolean CheckBBoxes(/*e1, e2*/);
extern boolean CheckSmoothness(/*x0, y0, x1, y1, x2, y2, pd*/);
extern void CheckForDups();
extern boolean showClrInfo;
extern void AddColorPoint(Fixed x0, Fixed y0, Fixed x1, Fixed y1, char ch, PPathElt p0, PPathElt p1);
extern void AddHPair(PClrVal v, char ch);
extern void AddVPair(PClrVal v, char ch);
extern void XtraClrs(/*e*/);
extern boolean CreateTimesFile();
extern boolean DoFile(char *fname, boolean extracolor);
extern void DoList(/*filenames*/);
extern void EvalV();
extern void EvalH();
extern void GenVPts();
extern void CheckVal(/*val, vert*/);
extern void CheckTfmVal(/*b, t, vert*/);
extern void CheckVals(/*vlst, vert*/);
extern boolean DoFixes();
extern boolean FindLineSeg();
extern void FltnCurve(/*c0, c1, c2, c3, pfr*/);
extern boolean ReadFontInfo();
extern boolean InBlueBand(/*loc,n,p*/);
extern void GenHPts();
extern void PreGenPts();
extern PPathElt GetDest(/*cldest*/);
extern PPathElt GetClosedBy(/*clsdby*/);
extern void GetEndPoint(/*e, x1p, y1p*/);
extern void GetEndPoints(/*p,px0,py0,px1,py1*/);
extern Fixed VertQuo(/*xk,yk,xl,yl*/);
extern Fixed HorzQuo(/*xk,yk,xl,yl*/);
extern boolean IsTiny(/*e*/);
extern boolean IsShort(/*e*/);
extern PPathElt NxtForBend(/*p,px2,py2,px3,py3*/);
extern PPathElt PrvForBend(/*p,px2,py2*/);
extern boolean IsLower(/*p*/);
extern boolean IsUpper(/*p*/);
extern boolean CloseSegs(/*s1,s2,vert*/);
extern boolean DoAllIgnoreTime();
extern boolean DoArgsIgnoreTime();
extern boolean DoArgs(int cnt, char* names[], boolean extraColor, boolean* renameLog, boolean release);
extern boolean DoAll(boolean extraColor, boolean release, boolean *renameLog, boolean quiet);

extern void DoPrune();
extern void PruneVVals();
extern void PruneHVals();
extern void MergeVals(/*vert*/);
extern void MergeFromMainColors(char ch);
extern void RoundPathCoords();
extern void MoveSubpathToEnd(/*e*/);
extern void AddSolEol();
extern int IncludeFile();
extern void InitAuto();
extern void InitData(integer reason);
extern void InitFix();
extern void InitGen();
extern boolean RotateSubpaths(/*flg*/);
extern void InitPick();
extern void AutoAddFlex();
extern integer PointListCheck(/*new,lst*/);
extern boolean SameColors(/*cn1, cn2*/);
extern boolean PreCheckForColoring();
extern integer CountSubPaths();
extern void PickVVals(/*valList*/);
extern void PickHVals(/*valList*/);
extern void FindBestHVals();
extern void FindBestVVals();
extern void LogYMinMax();
extern void PrintMessage(/*s*/);
extern void ReportError(/*s*/);
extern void ReportSmoothError(/*x, y*/);
extern void ReportBadClosePathForAutoColoring(/*e*/);
extern void ReportAddFlex();
extern void ReportClipSharpAngle(/*x, y*/);
extern void ReportSharpAngle(/*x, y*/);
extern void ReportLinearCurve(/*e, x0, y0, x1, y1*/);
extern void ReportNonHError(/*x0, y0, x1, y1*/);
extern void ReportNonVError(/*x0, y0, x1, y1*/);
extern void ExpectedMoveTo(/*e*/);
extern void ReportMissingClosePath();
extern void ReportTryFlexNearMiss(/*x0, y0, x2, y2*/);
extern void ReportTryFlexError(/*CPflg, x, y*/);
extern void AskForSplit(/*e*/);
extern void ReportSplit(/*e*/);
extern void ReportConflictCheck(/*e, conflict, cp*/);
extern void ReportConflictCnt(/*e, cnt*/);
extern void ReportMoveSubpath(/*e*/);
extern void ReportRemFlare(/*e*/);
extern void ReportRemConflict(/*e*/);
extern void ReportRotateSubpath(/*e*/);
extern void ReportRemShortColors(/*ex, ey*/);
extern boolean   ResolveConflictBySplit(/*e,Hflg,lnk1,lnk2*/);
extern void ReportPossibleLoop(/*e*/);
extern void ShowHVal(/*val*/);
extern void ShowHVals(/*lst*/);
extern void ReportAddHVal(/*val*/);
extern void ShowVVal(/*val*/);
extern void ShowVVals(/*lst*/);
extern void ReportAddVVal(/*val*/);
extern void ReportFndBstVal(/*seg,val,hFlg*/);
extern void ReportCarry(/*l0, l1, loc, clrs, vert*/);
extern void ReportBestCP(/*e, cp*/);
extern void LogColorInfo(/*pl*/);
extern void ReportAddVSeg(/*from, to, loc, i*/);
extern void ReportAddHSeg(/*from, to, loc, i*/);
#if 0
extern void ReportBandError(/*str, loc, blu*/);
#else
extern void ReportBandNearMiss(/*str, loc, blu*/);
#endif
extern void ReportStemNearMiss(/*vert, w, minW, b, t*/);
extern void ReportColorConflict(/*x0, y0, x1, y1, ch*/);
extern void ReportDuplicates(/*x, y*/);
extern void ReportBBoxBogus(/*llx, lly, urx, ury*/);
extern void ReportMergeHVal(/*b0,t0,b1,t1,v0,s0,v1,s1*/);
extern void ReportMergeVVal(/*l0,r0,l1,r1,v0,s0,v1,s1*/);
extern void ReportPruneHVal(/*val*/);
extern void ReportPruneVVal(/*val*/);
extern Fixed ScaleAbs();
extern Fixed UnScaleAbs();
extern void InitShuffleSubpaths();
extern void MarkLinks(/*vL,hFlg*/);
extern void DoShuffleSubpaths();
extern void CopyMainH();
extern void CopyMainV();
extern void RMovePoint();
extern void AddVSegment();
extern void AddHSegment();
extern void Delete();
extern boolean StrEqual();
extern boolean ReadCharFile();
extern double FixToDbl(/*f*/);
extern boolean CompareValues();
extern void SaveFile();
extern void CheckForMultiMoveTo();
extern double fabs();
#define STARTUP (0)
#define RESTART (1)

void ListClrInfo(/**/);
void Test();

extern void InitAll(/*reason*/);

void AddVStem();
void AddHStem();

void AddCharExtremes();
void AddStemExtremes ();
