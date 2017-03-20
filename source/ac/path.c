/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/
/* path.c */


#include "ac.h"

#if 0
#define GetSubpathNext(e) ((e)->type == CLOSEPATH)? GetDest(e) : (e)->next
#define GetSubpathPrev(e) ((e)->type == MOVETO)? GetClosedBy(e) : (e)->prev

static void NumberSubpath(e) register PPathElt e; {
  /* number the elements of the subpath starting at e */
  PPathElt first;
  int32_t cnt;
  first = e; cnt = 0;
  while (true) {
    e->count = cnt;
    e = GetSubpathNext(e);
    if (e == first) break;
    cnt++;
    }
  }

static int32_t TstClrLsts(l1, l2, flg)
  PSegLnkLst l1, l2; bool flg; {
  int32_t result, i;
  result = -1;
  if (l1 != NULL) while (l2 != NULL) {
    i = TestColorLst(l1, l2->lnk->seg->sLnk, flg, false);
    if (i == 0) return 0;
    if (i == 1) result = 1;
    l2 = l2->next;
    }
  return result;
  }

static int32_t ClrLstLen(lst) PSegLnkLst lst; {
  int32_t cnt = 0;
  while (lst != NULL) {
    cnt++; lst = lst->next; }
  return cnt; }

static bool SameClrLsts(l1, l2, flg)
  PSegLnkLst l1, l2; bool flg; {
  if (ClrLstLen(l1) != ClrLstLen(l2)) return false;
  return (TstClrLsts(l1, l2, flg) == -1) ? true : false;
  }

static void FindConflicts(e) PPathElt e; {
  /* find conflicts in subpath for e */
  PSegLnkLst hLst, vLst, phLst, pvLst;
  bool checked;
  PPathElt start, p;
  int32_t h, v;
  if (e->type != MOVETO) e = GetDest(e);
  while (true) {
    hLst = e->Hs;
    vLst = e->Vs;
    if (hLst == NULL && vLst == NULL) goto Nxt;
    /* search for previous subpath element with conflicting colors */
    start = e;
    p = GetSubpathPrev(e);
    checked = true;
    while (p != start) {
      if (checked) {
	if (p->type == CLOSEPATH) checked = false;
	}
      phLst = p->Hs;
      pvLst = p->Vs;
      if (phLst == NULL && pvLst == NULL) goto NxtP;
      if (hLst != NULL && phLst != NULL)
        h = TstClrLsts(hLst, phLst, YgoesUp);
      else if (hLst == NULL && phLst == NULL) h = -1;
      else h = 1;
      if (vLst != NULL && pvLst != NULL)
        v = TstClrLsts(vLst, pvLst, true);
      else if (vLst == NULL && pvLst == NULL) v = -1;
      else v = 1;
      if (checked && h == -1 && v == -1 &&
          ClrLstLen(hLst)==ClrLstLen(phLst) &&
	  ClrLstLen(vLst)==ClrLstLen(pvLst)) { /* same coloring */
	p = p->conflict;
	if (p != NULL) e->conflict = p;
	break;
        }
      if (h == 0 || v == 0) {
        e->conflict = p; break; }
      NxtP: p = GetSubpathPrev(p);
      }
    Nxt: e = e->next;
    if (e == NULL) break;
    if (e->type == MOVETO) break;
    }
  }

static int32_t CountConflicts(e) PPathElt e; {
  /* e is a proposed closepath. return number of color conflicts */
  int32_t conflicts, nc, c, cnt;
  PPathElt first, conflict;
  first = e; cnt = conflicts = nc = 0;
  NumberSubpath(e);
  while (true) {
    conflict = e->conflict;
    if (conflict == NULL) goto Nxt;
    c = conflict->count;
    if (c >= nc && c < cnt) { conflicts++; nc = cnt; }
    Nxt:
    e = GetSubpathNext(e); cnt++;
    if (e == first) break; }
  return conflicts;
  }

