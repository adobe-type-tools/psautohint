/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */

#include <math.h>

#include "ac.h"
#include "bftoac.h"

extern void ReportRemVSeg(Fixed from, Fixed to, Fixed loc);
extern void ReportRemHSeg(Fixed from, Fixed to, Fixed loc);


static PSegLnkLst Hlnks, Vlnks;
static int32_t cpFrom, cpTo;

void InitGen(reason) int32_t reason; {
	int32_t i;
	switch (reason) {
		case STARTUP: case RESTART:
			for (i = 0; i < 4; i++) segLists[i] = NULL;
			Hlnks = Vlnks = NULL;
    }
}

static void LinkSegment(e, Hflg, seg)
PPathElt e; bool Hflg; PClrSeg seg; {
	PSegLnk newlnk;
	PSegLnkLst newlst, globlst;
	newlnk = (PSegLnk)Alloc(sizeof(SegLnk));
	newlnk->seg = seg;
	newlst = (PSegLnkLst)Alloc(sizeof(SegLnkLst));
	globlst = (PSegLnkLst)Alloc(sizeof(SegLnkLst));
	globlst->lnk = newlnk;
	newlst->lnk = newlnk;
	if (Hflg) {
		newlst->next = e->Hs; e->Hs = newlst;
		globlst->next = Hlnks; Hlnks = globlst; }
	else {
		newlst->next = e->Vs; e->Vs = newlst;
		globlst->next = Vlnks; Vlnks = globlst; }
}

static void CopySegmentLink(e1, e2, Hflg)
PPathElt e1, e2; bool Hflg; {
	/* copy reference to first link from e1 to e2 */
	PSegLnkLst newlst;
	newlst = (PSegLnkLst)Alloc(sizeof(SegLnkLst));
	if (Hflg) {
		newlst->lnk = e1->Hs->lnk; newlst->next = e2->Hs; e2->Hs = newlst; }
	else {
		newlst->lnk = e1->Vs->lnk; newlst->next = e2->Vs; e2->Vs = newlst; }
}

static void AddSegment(from,to,loc,lftLstNm,rghtLstNm,e1,e2,Hflg,typ)
Fixed from, to, loc; int32_t lftLstNm, rghtLstNm;
PPathElt e1, e2; bool Hflg; int32_t typ; {
	PClrSeg seg, segList, prevSeg;
	int32_t segNm;
	seg = (PClrSeg)Alloc(sizeof(ClrSeg));
	seg->sLoc = loc;
	if (from > to) { seg->sMax = from; seg->sMin = to; }
	else { seg->sMax = to; seg->sMin = from; }
	seg->sBonus = bonus;
	seg->sType = (int16_t)typ;
	if (e1 != NULL) {
		if (e1->type == CLOSEPATH) e1 = GetDest(e1);
		LinkSegment(e1, Hflg, seg);
		seg->sElt = e1;
    }
	if (e2 != NULL) {
		if (e2->type == CLOSEPATH) e2 = GetDest(e2);
		CopySegmentLink(e1, e2, Hflg);
		if (e1 == NULL || e2 == e1->prev) seg->sElt = e2;
    }
	segNm = (from > to)? lftLstNm: rghtLstNm;
	segList = segLists[segNm];
	prevSeg = NULL;
	while (true) { /* keep list in increasing order by sLoc */
		if (segList == NULL) { /* at end of list */
			if (prevSeg == NULL) { segLists[segNm] = seg; break; }
			prevSeg->sNxt = seg; break; }
		if (segList->sLoc >= loc) { /* insert before this one */
			if (prevSeg == NULL) segLists[segNm] = seg;
			else prevSeg->sNxt = seg;
			seg->sNxt = segList; break; }
		prevSeg = segList; segList = segList->sNxt; }
}

void AddVSegment(from,to,loc,p1,p2,typ,i) 
Fixed from, to, loc; PPathElt p1, p2; int32_t typ, i; {
 	if (DEBUG) ReportAddVSeg(from, to, loc, i);
	if (YgoesUp) AddSegment(from,to,loc,0,1,p1,p2,false,typ);
	else AddSegment(from,to,loc,1,0,p1,p2,false,typ); }

void AddHSegment(from,to,loc,p1,p2,typ,i)
Fixed from, to, loc; PPathElt p1, p2; int32_t typ, i; {
	if (DEBUG) ReportAddHSeg(from, to, loc, i);
	AddSegment(from,to,loc,2,3,p1,p2,true,typ); }

static Fixed CPFrom(cp2,cp3) Fixed cp2,cp3; {
    Fixed val = 2*(((cp3-cp2)*cpFrom)/200);  /*DEBUG 8 BIT: hack to get same rounding as old version */
    val += cp2;

     DEBUG_ROUND(val)
	return val; /* DEBUG 8 BIT to match results with 7 bit fractions */
}

