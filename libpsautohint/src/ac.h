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

/* values for HintSeg.sType */
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

/* glyph point coordinates */
typedef struct {
   Fixed x, y;
   } Cd;

typedef struct {
  int16_t limit;
  Fixed feps;
  void (*report)(Cd);
  Cd ll, ur;
  Fixed llx, lly;
  } FltnRec;

typedef struct _hintseg {
  struct _hintseg *sNxt;
    /* points to next HintSeg in list */
    /* separate lists for top, bottom, left, and right segments */
  Fixed sLoc, sMax, sMin;
    /* sLoc is X loc for vertical seg, Y loc for horizontal seg */
    /* sMax and sMin give Y extent for vertical seg, X extent for horizontal */
    /* i.e., sTop=sMax, sBot=sMin, sLft=sMin, sRght=sMax. */
  Fixed sBonus;
    /* nonzero for segments in sol-eol subpaths */
    /* (probably a leftover that is no longer needed) */
  struct _hintval *sLnk;
    /* points to the best HintVal that uses this HintSeg */
    /* set by FindBestValForSegs in pick.c */
  struct _pthelt *sElt;
    /* points to the path element that generated this HintSeg */
    /* set by AddSegment in gen.c */
  int16_t sType;
    /* tells what type of segment this is: sLINE sBEND sCURVE or sGHOST */
  } HintSeg;

typedef struct {
  HintSeg* seg;
  } SegLnk;

typedef struct _seglnklst {
  struct _seglnklst *next;
  SegLnk* lnk;
  } SegLnkLst;

#if 0
typedef struct _hintrep {
  Fixed vVal, vSpc, vLoc1, vLoc2;
  struct _hintval *vBst;
  } HintRep;

typedef struct _hintval {
  struct _hintval *vNxt;
  Fixed vVal, vSpc, initVal;
  Fixed vLoc1, vLoc2;
    /* vBot=vLoc1, vTop=vLoc2, vLft=vLoc1, vRght=vLoc2 */ 
  int16_t vGhst:8;
  int16_t pruned:8;
  HintSeg* vSeg1, *vSeg2;
  struct _hintval *vBst;
  HintRep* vRep;
  } HintVal;
#else
typedef struct _hintval {
  struct _hintval *vNxt;
    /* points to next HintVal in list */
  Fixed vVal, vSpc, initVal;
    /* vVal is value given in eval.c */
    /* vSpc is nonzero for "special" HintVals */
       /* such as those with a segment in a blue zone */
    /* initVal is the initially assigned value */
       /* used by FndBstVal in pick.c */
  Fixed vLoc1, vLoc2;
    /* vLoc1 is location corresponding to vSeg1 */
    /* vLoc2 is location corresponding to vSeg2 */
    /* for horizontal HintVal, vBot=vLoc1 and vTop=vLoc2 */
    /* for vertical HintVal, vLft=vLoc1 and vRght=vLoc2 */
  unsigned int vGhst:1;  /* true iff one of the HintSegs is a sGHOST seg */
  unsigned int pruned:1;
    /* flag used by FindBestHVals and FindBestVVals */ 
    /* and by PruneVVals and PruneHVals */
  unsigned int merge:1;
    /* flag used by ReplaceVals in merge.c */
  unsigned int unused:13;
  HintSeg *vSeg1, *vSeg2;
    /* vSeg1 points to the left HintSeg in a vertical, bottom in a horizontal */
    /* vSeg2 points to the right HintSeg in a vertical, top in a horizontal */
  struct _hintval *vBst;
    /* points to another HintVal if this one has been merged or replaced */
  } HintVal;
#endif

typedef struct _pthelt {
  struct _pthelt *prev, *next, *conflict;
  int16_t type;
  SegLnkLst *Hs, *Vs;
  bool Hcopy:1, Vcopy:1, isFlex:1, yFlex:1, newCP:1;
  unsigned int unused:9;
  int16_t count, newhints;
  Fixed x, y, x1, y1, x2, y2, x3, y3;
  } PathElt;

