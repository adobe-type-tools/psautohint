/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

/* See the discussion in the function definition for:
 control.c:Blues() 
 static void Blues() 
 */

#ifndef AC_AC_H_
#define AC_AC_H_

#include "ac_C_lib.h"
#include "buildfont.h"


/* widely used definitions */


#if defined(THINK_C)
#define L_cuserid 20 /*ASP: was defined stdio.h*/
#endif


/* number of default entries in counter color character list. */
#define COUNTERDEFAULTENTRIES 4
#define COUNTERLISTSIZE 64

/* values for ClrSeg.sType */
#define sLINE (0)
#define sBEND (1)
#define sCURVE (2)
#define sGHOST (3)

/* values for PathElt.type */
#define MOVETO (0)
#define LINETO (1)
#define CURVETO (2)
#define CLOSEPATH (3)

/* values for pathelt control points */
#define cpStart (0)
#define cpCurve1 (1)
#define cpCurve2 (2)
#define cpEnd (3)

/* widths of ghost bands */
#define botGhst (-21)
#define topGhst (-20)

/* structures */

typedef struct {
  int16_t limit;
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
  int16_t sType;
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
  int16_t vGhst:8;
  int16_t pruned:8;
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
  int16_t type;
  PSegLnkLst Hs, Vs;
  bool Hcopy:1, Vcopy:1, isFlex:1, yFlex:1, newCP:1, sol:1, eol:1;
  int unused:9;
  int16_t count, newcolors;
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
  bool done;
  } ClrPoint, *PClrPoint;

typedef struct {
	char *key, *value;
} FFEntry;

typedef struct {
  FFEntry *entries;
  int size;
} ACFontInfo;

/* global data */

extern PPathElt pathStart, pathEnd;
extern bool YgoesUp;
extern bool useV, useH, autoVFix, autoHFix, autoLinearCurveFix;
extern bool AutoExtraDEBUG, debugColorPath, DEBUG, logging;
extern bool editChar; /* whether character can be modified when adding hints */
extern bool scalehints;
extern bool showHs, showVs, bandError, listClrInfo;
extern bool reportErrors, hasFlex, flexOK, flexStrict, showClrInfo;
extern Fixed hBigDist, vBigDist, initBigDist, minDist, minMidPt, ghostWidth,
  ghostLength, bendLength, bandMargin, maxFlare,
  maxBendMerge, maxMerge, minColorElementLength, flexCand,
  pruneMargin;
extern Fixed pruneA, pruneB, pruneC, pruneD, pruneValue, bonus;
extern float theta, hBigDistR, vBigDistR, maxVal, minVal;
extern int32_t DMIN, DELTA, CPpercent, bendTan, sCurveTan;
extern PClrVal Vcoloring, Hcoloring, Vprimary, Hprimary, valList;
extern char * fileName;
extern PClrSeg segLists[4]; /* left, right, top, bot */
extern PClrPoint pointList, *ptLstArray;
extern int32_t ptLstIndex, numPtLsts, maxPtLsts;
void AddStemExtremes(Fixed bot, Fixed top);

#define leftList (segLists[0])
#define rightList (segLists[1])
#define topList (segLists[2])
#define botList (segLists[3])

#define MAXFLEX (PSDist(20))
#define MAXBLUES (20)
#define MAXSERIFS (5)
extern Fixed topBands[MAXBLUES], botBands[MAXBLUES], serifs[MAXSERIFS];
extern int32_t lenTopBands, lenBotBands, numSerifs;
#define MAXSTEMS (20)
extern Fixed VStems[MAXSTEMS], HStems[MAXSTEMS];
extern int32_t NumVStems, NumHStems;
extern char *HColorList[], *VColorList[];
extern int32_t NumHColors, NumVColors;
extern bool writecoloredbez;
extern Fixed bluefuzz;
extern bool doAligns, doStems;
extern bool idInFile;
extern bool roundToInt;
extern char bezGlyphName[64]; /* defined in read.c; set from the glyph name at the start of the bex file. */

/* macros */

#define FixedPosInf INT32_MAX
#define FixedNegInf INT32_MIN
#define FixShift (8)
#define FixInt(i) (((int32_t)(i)) << FixShift)
#define FixReal(i) ((int32_t)((i) *256.0))
int32_t FRnd(int32_t x);
#define FHalfRnd(x) ((int32_t)(((x)+(1<<7)) & ~0xFF))
#define FracPart(x) ((int32_t)(x) & 0xFF)
#define FTrunc(x) (((int32_t)(x))>>FixShift)
#define FIXED2FLOAT(x) (x/256.0)