static bool TestColorSection(first, after)
  /* returns false if there is a conflict, true otherwise */
  PPathElt first, after; {
  PPathElt e;
  PSegLnkLst hLst, vLst, hPrv, vPrv;
  e = first; hPrv = vPrv = NULL;
  while (e != after) {
    hLst = e->Hs;
    vLst = e->Vs;
    if (hLst != NULL && !SameClrLsts(hLst,hPrv,YgoesUp)) {
      if (TestColorLst(hLst, Hprimary, YgoesUp, true)==0) return false;
      hPrv = hLst;
      }
    if (vLst != NULL && !SameClrLsts(vLst,vPrv,true)) {
      if (TestColorLst(vLst, Vprimary, true, true)==0) return false;
      vPrv = vLst;
      }
    e = GetSubpathNext(e); }
  return true; }

static bool StartsOkWithPrimaryClrs(cp)
  PPathElt cp; {
  /* return true if proposed cp yields subpath whose
     first coloring section is consistent with the primary coloring */
  PPathElt e, conflict, first;
  int32_t c, cnt;
  first = e = cp;
  NumberSubpath(e);
  cnt = 0;
  while (true) {
    conflict = e->conflict;
    if (conflict != NULL) {
      c = conflict->count;
      if (c < cnt) break; }
    e = GetSubpathNext(e);
    if (e == cp) break;
    cnt++; }
  /* e is start of new coloring in subpath */
  return TestColorSection(first, e); }

static bool OkCandidate(cp,conflict,isShort)
  PPathElt cp, conflict; bool isShort; {
  PPathElt cpConflict, nxt, nxtConflict;
  if (cp == conflict) return false;
  cpConflict = cp->conflict;
  if (cpConflict == conflict) return false;
  if (!isShort) return true;
  /* don't put short closepath between two conflicting elements */
  nxt = cp->next;
  if (nxt == NULL) return true;
  nxtConflict = nxt->conflict;
  if (nxtConflict == cp) return false;
  return true;
  }

static bool OkJunction(p,e) PPathElt p,e; {
  Fixed x, y, x0, y0, x1, y1, sm;
  if (p->isFlex && e->isFlex) return false;
  if (e->type == CURVETO) { x0 = e->x1; y0 = e->y1; }
  else GetEndPoint(e, &x0, &y0);
  if (p->type == CURVETO) { x1 = p->x2; y1 = p->y2; }
  else GetEndPoint(p->prev, &x1, &y1);
  GetEndPoint(p, &x, &y);
  return CheckSmoothness(x0, y0, x, y, x1, y1, &sm);
  }

static PPathElt FindMinConflict(e) PPathElt e; {
  PPathElt conflict, cp, cpnxt, bestCP, first, prevConflict;
  int32_t best, cnt;
  bool isShort;
  first = e;
  if (e->type != MOVETO) e = GetDest(e);
  best = 10000;
  prevConflict = bestCP = NULL;
  while (true) {
    conflict = e->conflict;
    if (conflict == NULL || conflict == prevConflict) goto Nxt;
    cp = e; isShort = false;
    while (true) { /* search for a possible place for a closepath */
      if ((cp->type == LINETO || cp->type == CLOSEPATH) && !IsShort(cp)) break;
      if (!OkCandidate(cp,conflict,isShort)) { cp = NULL; break; }
      cp = GetSubpathPrev(cp);
      }
    if (cp == NULL) { /* consider short cp */
      cp = e; isShort = true;
      while (true) {
	if (cp->type == CLOSEPATH || cp->type == LINETO) break;
        if (cp->type == CURVETO && cp != e) {
          cpnxt = GetSubpathNext(cp);
	  if (cpnxt->type == CURVETO && OkJunction(cp,cpnxt)) break; }
        if (!OkCandidate(cp,conflict,isShort)) { cp = NULL; break; }
        cp = GetSubpathPrev(cp);
        }
      }
    if (cp == NULL) goto Nxt;
    if (DEBUG) ReportConflictCheck(e, conflict, cp);
    cnt = CountConflicts(cp);
    if (isShort) {
      cnt++; /* bias against short closepaths */
      if (DEBUG) fprintf(OUTPUTBUFF, "short, "); }
    if (!StartsOkWithPrimaryClrs(cp)) {
      cnt++; /* bias against conflict with primary colors */
      if (DEBUG) fprintf(OUTPUTBUFF, "conflicts with primary, "); }
    if (DEBUG) ReportConflictCnt(cp, cnt);
    if (cnt < best || (cnt == best && cp->type == CLOSEPATH)) {
      /* break ties in favor of leaving closepath where it is */
      bestCP = cp; best = cnt; }
   Nxt: e = e->next;
    if (e == NULL) break;
    if (e->type == MOVETO) break;
    if (conflict != NULL) prevConflict = conflict;
    }
  if (DEBUG) {
    fprintf(OUTPUTBUFF, "final choice: cnt %d ", best);
    ReportBestCP(first, bestCP); }
  return bestCP;
  }