typedef struct _hintpnt {
  struct _hintpnt *next;
  Fixed x0, y0, x1, y1;
    /* for vstem, only interested in x0 and x1 */
    /* for hstem, only interested in y0 and y1 */
  PathElt *p0, *p1;
    /* p0 is source of x0,y0; p1 is source of x1,y1 */
  char c;
    /* tells what kind of hinting: 'b' 'y' 'm' or 'v' */
  bool done;
  } HintPoint;

typedef struct {
  char** keys;      /* font information keys */
  char** values;    /* font information values */
  size_t length;    /* number of the entries */
} ACFontInfo;

/* global data */

extern ACBuffer* gBezOutput;

extern PathElt* gPathStart, *gPathEnd;
extern bool gUseV, gUseH, gAutoLinearCurveFix;
extern bool gEditGlyph; /* whether glyph can be modified when adding hints */
extern bool gBandError;
extern bool gHasFlex, gFlexOK, gFlexStrict;
extern Fixed gHBigDist, gVBigDist, gInitBigDist, gMinDist, gGhostWidth,
  gGhostLength, gBendLength, gBandMargin, gMaxFlare,
  gMaxBendMerge, gMaxMerge, gMinHintElementLength, gFlexCand;
extern Fixed gPruneA, gPruneB, gPruneC, gPruneD, gPruneValue, gBonus;
extern float gTheta, gHBigDistR, gVBigDistR, gMaxVal, gMinVal;
extern int32_t gDMin, gDelta, gCPpercent, gBendTan, gSCurveTan;
extern HintVal *gVHinting, *gHHinting, *gVPrimary, *gHPrimary, *gValList;
extern HintSeg* gSegLists[4]; /* left, right, top, bot */
extern HintPoint* gPointList, **gPtLstArray;
extern int32_t gPtLstIndex, gNumPtLsts, gMaxPtLsts;

/* global callbacks */

/* if false, then stems defined by curves are excluded from the reporting */
extern unsigned int gAllStems;

extern AC_REPORTSTEMPTR gAddHStemCB;
extern AC_REPORTSTEMPTR gAddVStemCB;

extern AC_REPORTZONEPTR gAddGlyphExtremesCB;
extern AC_REPORTZONEPTR gAddStemExtremesCB;

extern AC_RETRYPTR gReportRetryCB;

extern void* gAddStemUserData;
extern void* gAddExtremesUserData;
extern void* gReportRetryUserData;

void AddStemExtremes(Fixed bot, Fixed top);

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
extern char *gHHintList[], *gVHintList[];
extern int32_t gNumHHints, gNumVHints;
extern bool gWriteHintedBez;
extern Fixed gBlueFuzz;
extern bool gDoAligns, gDoStems;
extern bool gRoundToInt;
extern bool gAddHints;

#define MAX_GLYPHNAME_LEN 64
/* defined in read.c; set from the glyph name at the start of the bex file. */
extern char gGlyphName[MAX_GLYPHNAME_LEN];

/* macros */

#define FixOne (0x100)
#define FixTwo (0x200)
#define FixHalf (0x80)
#define FixQuarter (0x40)

#define FIXED_MAX INT32_MAX
#define FIXED_MIN INT32_MIN
#define FixInt(i) (((int32_t)(i)) * FixOne)
#define FixReal(i) ((int32_t)((i) * (float)FixOne))
int32_t FRnd(int32_t x);
#define FHalfRnd(x) ((int32_t)(((x)+(1<<7)) & ~0xFF))
#define FracPart(x) ((int32_t)(x) & 0xFF)
#define FTrunc(x) (((int32_t)(x)) / FixOne)
#define FIXED2FLOAT(x) (x / (float)FixOne)

#define FixHalfMul(f) (2*((f) >> 2)) /* DEBUG 8 BIT. Revert this to ((f) >>1) once I am confident that there are not bugs from the update to 8 bits for the Fixed fraction. */
#define FixTwoMul(f) ((f) << 1)
#define PSDist(d) ((FixInt(d)))
#define IsVertical(x1,y1,x2,y2) (VertQuo(x1,y1,x2,y2) > 0)
#define IsHorizontal(x1,y1,x2,y2) (HorzQuo(x1,y1,x2,y2) > 0)
#define SFACTOR (20)
   /* SFACTOR must be < 25 for Gothic-Medium-22 c08 */