#define FixOne (0x100)
#define FixTwo (0x200)
#define FixHalf (0x80)
#define FixQuarter (0x40)
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
#define SFACTOR (20)
   /* SFACTOR must be < 25 for Gothic-Medium-22 c08 */
#define spcBonus (1000)
#define ProdLt0(f0, f1) (((f0) < 0 && (f1) > 0) || ((f0) > 0 && (f1) < 0))
#define ProdGe0(f0, f1) (!ProdLt0(f0, f1))

#define DEBUG_ROUND(val) { val = ( val >=0 ) ? (2*(val/2)) : (2*((val - 1)/2));}
#define DEBUG_ROUND4(val) { val = ( val >=0 ) ? (4*(val/4)) : (4*((val - 1)/4));}

/* DEBUG_ROUND is used to force calculations to come out the same as the previous version, where coordinates used 7 bits for the Fixed fraction, rather than the current 8 bits. Once I am confident that there are no bugs in the update, I will remove all occurences of this macro, and accept the differences due to more exact division */
/* procedures */

/* The fix to float and float to fixed procs are different for ac because it
   uses 24 bit of int32_t and 8 bits of fraction. */
void acfixtopflt(Fixed x, float *pf);
Fixed acpflttofix(float *pv);

unsigned char * Alloc(int32_t sz); /* Sub-allocator */



extern AC_MEMMANAGEFUNCPTR AC_memmanageFuncPtr;
extern void *AC_memmanageCtxPtr;
void setAC_memoryManager(void *ctxptr, AC_MEMMANAGEFUNCPTR func);



#define ACNEWMEM(size) AC_memmanageFuncPtr(AC_memmanageCtxPtr, NULL, (uint32_t)(size))
#define ACREALLOCMEM(oldptr, newsize) AC_memmanageFuncPtr(AC_memmanageCtxPtr, (oldptr), (newsize))
#define ACFREEMEM(ptr) AC_memmanageFuncPtr(AC_memmanageCtxPtr, (ptr), 0)







int AddCounterColorChars();
bool FindNameInList();
void ACGetVersion();
void PruneElementColorSegs();
int TestColorLst(/*lst, colorList, flg, Hflg, doLst*/);
PClrVal CopyClrs(/*lst*/);
void AutoExtraColors(/*movetoNewClrs, soleol, solWhere*/);
PPathElt FindSubpathBBox(/*e*/);
void ClrVBnds();
void ReClrVBnds();
void ClrHBnds();
void ReClrHBnds();
void AddBBoxHV(/*Hflg*/);
void ClrBBox();
void CheckPathBBox();
int32_t SpecialCharType();
bool VColorChar();
bool HColorChar();
bool NoBlueChar();
int32_t SolEolCharCode(/*s*/);
bool SpecialSolEol();
bool MoveToNewClrs();
void CheckSmooth();
void CheckBBoxEdge(/*e, vrt, lc, pf, pl*/);
bool CheckBBoxes(/*e1, e2*/);
bool CheckSmoothness(/*x0, y0, x1, y1, x2, y2, pd*/);
void CheckForDups();
bool showClrInfo;
void AddColorPoint(Fixed x0, Fixed y0, Fixed x1, Fixed y1, char ch, PPathElt p0, PPathElt p1);
void AddHPair(PClrVal v, char ch);
void AddVPair(PClrVal v, char ch);
void XtraClrs(/*e*/);
bool CreateTimesFile();
bool DoFile(const ACFontInfo* fontinfo, const char* srcglyph,
                   char* fname, bool extracolor);
void DoList(/*filenames*/);
void EvalV();
void EvalH();
void GenVPts();
void CheckVal(/*val, vert*/);
void CheckTfmVal(/*b, t, vert*/);
void CheckVals(/*vlst, vert*/);
bool DoFixes();
bool FindLineSeg();
void FltnCurve(/*c0, c1, c2, c3, pfr*/);
bool ReadFontInfo(const ACFontInfo* fontinfo);
bool InBlueBand(/*loc,n,p*/);
void GenHPts();
void PreGenPts();
PPathElt GetDest(/*cldest*/);
PPathElt GetClosedBy(/*clsdby*/);
void GetEndPoint(/*e, x1p, y1p*/);
void GetEndPoints(/*p,px0,py0,px1,py1*/);
Fixed VertQuo(/*xk,yk,xl,yl*/);
Fixed HorzQuo(/*xk,yk,xl,yl*/);
bool IsTiny(/*e*/);
bool IsShort(/*e*/);
PPathElt NxtForBend(/*p,px2,py2,px3,py3*/);
PPathElt PrvForBend(/*p,px2,py2*/);
bool IsLower(/*p*/);
bool IsUpper(/*p*/);
bool CloseSegs(/*s1,s2,vert*/);