static Fixed CPTo(cp0,cp1) Fixed cp0,cp1; {
    Fixed val = 2*(((cp1-cp0)*cpTo)/200); /*DEBUG 8 BIT: hack to get same rounding as old version */
    val += cp0;
    DEBUG_ROUND(val)
	return val; /* DEBUG 8 BIT to match results with 7 bit fractions */
}

static bool TestBend(x0,y0,x1,y1,x2,y2) Fixed x0, y0, x1, y1, x2, y2; {
	/* return true if bend angle is sharp enough (135 degrees or less) */
	float dx1, dy1, dx2, dy2;
	double dotprod, lensqprod;
	acfixtopflt(x1-x0, &dx1);
	acfixtopflt(y1-y0, &dy1);
	acfixtopflt(x2-x1, &dx2);
	acfixtopflt(y2-y1, &dy2);
	dotprod = dx1*dx2 + dy1*dy2;
	lensqprod = (dx1*dx1 + dy1*dy1) * (dx2*dx2 + dy2*dy2);
	return roundf((dotprod*dotprod / lensqprod) * 1000) / 1000 <= .5f;
}

#define TestTan(d1,d2) (abs(d1) > (abs(d2)*bendTan)/1000)
#define FRound(x) FTrunc(FRnd(x))

static bool IsCCW(x0,y0,x1,y1,x2,y2)
Fixed x0, y0, x1, y1, x2, y2; {
    /* returns true if (x0,y0) -> (x1,y1) -> (x2,y2) is counter clockwise
	 in character space */
	int32_t dx0, dy0, dx1, dy1;
	bool ccw;
	dx0 = FRound(x1-x0); dy0 = FRound(y1-y0);
	dx1 = FRound(x2-x1); dy1 = FRound(y2-y1);
	if (!YgoesUp) { dy0 = -dy0; dy1 = -dy1; }
	ccw = (dx0*dy1) >= (dx1*dy0);
	return ccw;
}

static void DoHBendsNxt(x0,y0,x1,y1,p)
Fixed x0, y0, x1, y1; PPathElt p; {
	Fixed x2, y2, delta, strt, end, x3, y3;
	bool ysame, ccw, above, doboth;
	if (y0 == y1) return;
	(void)NxtForBend(p,&x2,&y2,&x3,&y3);
	ysame = ProdLt0(y2-y1,y1-y0); /* y0 and y2 on same side of y1 */
	if (ysame ||
		(TestTan(x1-x2, y1-y2) &&
		 (ProdLt0(x2-x1,x1-x0) ||
          (IsVertical(x0,y0,x1,y1) && TestBend(x0,y0,x1,y1,x2,y2))))) {
			 delta = FixHalfMul(bendLength);
			 doboth = false;
			 if ((x0 <= x1 && x1 < x2) || (x0 < x1 && x1 <= x2)) {}
			 else if ((x2 < x1 && x1 <= x0) || (x2 <= x1 && x1 < x0))
				 delta = -delta;
			 else if (ysame) {
				 above = y0 > y1;
				 if (!YgoesUp) above = !above;
				 ccw = IsCCW(x0,y0,x1,y1,x2,y2);
				 if (above != ccw) delta = -delta;
			 }
			 else doboth = true;
			 strt = x1 - delta; end = x1 + delta;
			 AddHSegment(strt, end, y1, p, (PPathElt)NULL, sBEND, 0);
			 if (doboth)
				 AddHSegment(end, strt, y1, p, (PPathElt)NULL, sBEND, 1);
		 }
}

static void DoHBendsPrv(x0,y0,x1,y1,p)
Fixed x0, y0, x1, y1; PPathElt p; {
	Fixed x2, y2, delta, strt, end;
	bool ysame, ccw, above, doboth;
	if (y0 == y1) return;
	(void)PrvForBend(p,&x2,&y2);
	ysame = ProdLt0(y2-y0,y0-y1);
	if (ysame ||
		(TestTan(x0-x2,y0-y2) &&
		 (ProdLt0(x2-x0,x0-x1) ||
          (IsVertical(x0,y0,x1,y1) && TestBend(x2,y2,x0,y0,x1,y1))))) {
			 delta = FixHalfMul(bendLength);
			 doboth = false;
			 if ((x2 < x0 && x0 <= x1) || (x2 <= x0 && x0 < x1)) {}
			 else if ((x1 < x0 && x0 <= x2) || (x1 <= x0 && x0 < x2))
				 delta = -delta;
			 else if (ysame) {
				 above = (y2 > y0);
				 if (!YgoesUp) above = !above;
				 ccw = IsCCW(x2,y2,x0,y0,x1,y1);
				 if (above != ccw) delta = -delta;
			 }
			 strt = x0 - delta; end = x0 + delta;
			 AddHSegment(strt, end, y0, p->prev, (PPathElt)NULL, sBEND, 2);
			 if (doboth)
				 AddHSegment(end, strt, y0, p->prev, (PPathElt)NULL, sBEND, 3);
		 }
}