#define SubpathConflictsWithPrimary(e) \
    (!TestColorSection(e, GetClosedBy(e)))

static PPathElt NewCP(cp) PPathElt cp; {
  PPathElt first;
  first = cp;
  while (true) {
    if (cp->newCP) return cp;
    cp = GetSubpathPrev(cp);
    if (cp == first) return cp;
    }
  }

static bool ReorderSubpaths() {
  /* if first subpath conflicts with primary,
     and there is a later subpath that does not,
     do tailsubpaths until later one becomes first */
  PPathElt e, first;
  first = e = pathStart;
  if (!SubpathConflictsWithPrimary(e)) return false;
  while (true) {
    e = GetClosedBy(e);
    if (e == NULL) break;
    e = e->next; /* moveto */
    if (e == NULL) break;
    if (!SubpathConflictsWithPrimary(e)) {
      first = e; goto Rt; }
    }
  if (e == NULL) {
    /* if first subpath start with coloring that conflicts with primary,
       and there is a later subpath that does not,
       do tailsubpaths until later one becomes first */
    e = GetClosedBy(first);
    if (StartsOkWithPrimaryClrs(NewCP(e))) return false;
    while (true) {
      e = e->next; /* moveto */
      if (e == NULL) return false;
      e = GetClosedBy(e);
      if (e == NULL) return false;
      if (StartsOkWithPrimaryClrs(NewCP(e))) break;
      }
    }
  first = GetDest(e);
 Rt: if (DEBUG) ReportMoveSubpath(first, "front");
  while (true) {
    e = pathStart;
    if (e == first) break;
    if (DEBUG) ReportMoveSubpath(e, "end");
    MoveSubpathToEnd(e);
    }
  return true;
  }

bool RotateSubpaths(flg) bool flg; {
  PPathElt e, cp, nxt;
  bool chng = false, chngSub;
  DEBUG = flg;
  e = pathStart;
  if (DEBUG) PrintMessage("RotateSubpaths");
  while (e != NULL) {
    e->conflict = NULL;
    e->newCP = false;
    e = e->next;
    }
  e = pathStart;
  while (e != NULL) {
    FindConflicts(e);
    cp = FindMinConflict(e);
    e = GetClosedBy(e);
    nxt = e->next;
    if (cp == NULL) goto Nxt;
    if (cp->type == CLOSEPATH) goto Nxt;
    cp->newCP = true;
      /* dont change yet so preserve info for ReorderSubpaths */
    chng = true;
   Nxt: e = nxt;
    }
  chngSub = chng;
  if (DEBUG) PrintMessage("ReorderSubpaths");
  if (ReorderSubpaths()) chng = true;
  if (!chngSub) goto Done;
  e = pathStart; /* must reload since may have reordered */
  while (e != NULL) {
    nxt = GetClosedBy(e); /* closepath for this subpath */
    if (nxt != NULL) nxt = nxt->next; /* start of next subpath */
    while (e != NULL) {
      if (e->type == CLOSEPATH) break;
      if (e->newCP) {
        ReportRotateSubpath(e);
	/* fix this someday
	if (e->type == CURVETO) PointClosePath(cpEnd); 
        else ChangeClosePath();
	*/
        break;
        }
      e = e->next; }
    e = nxt; }
  Done:
  if (DEBUG) PrintMessage("done with ReorderSubpaths");
  return chng;
  }
#endif