void DoPrune();
void PruneVVals();
void PruneHVals();
void MergeVals(/*vert*/);
void MergeFromMainColors(char ch);
void RoundPathCoords();
void MoveSubpathToEnd(/*e*/);
void AddSolEol();
void InitAuto();
void InitData(const ACFontInfo* fontinfo, int32_t reason);
void InitFix();
void InitGen();
bool RotateSubpaths(/*flg*/);
void InitPick();
void AutoAddFlex();
int32_t PointListCheck(/*new,lst*/);
bool SameColors(/*cn1, cn2*/);
bool PreCheckForColoring();
int32_t CountSubPaths();
void PickVVals(/*valList*/);
void PickHVals(/*valList*/);
void FindBestHVals();
void FindBestVVals();
void PrintMessage(/*s*/);
void ReportError(/*s*/);
void ReportSmoothError(/*x, y*/);
void ReportBadClosePathForAutoColoring(/*e*/);
void ReportAddFlex();
void ReportClipSharpAngle(/*x, y*/);
void ReportSharpAngle(/*x, y*/);
void ReportLinearCurve(/*e, x0, y0, x1, y1*/);
void ReportNonHError(/*x0, y0, x1, y1*/);
void ReportNonVError(/*x0, y0, x1, y1*/);
void ExpectedMoveTo(/*e*/);
void ReportMissingClosePath();
void ReportTryFlexNearMiss(/*x0, y0, x2, y2*/);
void ReportTryFlexError(/*CPflg, x, y*/);
void AskForSplit(/*e*/);
void ReportSplit(/*e*/);
void ReportConflictCheck(/*e, conflict, cp*/);
void ReportConflictCnt(/*e, cnt*/);
void ReportMoveSubpath(/*e*/);
void ReportRemFlare(/*e*/);
void ReportRemConflict(/*e*/);
void ReportRotateSubpath(/*e*/);
void ReportRemShortColors(/*ex, ey*/);
bool   ResolveConflictBySplit(/*e,Hflg,lnk1,lnk2*/);
void ReportPossibleLoop(/*e*/);
void ShowHVal(/*val*/);
void ShowHVals(/*lst*/);
void ReportAddHVal(/*val*/);
void ShowVVal(/*val*/);
void ShowVVals(/*lst*/);
void ReportAddVVal(/*val*/);
void ReportFndBstVal(/*seg,val,hFlg*/);
void ReportCarry(/*l0, l1, loc, clrs, vert*/);
void ReportBestCP(/*e, cp*/);
void LogColorInfo(/*pl*/);
void ReportAddVSeg(/*from, to, loc, i*/);
void ReportAddHSeg(/*from, to, loc, i*/);
#if 0
void ReportBandError(/*str, loc, blu*/);
#else
void ReportBandNearMiss(/*str, loc, blu*/);
#endif
void ReportStemNearMiss(/*vert, w, minW, b, t*/);
void ReportColorConflict(/*x0, y0, x1, y1, ch*/);
void ReportDuplicates(/*x, y*/);
void ReportBBoxBogus(/*llx, lly, urx, ury*/);
void ReportMergeHVal(/*b0,t0,b1,t1,v0,s0,v1,s1*/);
void ReportMergeVVal(/*l0,r0,l1,r1,v0,s0,v1,s1*/);
void ReportPruneHVal(/*val*/);
void ReportPruneVVal(/*val*/);
Fixed ScaleAbs(const ACFontInfo* fontinfo, Fixed unscaled);
Fixed UnScaleAbs(const ACFontInfo* fontinfo, Fixed scaled);
void InitShuffleSubpaths();
void MarkLinks(/*vL,hFlg*/);
void DoShuffleSubpaths();
void CopyMainH();
void CopyMainV();
void RMovePoint();
void AddVSegment();
void AddHSegment();
void Delete();
bool StrEqual();
bool ReadCharFile(const ACFontInfo* fontinfo, const char* srcglyph,
                         bool normal, bool forBlendData, bool readHints,
                         bool prependprefix);
double FixToDbl(/*f*/);
bool CompareValues();
void SaveFile(const ACFontInfo* fontinfo);
void CheckForMultiMoveTo();
double fabs();
#define STARTUP (0)
#define RESTART (1)

void ListClrInfo(/**/);

void InitAll(const ACFontInfo* fontinfo, int32_t reason);

void AddVStem();
void AddHStem();

void AddCharExtremes();
void AddStemExtremes ();

bool AutoColor(const ACFontInfo* fontinfo, const char* srcglyph, bool fixStems,
               bool debug, bool extracolor, bool changeChar, bool roundCoords);

#endif /* AC_AC_H_ */