static void DoVBendsNxt(x0,y0,x1,y1,p)
Fixed x0, y0, x1, y1; PPathElt p; {
	Fixed x2, y2, delta, strt, end, x3, y3;
	bool xsame, ccw, right, doboth;
	if (x0 == x1) return;
	(void)NxtForBend(p,&x2,&y2,&x3,&y3);
	xsame = ProdLt0(x2-x1,x1-x0);
	if (xsame ||
		(TestTan(y1-y2,x1-x2) &&
		 (ProdLt0(y2-y1,y1-y0) ||
          (IsHorizontal(x0,y0,x1,y1) && TestBend(x0,y0,x1,y1,x2,y2))))) {
			 delta = FixHalfMul(bendLength);
			 doboth = false;
			 if ((y0 <= y1 && y1 < y2) || (y0 < y1 && y1 <= y2)) {}
			 else if ((y2 < y1 && y1 <= y0) || (y2 <= y1 && y1 < y0))
				 delta = -delta;
			 else if (xsame) {
				 right = x0 > x1;
				 ccw = IsCCW(x0,y0,x1,y1,x2,y2);
				 if (right != ccw) delta = -delta;
				 if (!YgoesUp) delta = -delta;
			 }
			 else doboth = true;
			 strt = y1 - delta; end = y1 + delta;
			 AddVSegment(strt, end, x1, p, (PPathElt)NULL, sBEND, 0);
			 if (doboth)
				 AddVSegment(end, strt, x1, p, (PPathElt)NULL, sBEND, 1);
		 }
}

static void DoVBendsPrv(x0,y0,x1,y1,p)
Fixed x0, y0, x1, y1; PPathElt p; {
	Fixed x2, y2, delta, strt, end;
	bool xsame, ccw, right, doboth;
	if (x0 == x1) return;
	(void)PrvForBend(p,&x2,&y2);
	xsame = ProdLt0(x2-x0,x0-x1);
	if (xsame ||
		(TestTan(y0-y2,x0-x2) &&
		 (ProdLt0(y2-y0,y0-y1) ||
          (IsHorizontal(x0,y0,x1,y1) && TestBend(x2,y2,x0,y0,x1,y1))))) {
			 delta = FixHalfMul(bendLength);
			 doboth = false;
			 if ((y2 < y0 && y0 <= y1) || (y2 <= y0 && y0 < y1)) {}
			 else if ((y1 < y0 && y0 <= y2) || (y1 <= y0 && y0 < y2))
				 delta = -delta;
			 else if (xsame) {
				 right = x0 > x1;
				 ccw = IsCCW(x2,y2,x0,y0,x1,y1);
				 if (right != ccw) delta = -delta;
				 if (!YgoesUp) delta = -delta;
			 }
			 strt = y0 - delta; end = y0 + delta;
			 AddVSegment(strt, end, x0, p->prev, (PPathElt)NULL, sBEND, 2);
			 if (doboth)
				 AddVSegment(end, strt, x0, p->prev, (PPathElt)NULL, sBEND, 3);
		 }
}

static void MergeLnkSegs(seg1, seg2, lst)
PSegLnkLst lst; PClrSeg seg1, seg2; {
	/* replace lnk refs to seg1 by seg2 */
	PSegLnk lnk;
	while (lst != NULL) {
		lnk = lst->lnk;
		if (lnk->seg == seg1) lnk->seg = seg2;
		lst = lst->next; }
}

static void MergeHSegs(seg1, seg2) PClrSeg seg1, seg2; {
	MergeLnkSegs(seg1, seg2, Hlnks);
}

static void MergeVSegs(seg1, seg2) PClrSeg seg1, seg2; {
	MergeLnkSegs(seg1, seg2, Vlnks);
}

static void ReportRemSeg(l, lst) int32_t l; PClrSeg lst; {
	Fixed from, to, loc;
	/* this assumes !YgoesUp */
	switch (l) {
		case 1: case 2: from = lst->sMax; to = lst->sMin; break;
		case 0: case 3: from = lst->sMin; to = lst->sMax; break;
    }
	loc = lst->sLoc;
	switch (l) {
		case 0: case 1: ReportRemVSeg(from, to, loc); break;
		case 2: case 3: ReportRemHSeg(from, to, loc); break;
    }
}

