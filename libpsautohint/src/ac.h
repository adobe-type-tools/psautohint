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

#include "psautohint.h"

#include "logging.h"
#include "memory.h"


/* widely used definitions */

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

/* character bounding box */
typedef struct Bbox
   {
   int32_t llx, lly, urx, ury;
   } Bbox, *BboxPtr;

/* character point coordinates */
typedef struct
   {
   int32_t x, y;
   } Cd, *CdPtr;

typedef struct {
  int16_t limit;
  Fixed feps;
  void (*report)(Cd);
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
  FFEntry *entries; /* font information entries */
  size_t length;    /* number of the entries */
} ACFontInfo;

typedef struct {
  char* data;       /* character data held in the buffer */
  size_t length;    /* actual length of the data */
  size_t capacity;  /* allocated memory size */
} ACBuffer;

/* global data */

extern ACBuffer* gBezOutput;

extern PPathElt gPathStart, gPathEnd;
extern bool gYgoesUp;
extern bool gUseV, gUseH, gAutoVFix, gAutoHFix, gAutoLinearCurveFix;
extern bool gAutoExtraDebug, gDebugColorPath, gDebug, gLogging;
extern bool gEditChar; /* whether character can be modified when adding hints */
extern bool gShowHs, gShowVs, gBandError, gListClrInfo;
extern bool gReportErrors, gHasFlex, gFlexOK, gFlexStrict, gShowClrInfo;
extern Fixed gHBigDist, gVBigDist, gInitBigDist, gMinDist, gGhostWidth,
  gGhostLength, gBendLength, gBandMargin, gMaxFlare,
  gMaxBendMerge, gMaxMerge, gMinColorElementLength, gFlexCand;
extern Fixed gPruneA, gPruneB, gPruneC, gPruneD, gPruneValue, gBonus;
extern float gTheta, gHBigDistR, gVBigDistR, gMaxVal, gMinVal;
extern int32_t gDMin, gDelta, gCPpercent, gBendTan, gSCurveTan;
extern PClrVal gVColoring, gHColoring, gVPrimary, gHPrimary, gValList;
extern PClrSeg gSegLists[4]; /* left, right, top, bot */
extern PClrPoint gPointList, *gPtLstArray;
extern int32_t gPtLstIndex, gNumPtLsts, gMaxPtLsts;
extern bool gScalingHints;

/* global callbacks */

/* global log function which is supplied by the following */
extern AC_REPORTFUNCPTR gLibReportCB;
/* global error log function which is supplied by the following */
extern AC_REPORTFUNCPTR gLibErrorReportCB;

/* if false, then stems defined by curves are excluded from the reporting */
extern unsigned int gAllStems;

extern AC_REPORTSTEMPTR gAddHStemCB;
extern AC_REPORTSTEMPTR gAddVStemCB;

extern AC_REPORTZONEPTR gAddCharExtremesCB;
extern AC_REPORTZONEPTR gAddStemExtremesCB;

void AddStemExtremes(Fixed bot, Fixed top);

extern AC_RETRYPTR gReportRetryCB;

#define leftList (gSegLists[0])
#define rightList (gSegLists[1])
#define topList (gSegLists[2])
#define botList (gSegLists[3])

#define MAXFLEX (PSDist(20))
#define MAXBLUES (20)
#define MAXSERIFS (5)
extern Fixed gTopBands[MAXBLUES], gBotBands[MAXBLUES], gSerifs[MAXSERIFS];
extern int32_t gLenTopBands, gLenBotBands, gNumSerifs;
#define MAXSTEMS (20)
extern Fixed gVStems[MAXSTEMS], gHStems[MAXSTEMS];
extern int32_t gNumVStems, gNumHStems;
extern char *gHColorList[], *gVColorList[];
extern int32_t gNumHColors, gNumVColors;
extern bool gWriteColoredBez;
extern Fixed gBlueFuzz;
extern bool gDoAligns, gDoStems;
extern bool gIdInFile;
extern bool gRoundToInt;

#define MAX_GLYPHNAME_LEN 64
/* defined in read.c; set from the glyph name at the start of the bex file. */
extern char gGlyphName[MAX_GLYPHNAME_LEN];

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

#define MAXBUFFLEN 127

/* DEBUG_ROUND is used to force calculations to come out the same as the previous version, where coordinates used 7 bits for the Fixed fraction, rather than the current 8 bits. Once I am confident that there are no bugs in the update, I will remove all occurences of this macro, and accept the differences due to more exact division */
/* procedures */

/* The fix to float and float to fixed procs are different for ac because it
   uses 24 bit of int32_t and 8 bits of fraction. */
void acfixtopflt(Fixed x, float* pf);
Fixed acpflttofix(float* pf);