#define spcBonus (1000)
#define ProdLt0(f0, f1) (((f0) < 0 && (f1) > 0) || ((f0) > 0 && (f1) < 0))
#define ProdGe0(f0, f1) (!ProdLt0(f0, f1))

#define DEBUG_ROUND(val) { val = ( val >=0 ) ? (2*(val/2)) : (2*((val - 1)/2));}

/* DEBUG_ROUND is used to force calculations to come out the same as the previous version, where coordinates used 7 bits for the Fixed fraction, rather than the current 8 bits. Once I am confident that there are no bugs in the update, I will remove all occurences of this macro, and accept the differences due to more exact division */
/* procedures */

/* The fix to float and float to fixed procs are different for ac because it
   uses 24 bit of int32_t and 8 bits of fraction. */
void acfixtopflt(Fixed x, float* pf);
Fixed acpflttofix(float* pf);

void *Alloc(int32_t sz); /* Sub-allocator */

int AddCounterHintGlyphs(char* charlist, char* HintList[]);
bool FindNameInList(char* nm, char** lst);
void PruneElementHintSegs(void);
int TestHintLst(SegLnkLst* lst, HintVal* hintList, bool flg, bool doLst);
HintVal* CopyHints(HintVal* lst);
void AutoExtraHints(bool movetoNewHints);
int32_t SpecialGlyphType(void);
bool VHintGlyph(void);
bool HHintGlyph(void);
bool NoBlueGlyph(void);
bool MoveToNewHints(void);
bool GetInflectionPoint(Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed *);
void CheckSmooth(void);
void CheckBBoxEdge(PathElt* e, bool vrt, Fixed lc, Fixed* pf, Fixed* pl);
bool CheckSmoothness(Fixed x0, Fixed cy0, Fixed x1, Fixed cy1, Fixed x2,
                     Fixed y2, Fixed* pd);
void CheckForDups(void);
void AddHintPoint(Fixed x0, Fixed y0, Fixed x1, Fixed y1, char ch, PathElt* p0,
                   PathElt* p1);
void AddHPair(HintVal* v, char ch);
void AddVPair(HintVal* v, char ch);
void XtraHints(PathElt* e);
bool AutoHintGlyph(const char* srcglyph, bool extrahint);
void EvalV(void);
void EvalH(void);
void GenVPts(int32_t specialGlyphType);
void CheckTfmVal(HintSeg* hSegList, Fixed* bandList, int32_t length);
void CheckVals(HintVal* vlst, bool vert);
bool FindLineSeg(Fixed loc, HintSeg* sL);
void FltnCurve(Cd c0, Cd c1, Cd c2, Cd c3, FltnRec* pfr);
bool InBlueBand(Fixed loc, int32_t n, Fixed* p);
void GenHPts(void);
void PreGenPts(void);
PathElt* GetDest(PathElt* cldest);
PathElt* GetClosedBy(PathElt* clsdby);
void GetEndPoint(PathElt* e, Fixed* x1p, Fixed* y1p);
void GetEndPoints(PathElt* p, Fixed* px0, Fixed* py0, Fixed* px1, Fixed* py1);
Fixed VertQuo(Fixed xk, Fixed yk, Fixed xl, Fixed yl);
Fixed HorzQuo(Fixed xk, Fixed yk, Fixed xl, Fixed yl);
bool IsTiny(PathElt* e);
bool IsShort(PathElt* e);
PathElt* NxtForBend(PathElt* p, Fixed* px2, Fixed* py2, Fixed* px3, Fixed* py3);
PathElt* PrvForBend(PathElt* p, Fixed* px2, Fixed* py2);
bool IsLower(PathElt* p);
bool IsUpper(PathElt* p);
bool CloseSegs(HintSeg* s1, HintSeg* s2, bool vert);