/* Filters out bogus bend segments. */
static void RemExtraBends(l0, l1) int32_t l0, l1;
{
	register PClrSeg lst0, lst, n, p;
	PClrSeg nxt, prv;
	register Fixed loc0, loc;
	lst0 = segLists[l0]; prv = NULL;
	while (lst0 != NULL) {
		nxt = lst0->sNxt; loc0 = lst0->sLoc;
		lst = segLists[l1]; p = NULL;
		while (lst != NULL) {
			n = lst->sNxt; loc = lst->sLoc;
			if (loc > loc0) break; /* list in increasing order by sLoc */
			if (loc == loc0 &&
				lst->sMin < lst0->sMax && lst->sMax > lst0->sMin) {
				if (lst0->sType == sBEND && lst->sType != sBEND &&
					lst->sType != sGHOST &&
					(lst->sMax - lst->sMin) > (lst0->sMax - lst0->sMin)*3) {
					/* delete lst0 */
					if (prv == NULL) segLists[l0] = nxt;
					else prv->sNxt = nxt;
					if (DEBUG) ReportRemSeg(l0, lst0);
					lst0 = prv;
					break;
				}
				if (lst->sType == sBEND && lst0->sType != sBEND &&
					lst0->sType != sGHOST &&
					(lst0->sMax - lst0->sMin) > (lst->sMax - lst->sMin)*3) {
					/* delete lst */
					if (p == NULL) segLists[l1] = n;
					else p->sNxt = n;
					if (DEBUG) ReportRemSeg(l1, lst);
					lst = p;
				}
			}
			p = lst; lst = n;
		}
		prv = lst0; lst0 = nxt;
    }
}

static void CompactList(i,nm)
int32_t i; void (*nm)(); {
	PClrSeg lst, prv, nxtprv, nxt;
	Fixed lstmin, lstmax, nxtmin, nxtmax;
	bool flg;
	lst = segLists[i]; prv = NULL;
	while (lst != NULL) {
		nxt = lst->sNxt; nxtprv = lst;
		while (true) {
			if ((nxt == NULL) || (nxt->sLoc > lst->sLoc)) {
				flg = true; break; }
			lstmin = lst->sMin; lstmax = lst->sMax;
			nxtmin = nxt->sMin; nxtmax = nxt->sMax;
			if (lstmax >= nxtmin && lstmin <= nxtmax) {
				/* do not worry about YgoesUp since "sMax" is really max in
				 device space, not in character space */
				if (abs(lstmax-lstmin) > abs(nxtmax-nxtmin)) {
					/* merge into lst and remove nxt */
					(*nm)(nxt, lst);
					lst->sMin = NUMMIN(lstmin,nxtmin);
					lst->sMax = NUMMAX(lstmax,nxtmax);
					lst->sBonus = NUMMAX(lst->sBonus, nxt->sBonus);
					nxtprv->sNxt = nxt->sNxt; }
				else { /* merge into nxt and remove lst */
					(*nm)(lst, nxt);
					nxt->sMin = NUMMIN(lstmin,nxtmin);
					nxt->sMax = NUMMAX(lstmax,nxtmax);
					nxt->sBonus = NUMMAX(lst->sBonus, nxt->sBonus);
					lst = lst->sNxt;
					if (prv == NULL) segLists[i] = lst;
					else prv->sNxt = lst; }
				flg = false; break; }
			nxtprv = nxt; nxt = nxt->sNxt; }
		if (flg) { prv = lst; lst = lst->sNxt; }
    }
}

static Fixed PickVSpot(x0,y0,x1,y1,px1,py1,px2,py2,prvx,prvy,nxtx,nxty)
Fixed x0,y0,x1,y1,px1,py1,px2,py2,prvx,prvy,nxtx,nxty; {
	register Fixed a1, a2;
	if (x0 == px1 && x1 != px2) return x0;
	if (x0 != px1 && x1 == px2) return x1;
	if (x0 == prvx && x1 != nxtx) return x0;
	if (x0 != prvx && x1 == nxtx) return x1;
	a1 = abs(py1-y0);
	a2 = abs(py2-y1);
	if (a1 > a2) return x0;
	a1 = abs(py2-y1);
	a2 = abs(py1-y0);
	if (a1 > a2) return x1;
	if (x0 == prvx && x1 == nxtx) {
		a1 = abs(y0-prvy); a2 = abs(y1-nxty);
		if (a1 > a2) return x0;
		return x1;
    }
	return FixHalfMul(x0 + x1);
}

static Fixed AdjDist(d,q) Fixed d,q; {
	Fixed val;
	if (q == FixOne)
    {
        DEBUG_ROUND(d)/* DEBUG 8 BIT */
        return d;
    }
	val =  (d * q) >>8;
    DEBUG_ROUND(val)/* DEBUG 8 BIT */
    return val;
}

/* serifs of ITCGaramond Ultra have points that are not quite horizontal 
 e.g., in H: (53,51)(74,52)(116,54) 
 the following was added to let these through */