unsigned char* Alloc(int32_t sz); /* Sub-allocator */

int AddCounterColorChars(char* charlist, char* ColorList[]);
bool FindNameInList(char* nm, char** lst);
void PruneElementColorSegs(void);
int TestColorLst(PSegLnkLst lst, PClrVal colorList, bool flg, bool doLst);
PClrVal CopyClrs(PClrVal lst);
void AutoExtraColors(bool movetoNewClrs, bool soleol, int32_t solWhere);
int32_t SpecialCharType(void);
bool VColorChar(void);
bool HColorChar(void);
bool NoBlueChar(void);
int32_t SolEolCharCode(void);
bool SpecialSolEol(void);
bool MoveToNewClrs(void);
bool GetInflectionPoint(Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed *);
void CheckSmooth(void);
void CheckBBoxEdge(PPathElt e, bool vrt, Fixed lc, Fixed* pf, Fixed* pl);
bool CheckSmoothness(Fixed x0, Fixed cy0, Fixed x1, Fixed cy1, Fixed x2,
                     Fixed y2, Fixed* pd);
void CheckForDups(void);
void AddColorPoint(Fixed x0, Fixed y0, Fixed x1, Fixed y1, char ch, PPathElt p0,
                   PPathElt p1);
void AddHPair(PClrVal v, char ch);
void AddVPair(PClrVal v, char ch);
void XtraClrs(PPathElt e);
bool AutoColorGlyph(const ACFontInfo* fontinfo, const char* srcglyph,
                    bool extracolor);
void EvalV(void);
void EvalH(void);
void GenVPts(int32_t specialCharType);
void CheckVal(PClrVal val, bool vert);
void CheckTfmVal(PClrSeg hSegList, Fixed* bandList, int32_t length);
void CheckVals(PClrVal vlst, bool vert);
bool DoFixes(void);
bool FindLineSeg(Fixed loc, PClrSeg sL);
void FltnCurve(Cd c0, Cd c1, Cd c2, Cd c3, PFltnRec pfr);
bool InBlueBand(Fixed loc, int32_t n, Fixed* p);
void GenHPts(void);
void PreGenPts(void);
PPathElt GetDest(PPathElt cldest);
PPathElt GetClosedBy(PPathElt clsdby);
void GetEndPoint(PPathElt e, Fixed* x1p, Fixed* y1p);
void GetEndPoints(PPathElt p, Fixed* px0, Fixed* py0, Fixed* px1, Fixed* py1);
Fixed VertQuo(Fixed xk, Fixed yk, Fixed xl, Fixed yl);
Fixed HorzQuo(Fixed xk, Fixed yk, Fixed xl, Fixed yl);
bool IsTiny(PPathElt e);
bool IsShort(PPathElt e);
PPathElt NxtForBend(PPathElt p, Fixed* px2, Fixed* py2, Fixed* px3, Fixed* py3);
PPathElt PrvForBend(PPathElt p, Fixed* px2, Fixed* py2);
bool IsLower(PPathElt p);
bool IsUpper(PPathElt p);
bool CloseSegs(PClrSeg s1, PClrSeg s2, bool vert);

void DoPrune(void);
void PruneVVals(void);
void PruneHVals(void);
void MergeVals(bool vert);
void MergeFromMainColors(char ch);
void RoundPathCoords(void);
void MoveSubpathToEnd(PPathElt e);
void AddSolEol(void);
void InitAuto(int32_t reason);
void InitData(const ACFontInfo* fontinfo, int32_t reason);
void InitFix(int32_t reason);
void InitGen(int32_t reason);
void InitPick(int32_t reason);
void AutoAddFlex(void);
bool SameColors(int32_t cn1, int32_t cn2);
bool PreCheckForColoring(void);
int32_t CountSubPaths(void);
void PickVVals(PClrVal gValList);
void PickHVals(PClrVal gValList);
void FindBestHVals(void);
void FindBestVVals(void);
void PrintMessage(char* format, ...);
void ReportError(char* format, ...);
void ReportSmoothError(Fixed x, Fixed y);
void ReportAddFlex(void);
void ReportClipSharpAngle(Fixed x, Fixed y);
void ReportSharpAngle(Fixed x, Fixed y);
void ReportLinearCurve(PPathElt e, Fixed x0, Fixed y0, Fixed x1, Fixed y1);
void ReportNonHError(Fixed x0, Fixed y0, Fixed x1, Fixed y1);
void ReportNonVError(Fixed x0, Fixed y0, Fixed x1, Fixed y1);
void ExpectedMoveTo(PPathElt e);
void ReportMissingClosePath(void);
void ReportTryFlexNearMiss(Fixed x0, Fixed y0, Fixed x2, Fixed y2);
void ReportTryFlexError(bool CPflg, Fixed x, Fixed y);
void AskForSplit(PPathElt e);
void ReportSplit(PPathElt e);
void ReportConflictCheck(PPathElt e, PPathElt conflict, PPathElt cp);
void ReportConflictCnt(PPathElt e, int32_t cnt);
void ReportMoveSubpath(PPathElt e, char* s);
void ReportRemFlare(PPathElt e, PPathElt e2, bool hFlg, int32_t i);
void ReportRemConflict(PPathElt e);
void ReportRotateSubpath(PPathElt e);
void ReportRemShortColors(Fixed ex, Fixed ey);
bool ResolveConflictBySplit(PPathElt e, bool Hflg, PSegLnkLst lnk1,
                            PSegLnkLst lnk2);