void DoPrune(void);
void PruneVVals(void);
void PruneHVals(void);
void MergeVals(bool vert);
void MergeFromMainHints(char ch);
void RoundPathCoords(void);
void MoveSubpathToEnd(PathElt* e);
void InitData(int32_t reason);
void InitFix(int32_t reason);
void InitGen(int32_t reason);
void InitPick(int32_t reason);
void AutoAddFlex(void);
bool SameHints(int32_t cn1, int32_t cn2);
bool PreCheckForHinting(void);
int32_t CountSubPaths(void);
void PickVVals(HintVal* gValList);
void PickHVals(HintVal* gValList);
void FindBestHVals(void);
void FindBestVVals(void);
void ReportAddFlex(void);
void ReportLinearCurve(PathElt* e, Fixed x0, Fixed y0, Fixed x1, Fixed y1);
void ReportNonHError(Fixed x0, Fixed y0, Fixed x1, Fixed y1);
void ReportNonVError(Fixed x0, Fixed y0, Fixed x1, Fixed y1);
void ExpectedMoveTo(PathElt* e);
void ReportMissingClosePath(void);
void ReportTryFlexNearMiss(Fixed x0, Fixed y0, Fixed x2, Fixed y2);
void ReportTryFlexError(bool CPflg, Fixed x, Fixed y);
void ReportSplit(PathElt* e);
void ReportRemFlare(PathElt* e, PathElt* e2, bool hFlg, int32_t i);
void ReportRemConflict(PathElt* e);
void ReportRemShortHints(Fixed ex, Fixed ey);
bool ResolveConflictBySplit(PathElt* e, bool Hflg, SegLnkLst* lnk1,
                            SegLnkLst* lnk2);
void ReportPossibleLoop(PathElt* e);
void ShowHVal(HintVal* val);
void ShowHVals(HintVal* lst);
void ReportAddHVal(HintVal* val);
void ShowVVal(HintVal* val);
void ShowVVals(HintVal* lst);
void ReportAddVVal(HintVal* val);
void ReportFndBstVal(HintSeg* seg, HintVal* val, bool hFlg);
void ReportCarry(Fixed l0, Fixed l1, Fixed loc, HintVal* hints, bool vert);
void LogHintInfo(HintPoint* pl);
void ReportStemNearMiss(bool vert, Fixed w, Fixed minW, Fixed b, Fixed t,
                        bool curve);
void ReportHintConflict(Fixed x0, Fixed y0, Fixed x1, Fixed y1, char ch);
void ReportDuplicates(Fixed x, Fixed y);
void ReportBBoxBogus(Fixed llx, Fixed lly, Fixed urx, Fixed ury);
void ReportMergeHVal(Fixed b0, Fixed t0, Fixed b1, Fixed t1, Fixed v0, Fixed s0,
                     Fixed v1, Fixed s1);
void ReportMergeVVal(Fixed l0, Fixed r0, Fixed l1, Fixed r1, Fixed v0, Fixed s0,
                     Fixed v1, Fixed s1);
void ReportPruneHVal(HintVal* val, HintVal* v, int32_t i);
void ReportPruneVVal(HintVal* val, HintVal* v, int32_t i);
unsigned char* InitShuffleSubpaths(void);
void MarkLinks(HintVal* vL, bool hFlg, unsigned char* links);
void DoShuffleSubpaths(unsigned char* links);
void CopyMainH(void);
void CopyMainV(void);
void RMovePoint(Fixed dx, Fixed dy, int32_t whichcp, PathElt* e);
void AddVSegment(Fixed from, Fixed to, Fixed loc, PathElt* p1, PathElt* p2,
                 int32_t typ, int32_t i);
void AddHSegment(Fixed from, Fixed to, Fixed loc, PathElt* p1, PathElt* p2,
                 int32_t typ, int32_t i);
void Delete(PathElt* e);
bool ReadGlyph(const char* srcglyph, bool forBlendData, bool readHints);
double FixToDbl(Fixed f);
bool CompareValues(HintVal* val1, HintVal* val2, int32_t factor,
                   int32_t ghstshift);
void SaveFile(void);
void CheckForMultiMoveTo(void);
#define STARTUP (0)
#define RESTART (1)

void ListHintInfo(void);

void InitAll(int32_t reason);

void AddVStem(Fixed right, Fixed left, bool curved);
void AddHStem(Fixed top, Fixed bottom, bool curved);

void AddGlyphExtremes(Fixed bot, Fixed top);

bool AutoHint(const ACFontInfo* fontinfo, const char* srcbezdata,
              bool extrahint, bool changeGlyph, bool roundCoords);

bool MergeGlyphPaths(const char** srcglyphs, int nmasters,
                     const char** masters, ACBuffer** outbuffers);

#endif /* AC_AC_H_ */