static bool TstFlat(dmn,dmx) Fixed dmn, dmx; {
	if (dmn < 0) dmn = -dmn; if (dmx < 0) dmx = -dmx;
	return (dmx >= PSDist(50) && dmn <= PSDist(4));
}

static bool NxtHorz(x,y,p) Fixed x,y; PPathElt p; {
	Fixed x2, y2, x3, y3;
	p = NxtForBend(p,&x2,&y2,&x3,&y3);
	return TstFlat(y2-y,x2-x);
}

static bool PrvHorz(x,y,p) Fixed x,y; PPathElt p; {
	Fixed x2, y2;
	p = PrvForBend(p,&x2,&y2);
	return TstFlat(y2-y,x2-x);
}

static bool NxtVert(x,y,p) Fixed x,y; PPathElt p; {
	Fixed x2, y2, x3, y3;
	p = NxtForBend(p,&x2,&y2,&x3,&y3);
	return TstFlat(x2-x,y2-y);
}

static bool PrvVert(x,y,p) Fixed x,y; PPathElt p; {
	Fixed x2, y2;
	p = PrvForBend(p,&x2,&y2);
	return TstFlat(x2-x,y2-y);
}

/* PrvSameDir and NxtSameDir were added to check the direction of a
 path and not add a band if the point is not at an extreme and is
 going in the same direction as the previous path. */  
static bool TstSameDir(x0,y0,x1,y1,x2,y2)
Fixed x0, y0, x1, y1, x2, y2; {
	if (ProdLt0(y0-y1,y1-y2) || ProdLt0(x0-x1,x1-x2))
		return false;
	return !TestBend(x0,y0,x1,y1,x2,y2);
}

static bool PrvSameDir(x0,y0,x1,y1,p)
Fixed x0,y0,x1,y1; PPathElt p; {
	Fixed x2, y2;
	p = PrvForBend(p,&x2,&y2);
	if (p != NULL && p->type == CURVETO && p->prev != NULL)
		GetEndPoint(p->prev,&x2,&y2);
	return TstSameDir(x0,y0,x1,y1,x2,y2);
}

static bool NxtSameDir(x0,y0,x1,y1,p)
Fixed x0,y0,x1,y1; PPathElt p; {
	Fixed x2, y2, x3, y3;
	p = NxtForBend(p,&x2,&y2,&x3,&y3);
	if (p != NULL && p->type == CURVETO) {
		x2 = p->x3; y2 = p->y3; }
	return TstSameDir(x0,y0,x1,y1,x2,y2);
}