void ReportPossibleLoop(PPathElt e);
void ShowHVal(PClrVal val);
void ShowHVals(PClrVal lst);
void ReportAddHVal(PClrVal val);
void ShowVVal(PClrVal val);
void ShowVVals(PClrVal lst);
void ReportAddVVal(PClrVal val);
void ReportFndBstVal(PClrSeg seg, PClrVal val, bool hFlg);
void ReportCarry(Fixed l0, Fixed l1, Fixed loc, PClrVal clrs, bool vert);
void ReportBestCP(PPathElt e, PPathElt cp);
void LogColorInfo(PClrPoint pl);
void ReportAddVSeg(Fixed from, Fixed to, Fixed loc, int32_t i);
void ReportAddHSeg(Fixed from, Fixed to, Fixed loc, int32_t i);
void ReportBandNearMiss(char* str, Fixed loc, Fixed blu);
void ReportStemNearMiss(bool vert, Fixed w, Fixed minW, Fixed b, Fixed t,
                        bool curve);
void ReportColorConflict(Fixed x0, Fixed y0, Fixed x1, Fixed y1, char ch);
void ReportDuplicates(Fixed x, Fixed y);
void ReportBBoxBogus(Fixed llx, Fixed lly, Fixed urx, Fixed ury);
void ReportMergeHVal(Fixed b0, Fixed t0, Fixed b1, Fixed t1, Fixed v0, Fixed s0,
                     Fixed v1, Fixed s1);
void ReportMergeVVal(Fixed l0, Fixed r0, Fixed l1, Fixed r1, Fixed v0, Fixed s0,
                     Fixed v1, Fixed s1);
void ReportPruneHVal(PClrVal val, PClrVal v, int32_t i);
void ReportRemVSeg(Fixed from, Fixed to, Fixed loc);
void ReportRemHSeg(Fixed from, Fixed to, Fixed loc);
void ReportPruneVVal(PClrVal val, PClrVal v, int32_t i);
Fixed ScaleAbs(const ACFontInfo* fontinfo, Fixed unscaled);
Fixed UnScaleAbs(const ACFontInfo* fontinfo, Fixed scaled);
void InitShuffleSubpaths(void);
void MarkLinks(PClrVal vL, bool hFlg);
void DoShuffleSubpaths(void);
void CopyMainH(void);
void CopyMainV(void);
void RMovePoint(Fixed dx, Fixed dy, int32_t whichcp, PPathElt e);
void AddVSegment(Fixed from, Fixed to, Fixed loc, PPathElt p1, PPathElt p2,
                 int32_t typ, int32_t i);
void AddHSegment(Fixed from, Fixed to, Fixed loc, PPathElt p1, PPathElt p2,
                 int32_t typ, int32_t i);
void Delete(PPathElt e);
bool ReadGlyph(const ACFontInfo* fontinfo, const char* srcglyph,
               bool forBlendData, bool readHints);
double FixToDbl(Fixed f);
bool CompareValues(PClrVal val1, PClrVal val2, int32_t factor,
                   int32_t ghstshift);
void SaveFile(const ACFontInfo* fontinfo);
void CheckForMultiMoveTo(void);
#define STARTUP (0)
#define RESTART (1)

void ListClrInfo(void);

void InitAll(const ACFontInfo* fontinfo, int32_t reason);

void AddVStem(Fixed top, Fixed bottom, bool curved);
void AddHStem(Fixed right, Fixed left, bool curved);

void AddCharExtremes(Fixed bot, Fixed top);

bool AutoColor(const ACFontInfo* fontinfo, const char* srcbezdata,
               bool fixStems, bool debug, bool extracolor, bool changeChar,
               bool roundCoords);

bool MergeCharPaths(const ACFontInfo* fontinfo, const char** srcglyphs,
                    int nmasters, const char** masters, char** outbuffers,
                    size_t* outlengths);

#endif /* AC_AC_H_ */