void GenVPts(specialCharType) int32_t specialCharType; {
	/* specialCharType 1 = upper; -1 = lower; 0 = neither */
	PPathElt p, fl;
	bool isVert, flex1, flex2;
	Fixed flx0, fly0, llx, lly, urx, ury, yavg, yend, ydist, q, q2;
	Fixed prvx, prvy, nxtx, nxty, xx, yy, yd2;
	p = pathStart;
	flex1 = flex2 = false;
	cpTo = CPpercent; cpFrom = 100 - cpTo;  
	flx0 = fly0 = 0; fl = NULL;
	while (p != NULL) {
		Fixed x0, y0, x1, y1;
		GetEndPoints(p,&x0,&y0,&x1,&y1);
		if (p->type == CURVETO) {
			Fixed px1, py1, px2, py2;
			isVert = false;
			if (p->isFlex) {
				if (flex1) {
					if (IsVertical(flx0,fly0,x1,y1))
						AddVSegment(fly0,y1,x1,fl->prev,p,sLINE,4);
					flex1 = false; flex2 = true; }
				else {
					flex1 = true; flex2 = false;
					flx0 = x0; fly0 = y0; fl = p; }}
			else flex1 = flex2 = false;
			px1 = p->x1; py1 = p->y1; px2 = p->x2; py2 = p->y2;
			if (!flex2) {
				if ((q=VertQuo(px1,py1,x0,y0)) == 0) /* first two not vertical */
					DoVBendsPrv(x0,y0,px1,py1,p);
				else {
					isVert = true;
					if (px1 == x0 ||
						(px2 != x1 &&
						 (PrvVert(px1,py1,p) || !PrvSameDir(x1,y1,x0,y0,p)))) {
							if ((q2=VertQuo(px2,py2,x0,y0)) > 0 &&
								ProdGe0(py1-y0,py2-y0) &&
								abs(py2-y0) > abs(py1-y0)) {
								ydist = AdjDist(CPTo(py1,py2)-y0, q2); 
								yend = AdjDist(CPTo(y0,py1)-y0, q);
								if (abs(yend) > abs(ydist)) ydist = yend;
								AddVSegment(y0,y0+ydist,x0,p->prev,p,sCURVE,5);
							}
							else {
								ydist = AdjDist(CPTo(y0,py1)-y0,q);
								AddVSegment(y0,CPTo(y0,py1),x0,p->prev,p,sCURVE,6);
							}
						}
				}
			}
			if (!flex1) {
				if ((q=VertQuo(px2,py2,x1,y1)) == 0) /* last 2 not vertical */
					DoVBendsNxt(px2,py2,x1,y1,p);
				else if (px2==x1 ||
						 (px1 != x0 &&
						  (NxtVert(px2,py2,p) || !NxtSameDir(x0,y0,x1,y1,p)))) {
							 ydist = AdjDist(y1-CPFrom(py2,y1),q);
							 isVert = true;
							 q2 = VertQuo(x0,y0,x1,y1);
							 yd2 = (q2 > 0) ? AdjDist(y1-y0, q2) : 0;
							 if (isVert && q2 > 0 && abs(yd2) > abs(ydist)) {
								 if (x0==px1 && px1==px2 && px2==x1)
									 ReportLinearCurve(p, x0, y0, x1, y1);
								 ydist = FixHalfMul(yd2);
								 yavg = FixHalfMul(y0+y1);
								 (void)PrvForBend(p,&prvx,&prvy);
								 (void)NxtForBend(p,&nxtx,&nxty,&xx,&yy);
								 AddVSegment(yavg-ydist,yavg+ydist,
											 PickVSpot(x0,y0,x1,y1,px1,py1,px2,py2,prvx,prvy,nxtx,nxty),
											 p,(PPathElt)NULL,sCURVE,7);
							 }
							 else {
								 q2=VertQuo(px1,py1,x1,y1);
								 if (q2 > 0 && ProdGe0(py1-y1,py2-y1) &&
									 abs(py2-y1) < abs(py1-y1)) {
									 yend = AdjDist(y1-CPFrom(py1,py2),q2);
									 if (abs(yend) > abs(ydist)) ydist = yend;
									 AddVSegment(y1-ydist,y1,x1,p,(PPathElt)NULL,sCURVE,8);
								 }
								 else
									 AddVSegment(y1-ydist,y1,x1,p,(PPathElt)NULL,sCURVE,9);
							 }
						 }
			}
			if (!flex1 && !flex2) {
				Fixed minx, maxx;
				maxx = NUMMAX(x0,x1); minx = NUMMIN(x0,x1);
				if (px1-maxx >= FixTwo || px2-maxx >= FixTwo ||
					px1-minx <= FixTwo || px2-minx <= FixTwo) {
					FindCurveBBox(x0,y0,px1,py1,px2,py2,x1,y1,&llx,&lly,&urx,&ury);
					if (urx-maxx > FixTwo || minx-llx > FixTwo) {
						Fixed loc, frst, lst;
						loc = (minx-llx > urx-maxx)? llx : urx;
						CheckBBoxEdge(p,true,loc,&frst,&lst);
						yavg = FixHalfMul(frst+lst);
						ydist = (frst==lst)? (y1-y0)/10 : FixHalfMul(lst-frst);
						if (abs(ydist) < bendLength)
							ydist = (ydist > 0) ?
							FixHalfMul(bendLength) : FixHalfMul(-bendLength);
						AddVSegment(yavg-ydist, yavg+ydist, loc,
									p, (PPathElt)NULL, sCURVE, 10);
					}
				}
			}
		}
		else if (p->type == MOVETO) {
			bonus = 0;
			if (specialCharType == -1) {
				if (IsLower(p)) bonus = FixInt(200); }
			else if (specialCharType == 1) {
				if (IsUpper(p)) bonus = FixInt(200); }}
		else if (!IsTiny(p)) {
			if ((q=VertQuo(x0,y0,x1,y1)) > 0) {
				if (x0 == x1)
					AddVSegment(y0,y1,x0,p->prev,p,sLINE,11);
				else {
					if (q < FixQuarter) q = FixQuarter;
					ydist = FixHalfMul(AdjDist(y1-y0,q));
					yavg = FixHalfMul(y0+y1);
					(void)PrvForBend(p,&prvx,&prvy);
					(void)NxtForBend(p,&nxtx,&nxty,&xx,&yy);
					AddVSegment(yavg-ydist,yavg+ydist,
								PickVSpot(x0,y0,x1,y1,x0,y0,x1,y1,prvx,prvy,nxtx,nxty),
								p,(PPathElt)NULL,sLINE,12);
					if (abs(x0-x1) <= FixTwo)
						ReportNonVError(x0, y0, x1, y1);
				}
			}
			else {
				DoVBendsNxt(x0,y0,x1,y1,p);
				DoVBendsPrv(x0,y0,x1,y1,p); }}
		p = p->next; }
	CompactList(0, MergeVSegs);
	CompactList(1, MergeVSegs);
	RemExtraBends(0, 1);
	leftList = segLists[0];
	rightList = segLists[1];
}

bool InBlueBand(loc,n,p) Fixed loc; register Fixed *p; int32_t n; {
	register int i;
	register Fixed y;
	if (n <= 0) return false;
	y = itfmy(loc);
	/* Augment the blue band by bluefuzz in each direction.  This will
     result in "near misses" being colored and so adjusted by the
     PS interpreter. */
	for (i=0; i < n; i += 2)
		if ((p[i]-bluefuzz) <= y &&
			(p[i+1]+bluefuzz) >= y) return true;
	return false; }

static Fixed PickHSpot(x0,y0,x1,y1,xdist,px1,py1,px2,py2,prvx,prvy,nxtx,nxty)
Fixed x0,y0,x1,y1,xdist,px1,py1,px2,py2,prvx,prvy,nxtx,nxty; {
	bool topSeg = (xdist < 0)? true : false;
	Fixed upper, lower;
	bool inBlue0, inBlue1;
	if (topSeg) {
		inBlue0 = InBlueBand(y0, lenTopBands, topBands);
		inBlue1 = InBlueBand(y1, lenTopBands, topBands);
    }
	else {
		inBlue0 = InBlueBand(y0, lenBotBands, botBands);
		inBlue1 = InBlueBand(y1, lenBotBands, botBands);
    }
	if (inBlue0 && !inBlue1) return y0;
	if (inBlue1 && !inBlue0) return y1;
	if (y0 == py1 && y1 != py2) return y0;
	if (y0 != py1 && y1 == py2) return y1;
	if (y0 == prvy && y1 != nxty) return y0;
	if (y0 != prvy && y1 == nxty) return y1;
	if (inBlue0 && inBlue1) {
		if (y0 > y1) { upper = y0; lower = y1; }
		else { upper = y1; lower = y0; }
		if (!YgoesUp) { Fixed tmp = lower; lower = upper; upper = tmp; }
		return topSeg ? upper : lower;
    }
	if (abs(px1-x0) > abs(px2-x1)) return y0;
	if (abs(px2-x1) > abs(px1-x0)) return y1;
	if (y0 == prvy && y1 == nxty) {
		if (abs(x0-prvx) > abs(x1-nxtx)) return y0;
		return y1;
    }
	return FixHalfMul(y0 + y1);
}

void GenHPts() {
	PPathElt p, fl;
	bool isHoriz, flex1, flex2;
	Fixed flx0, fly0, llx, lly, urx, ury, xavg, xend, xdist, q, q2;
	Fixed prvx, prvy, nxtx, nxty, xx, yy, xd2;
	p = pathStart;
	bonus = 0; flx0 = fly0 = 0; fl = NULL;
	flex1 = flex2 = false;
	cpTo = CPpercent;
	cpFrom = 100 - cpTo;
	while (p != NULL) {
		Fixed x0, y0, x1, y1;
		GetEndPoints(p,&x0,&y0,&x1,&y1);
		if (p->type == CURVETO) {
			Fixed px1, py1, px2, py2;
			isHoriz = false;
			if (p->isFlex) {
				if (flex1) {
					flex1 = false; flex2 = true;
					if (IsHorizontal(flx0,fly0,x1,y1))
						AddHSegment(flx0,x1,y1,fl->prev,p,sLINE,4);
				}
				else {
					flex1 = true; flex2 = false;
					flx0 = x0; fly0 = y0; fl = p; }}
			else flex1 = flex2 = false;
			px1 = p->x1; py1 = p->y1; px2 = p->x2; py2 = p->y2;
			if (!flex2) {
				if ((q=HorzQuo(px1,py1,x0,y0)) == 0)
					DoHBendsPrv(x0,y0,px1,py1,p);
				else {
					isHoriz = true;
					if (py1 == y0 ||
						(py2 != y1 &&
						 (PrvHorz(px1,py1,p) || !PrvSameDir(x1,y1,x0,y0,p)))) {
							if ((q2=HorzQuo(px2,py2,x0,y0)) > 0 &&
								ProdGe0(px1-x0,px2-x0) &&
								abs(px2-x0) > abs(px1-x0)) {
								xdist = AdjDist(CPTo(px1,px2)-x0,q2);
								xend = AdjDist(CPTo(x0,px1)-x0,q);
								if (abs(xend) > abs(xdist)) xdist = xend;
								AddHSegment(x0,x0+xdist,y0,p->prev,p,sCURVE,5);
							}
							else {
								xdist = AdjDist(CPTo(x0,px1)-x0,q);
								AddHSegment(x0,x0+xdist,y0,p->prev,p,sCURVE,6);
							}
						}
				}
			}
			if (!flex1) {
				if ((q=HorzQuo(px2,py2,x1,y1)) == 0) 
					DoHBendsNxt(px2,py2,x1,y1,p);
				else if (py2 == y1 ||
						 (py1 != y0 &&
						  (NxtHorz(px2,py2,p) || !NxtSameDir(x0,y0,x1,y1,p)))) {
							 xdist = AdjDist(x1-CPFrom(px2,x1),q);
							 q2=HorzQuo(x0,y0,x1,y1);
							 isHoriz = true;
							 xd2 = (q2 > 0)? AdjDist(x1-x0,q2) : 0;
							 if (isHoriz && q2 > 0 && abs(xd2) > abs(xdist)) {
								 Fixed hspot;
								 if (y0==py1 && py1==py2 && py2==y1)
									 ReportLinearCurve(p, x0, y0, x1, y1);
								 (void)PrvForBend(p,&prvx,&prvy);
								 (void)NxtForBend(p,&nxtx,&nxty,&xx,&yy);
								 xdist = FixHalfMul(xd2);
								 xavg = FixHalfMul(x0+x1);
								 hspot = PickHSpot(
												   x0,y0,x1,y1,xdist,px1,py1,px2,py2,prvx,prvy,nxtx,nxty);
								 AddHSegment(
											 xavg-xdist,xavg+xdist,hspot,p,(PPathElt)NULL,sCURVE,7);
							 }
							 else {
								 q2=HorzQuo(px1,py1,x1,y1);
								 if (q2 > 0 && ProdGe0(px1-x1,px2-x1) &&
									 abs(px2-x1) < abs(px1-x1)) {
									 xend = AdjDist(x1-CPFrom(px1,px2),q2);
									 if (abs(xend) > abs(xdist)) xdist = xend;
									 AddHSegment(x1-xdist,x1,y1,p,(PPathElt)NULL,sCURVE,8);
								 }
								 else
									 AddHSegment(x1-xdist,x1,y1,p,(PPathElt)NULL,sCURVE,9);
							 }
						 }
			}
			if (!flex1 && !flex2) {
				Fixed miny, maxy; 
				maxy = NUMMAX(y0,y1); miny = NUMMIN(y0,y1); 
				if (py1-maxy >= FixTwo || py2-maxy >= FixTwo ||
					py1-miny <= FixTwo || py2-miny <= FixTwo) {
					FindCurveBBox(x0,y0,px1,py1,px2,py2,x1,y1,&llx,&lly,&urx,&ury);
					if (ury-maxy > FixTwo || miny-lly > FixTwo) {
						Fixed loc, frst, lst;
						loc = (miny-lly > ury-maxy)? lly : ury;
						CheckBBoxEdge(p,false,loc,&frst,&lst);
						xavg = FixHalfMul(frst+lst);
						xdist = (frst==lst)? (x1-x0)/10 : FixHalfMul(lst-frst);
						if (abs(xdist) < bendLength)
							xdist = (xdist > 0.0) ?
							FixHalfMul(bendLength) : FixHalfMul(-bendLength);
						AddHSegment(
									xavg-xdist,xavg+xdist,loc,p,(PPathElt)NULL,sCURVE,10);
					}
				}
			}
		}
		else if (p->type != MOVETO && !IsTiny(p)) {
			if ((q=HorzQuo(x0,y0,x1,y1)) > 0) {
				if (y0 == y1)
					AddHSegment(x0,x1,y0,p->prev,p,sLINE,11);
				else {
					if (q < FixQuarter) q = FixQuarter;
					xdist = FixHalfMul(AdjDist(x1-x0,q));
					xavg = FixHalfMul(x0+x1);
					(void)PrvForBend(p,&prvx,&prvy);
					(void)NxtForBend(p,&nxtx,&nxty,&xx,&yy);
					yy = PickHSpot(x0,y0,x1,y1,xdist,x0,y0,x1,y1,prvx,prvy,nxtx,nxty);
					AddHSegment(xavg-xdist,xavg+xdist,yy,p->prev,p,sLINE,12);
					if (abs(y0-y1) <= FixTwo)
						ReportNonHError(x0, y0, x1, y1);
				}
			}
			else {
				DoHBendsNxt(x0,y0,x1,y1,p);
				DoHBendsPrv(x0,y0,x1,y1,p); }}
		p = p->next; }
	CompactList(2, MergeHSegs);
	CompactList(3, MergeHSegs);
	RemExtraBends(2, 3);
	topList = segLists[2]; /* this is probably unnecessary */
	botList = segLists[3];
	CheckTfmVal(topList, topBands, lenTopBands);
	CheckTfmVal(botList, botBands, lenBotBands);
}

void PreGenPts() {
	Hlnks = Vlnks = NULL;
	segLists[0] = NULL; segLists[1] = NULL;
	segLists[2] = NULL; segLists[3] = NULL;
}

