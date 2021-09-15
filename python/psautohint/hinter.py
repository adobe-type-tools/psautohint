# Copyright 2021 Adobe. All rights reserved.

"""
Associates segments with points on a pathElement, then builds
these into candidate stem pairs that are evaluated and pruned
to an optimal set.
"""

import logging
import bisect
import math
from copy import copy
from abc import abstractmethod

from fontTools.misc.bezierTools import solveCubic

from .glyphData import pt, feq, fne, stem
from .hintstate import hintSegment, stemValue, glyphHintState

log = logging.getLogger(__name__)


class hinter:
    @staticmethod
    def diffSign(a, b):
        return fne(a, 0) and fne(b, 0) and ((a > 0) != (b > 0))

    @staticmethod
    def sameSign(a, b):
        return fne(a, 0) and fne(b, 0) and ((a > 0) == (b > 0))

    def __init__(self, options):
        self.MaxStemDist = 150
#        self.DMin = 50 # Flex stuff handled in glyphData output
#        self.Delta = 0
        self.InitBigDist = self.MaxStemDist
        self.BigDistFactor = 23.0 / 20.0
        self.MinDist = 7
        self.GhostWidth = 20
        self.GhostLength = 4
        self.GhostVal = 20
        self.GhostSpecial = 2
        self.BendLength = 2
        self.BendTangent = 0.55735  # 30 sin 30 cos div abs == .57735
        self.Theta = 0.38  # Must be <= .38 for Ryumin-Light 32
        self.MinValue = 0.0039
        self.MaxValue = 8000000.0
        self.PruneA = 50
        self.PruneB = 0.0117187
        self.PruneC = 100  # XXX
        self.PruneD = 1
        self.PruneValue = 0.0117187
        self.PruneFactor = 3.0
        self.PruneFactorLt = 10.0 / 3.0
        self.PruneDistance = 10
        self.MuchPF = 50
        self.VeryMuchPF = 100
        self.CPfrac = 0.4
        self.ConflictValMin = 50.0
        self.ConflictValMult = 20.0
        self.BandMargin = 30
        self.MaxFlare = 10
        self.MaxBendMerge = 6
        self.MinHintElementLength = 12
        self.MaxFlex = 20
        self.FlexLengthRatioCutoff = 0.11  # .33^2 (ratio of 1:3 or better)
        self.FlexCand = 4
        self.SCurveTangent = 0.025
        self.CPfrac = 0.4
        self.OppoFlatMin = 4
        self.FlatMax = 50
        self.ExtremaDist = 20
        self.NearFuzz = 6
        self.NoOverlapPenalty = 7.0 / 5.0
        self.CloseMerge = 20
        self.MaxMerge = 2
        self.SFactor = 20
        self.SpcBonus = 1000
        self.SpecialCharBonus = 200
        self.GhostFactor = 1.0 / 8.0
        self.DeltaDiffMin = .05
        self.DeltaDiffReport = 3

        self.FlexStrict = True

        self.HasFlex = False
        self.options = options

        self.glyph = None
        self.fddict = None
        self.report = None
        self.name = None

    def setGlyph(self, fddict, report, glyph, name, clearPrev=True):
        self.fddict = fddict
        self.report = report
        self.glyph = glyph
        self.name = name
        self.HasFlex = False
        self.Bonus = None
        self.Pruning = True
        if self.isV():
            self.hs = glyph.vhs = glyphHintState()
        else:
            self.hs = glyph.hhs = glyphHintState()

    def resetForHinting(self):
        self.Bonus = None
        self.Pruning = True
        if self.isV():
            self.hs = self.glyph.vhs = glyphHintState()
        else:
            self.hs = self.glyph.hhs = glyphHintState()

    def info(self, msg):
        log.info(f"{self.name}: " + msg)

    def warning(self, msg):
        log.warning(f"{self.name}: " + msg)

    def error(self, msg):
        log.error(f"{self.name}: " + msg)

    def debug(self, msg):
        log.debug(f"{self.name}: " + msg)

    @abstractmethod
    def startFlex(self):
        pass

    @abstractmethod
    def startHint(self):
        pass

    @abstractmethod
    def startMaskConvert(self):
        pass

    @abstractmethod
    def isV(self):
        pass

    @abstractmethod
    def segmentLists(self):
        pass

    @abstractmethod
    def dominantStems(self):
        pass

    @abstractmethod
    def isCounterGlyph(self):
        pass

    # Flex
    def linearFlexOK(self):
        return False

    def addFlex(self, force=True, inited=False):
        if not inited and self.glyph.flex_count != 0:
            if force:
                self.glyph.clearFlex()
                self.info("Clearing existing flex hints")
            else:
                self.info("Already has flex hints, skipping addFlex")
                return
        self.startFlex()
        for c in self.glyph:
            self.tryFlex(c)

        pt.clearAlign()

    def tryFlex(self, c):
        # Not a curve, already flexed, or flex depth would be too large
        if not c or c.isLine() or c.flex or c.s.a_dist(c.e) > self.MaxFlex:
            return
        n = self.glyph.nextInSubpath(c, skipTiny=True)
        if not n or n.isLine() or n.flex:
            return

        da, do = (c.s - n.e).abs().ao()
        # Difference in "bases" too high to report a near miss or length
        # too short (Reuse of MaxFlex in other dimension is coincidental)
        if da > self.FlexCand or do < self.MaxFlex:
            return
        # The end-to-end distance should be at least 3 times the flex depth
        if do < 3 * c.s.avg(n.e).a_dist(c.e):  # XXX may be wrong
            return

        # Start and end points should be on the same side of mid-point

        if self.diffSign(c.e.a - c.s.a, c.e.a - n.e.a):
            return

        d1sq = c.s.distsq(c.e)
        d2sq = c.e.distsq(n.e)
        quot = d2sq / d1sq if d1sq > d2sq else d1sq / d2sq
        if quot < self.FlexLengthRatioCutoff:
            return

        if self.FlexStrict:
            nn = self.glyph.nextInSubpath(n, skipTiny=True)
            # Endpoint of spline after n bends back towards flex midpoint
            if not nn or self.diffSign(nn.e.a - n.e.a, c.e.a - n.e.a):
                return
            p = self.glyph.prevInSubpath(c, skipTiny=True)
            # Endpoint of spline before e bends back towards flex midpoint
            if not p or self.diffSign(p.s.a - c.s.a, c.e.a - c.s.a):
                return

            # flex is not convex
            if self.isV() and (c.s.x > c.e.x) != (c.e.y < c.s.y):
                return
            elif not self.isV() and (c.s.y > n.e.y) != (c.s.x < c.e.x):
                return

        real_n = self.glyph.nextInSubpath(c, skipTiny=False)
        if real_n is not n:
            self.error("Remove short spline at %g %g to add flex." % c.e)
            return
        elif real_n.is_close:
            self.error("Move closepath from %g %.gto add flex." % c.e)
            return

        if fne(c.s.a, n.e.a):
            self.warning("Curves from %g %g to %g %g " % (*c.s, *n.e) +
                         "near miss for adding flex")
            return

        if (feq(n.s.a, n.cs.a) and feq(n.cs.a, n.ce.a) and
                feq(n.ce.a, n.e.a) and not self.linearFlexOK()):
            self.info("Make curves from %g %g to %g %g" % (*c.s, *n.e) +
                      "non-linear to add flex")  # XXXwhat if only one is line?
            return

        c.flex = 1
        n.flex = 2
        self.glyph.flex_count += 1
        self.glyph.changed = True
        if not self.HasFlex:
            self.info("Added flex operators to this glyph." + str(self.isV()))
            self.HasFlex = True

    def calcHintValues(self, lnks, force=True, tryCounter=True):
        if self.glyph.hasHints(doVert=self.isV()):
            if force:
                self.info("Clearing existing %s hints" % self.aDesc())
                self.glyph.clearHints(self.isV())
                self.glyph.changed = True
            else:
                self.keepHints = True
                self.info("Already has %s hints" % self.aDesc())
                return
        self.keepHints = False
        self.startHint()
        self.BigDist = max(self.dominantStems() + [self.InitBigDist])
        self.BigDist *= self.BigDistFactor
        self.debug("generate %s segments" % self.aDesc())
        self.genSegs()
        self.showSegs()
        self.debug("generate %s stem values" % self.aDesc())
        self.Pruning = not (tryCounter and self.isCounterGlyph())
        self.genStemVals()
        self.pruneStemVals()
        self.bestStemVals()
        self.mergeVals()
        for sv in self.hs.stemValues:
            sv.show(self.isV(), "postmerge", self)
        self.debug("pick main %s" % self.aDesc())
        self.glyph.syncPositions()
        lnks.mark(self)
        self.checkVals()
        self.reportStems()
        self.mainVals()
        if tryCounter and self.isCounterGlyph():
            self.Pruning = True
            self.hs.counterHinted = self.tryCounterHinting()
            if not self.hs.counterHinted:
                self.addBBox(True)
                self.hs.counterHinted = self.tryCounterHinting()
                if not self.hs.counterHinted:
                    self.info(("Glyph is in list for using %s counter hints " +
                               "but didn't find any candidates.") %
                              self.aDesc())
                    self.resetForHinting()
                    self.calcHintValues(lnks, force=True, tryCounter=False)
                    return
        if len(self.hs.mainValues) == 0:
            self.addBBox(False)
        self.debug("%s results" % self.aDesc())
        if self.hs.counterHinted:
            self.debug("Using %s counter hints." % self.aDesc())
        for hv in self.hs.mainValues:
            hv.show(self.isV(), "result", self)
#            self.addHintPosition(hv)
        self.hs.pruneHintSegs()

        pt.clearAlign()

    # This method builds up the information needed to mostly get away from
    # looking at values. hs.stems contains the eventual hstems or vstems
    # object that will be copied into the glyphData object. hs.stemConflicts
    # is a map of which stems conflict with which other stems. hs.ghostCompat
    # [i][j] is true if stem i is a ghost and stem j can substitute for it.
    def convertToMasks(self):
        self.startMaskConvert()
        if self.keepHints:
            if self.isV():
                self.hs.stems = self.glyph.vstems
            else:
                self.hs.stems = self.glyph.hstems
            if not self.hs.stems:
                self.hs.stems = []
            l = len(self.hs.stems)
            self.hs.mainMask = [False] * l
            self.hs.hasConflicts = False
            for i in range(l):
                for j in range(i, l):
                    if self.hs.stems[0][i].overlaps(self.hs.stems[0][j]):
                        self.hs.hasConflicts = True
            return
        valuepairmap = {}
        sl = []
        if self.hs.counterHinted:
            svl = self.hs.mainValues
        else:
            svl = self.hs.stemValues
        for sv in svl:
            p = sv.ghosted()
            idx = valuepairmap.get(p, None)
            if idx is not None:
                sv.idx = idx
                continue
            valuepairmap[p] = sv.idx = len(sl)
            sl.append(stem(*p))
        l = len(sl)
        sc = [[False] * l for i in range(l)]
        hasConflicts = False
        for i in range(l):
            for j in range(i, l):
                if i == j:
                    continue
                if sl[i].overlaps(sl[j]):
                    hasConflicts = sc[i][j] = sc[j][i] = True
        assert not self.hs.counterHinted or not hasConflicts
        self.hs.stems = sl
        self.hs.stemConflicts = sc
        self.hs.hasConflicts = hasConflicts

        gc = [None] * l
        for svi in svl:
            if not svi.isGhost:
                continue
            gc[svi.idx] = [False] * l
            if svi.lseg.type == hintSegment.sType.GHOST:
                loc = svi.uloc
            else:
                loc = svi.lloc
            for svj in svl:
                if svi.idx == svj.idx:
                    continue
                if feq(svj.lloc, loc) or feq(svj.uloc, loc):
                    assert sc[svi.idx][svj.idx]
                    gc[svi.idx][svj.idx] = True
        self.hs.ghostCompat = gc

        mm = [False] * l
        for mv in self.hs.mainValues:
            mm[mv.idx] = True
        self.hs.mainMask = mm

        for pe in self.glyph:
            pestate = self.hs.getPEState(pe)
            if not pestate:
                continue
            self.makePEMask(pestate, pe)

    def makePEMask(self, pestate, c):
        l = len(self.hs.stems)
        mask = [False] * l
        for seg in pestate.segments:
            if not seg.hintval:
                continue
            mask[seg.hintval.idx] = True
#        maskstr = ''.join(( '1' if i else '0' for i in mask ))
#        self.info("%s mask %s at %g %g" %
#                  (self.aDesc(), maskstr, c.e.x, c.e.y))
        p0, p1 = (c.s, c.e) if c.isLine() else (c.cs, c.e)
        if self.hs.hasConflicts and True in mask:
            for i in range(l):
                for j in range(i, l):
                    if not mask[i]:
                        break
                    if not mask[j] or i == j:
                        continue
                    if self.hs.stemConflicts[i][j]:
                        remidx = None
                        _, segi = max(((seg.hintval.compVal(self.SpcBonus), sg)
                                      for sg in pestate.segments
                                      if sg.hintval.idx == i))
                        _, segj = max(((sg.hintval.compVal(self.SpcBonus), sg)
                                      for sg in pestate.segments
                                      if sg.hintval.idx == j))
                        vali, valj = segi.hintval, segj.hintval
                        assert vali.lloc <= valj.lloc or valj.isGhost
                        n = self.glyph.nextInSubpath(c)
                        p = self.glyph.prevInSubpath(c)
                        if (vali.val < self.ConflictValMin and
                                self.OKToRem(segi.loc, vali.spc)):
                            remidx = i
                        elif (valj.val < self.ConflictValMin and
                              vali.val > valj.val * self.ConflictValMult and
                              self.OKToRem(segj.loc, valj.spc)):
                            remidx = j
                        elif (c.isLine() or self.flatQuo(p0, p1) > 0 and
                              self.OKToRem(segi.loc, vali.spc)):
                            remidx = i
                        elif self.diffSign(p1.o - p0.o, p.s.o - p0.o):
                            remidx = i
                        elif self.diffSign(n.e.o - p1.o, p0.o - p1.o):
                            remidx = j
                        elif ((feq(p1.o, valj.lloc) or
                               feq(p1.o, valj.uloc)) and
                              fne(p0.o, vali.lloc) and fne(p0.o, vali.uloc)):
                            remidx = i
                        elif ((feq(p0.o, vali.lloc) or
                               feq(p0.o, vali.uloc)) and
                              fne(p1.o, valj.lloc) and fne(p1.o, valj.uloc)):
                            remidx = j
                        # XXX handle ResolveConflictBySplit here
                        if remidx is not None:
                            mask[remidx] = False
                            self.info("Resolved conflicting hints at %g %g" %
                                      (c.e.x, c.e.y))
                        else:
                            mask[i] = mask[j] = False
                            self.info("Could not resolve conflicting hints" +
                                      " at %g %g" % (c.e.x, c.e.y) +
                                      ", removing both")
        if True in mask:
            maskstr = ''.join(('1' if i else '0' for i in mask))
            self.info("%s mask %s at %g %g" %
                      (self.aDesc(), maskstr, c.e.x, c.e.y))
            pestate.mask = mask
        else:
            pestate.mask = None

    def OKToRem(self, loc, spc):
        return (spc == 0 or
                (not self.inBand(loc, False) and not self.inBand(loc, True)))

    # Segments
    def addSegment(self, fr, to, loc, pe1, pe2, typ, desc):
        if self.isV():
            pp = ('v', loc, fr, loc, to, desc)
        else:
            pp = ('h', fr, loc, to, loc, desc)
        self.debug("add %sseg %g %g to %g %g %s" % pp)

        self.hs.addSegment(fr, to, loc, pe1, pe2, typ, self.Bonus,
                           self.isV(), desc)

    def CPFrom(self, cp2, cp3):
#        self.info("CPFrom: cp2: %f, cp3: %f, cpFrom: %f, val: %f" %
#                  (cp2, cp3, 1-self.CPfrac,
#                   (cp3 - cp2) * (1.0-self.CPfrac) + cp2))
        return (cp3 - cp2) * (1.0 - self.CPfrac) + cp2

    def CPTo(self, cp0, cp1):
#        self.info("CPTo: cp0: %f, cp1: %f, cpTo: %f, val: %f" %
#                  (cp0, cp1, self.CPfrac,
#                   (cp1 - cp0) * self.CPfrac + cp0))
        return (cp1 - cp0) * self.CPfrac + cp0

    def adjustDist(self, v, q):
#        self.info("adjdist = %f ( %f %f )" % (v*q, v, q))
        return v * q

    def testTan(self, p):
#        self.info("testTan: %f %f  %f" %
#                  (abs(p.a), abs(p.o), abs(p.o) * self.BendTangent))
        return abs(p.a) > (abs(p.o) * self.BendTangent)

    @staticmethod
    def interpolate(q, v0, q0, v1, q1):
        return v0 + (q - q0) * (v1 - v0) / (q1 - q0)

    def flatQuo(self, p1, p2, doOppo=False):
        # 1 means exactly flat wrt dimension a, 0 means flat
        # in dimension o. Intermediate values represent degrees
        # of flatness
#        self.info("fq pts %f,%f  %f,%f" % (*p1, *p2))
        d = (p1 - p2).abs()
        if doOppo:
            d = pt(d.y, d.x)
        if feq(d.o, 0):
#            log.debug(f"flatquo diff = %f,%f result = 1" % d)
            return 1
        if feq(d.a, 0):
#            log.debug(f"flatquo diff = %f,%f result = 0" % d)
            return 0
        q = (d.o * d.o) / (self.Theta * d.a)
        if q < 0.25:
            result = self.interpolate(q, 1, 0, 0.841, 0.25)
        elif q < .5:
            result = self.interpolate(q, 0.841, 0.25, 0.707, 0.5)
        elif q < 1:
            result = self.interpolate(q, 0.707, 0.5, 0.5, 1)
        elif q < 2:
            result = self.interpolate(q, .5, 1, 0.25, 2)
        elif q < 4:
            result = self.interpolate(q, 0.25, 2, 0, 4)
        else:
            result = 0
#        log.debug(f"flatquo diff = %f,%f q=%f result = %f" % (*d, q, result))
        return result

    def testBend(self, p0, p1, p2):
#        self.info("TB: %f,%f %f,%f %f,%f" % (*p0, *p1, *p2))
        d1 = p1 - p0
        d2 = p2 - p1
        dp = d1.dot(d2)
#        self.info("dp: %f, d1normsq: %f, d2normsq = %f" %
#                  (dp, d1.normsq(), d2.normsq()))
        return dp * dp / (d1.normsq() * d2.normsq()) <= 0.5

    def isCCW(self, p0, p1, p2):
        d0 = p1 - p0  # XXX removed rounding
        d1 = p2 - p1
        return d0.x * d1.y >= d1.x * d0.y  # XXX May be reversed

    @abstractmethod
    def inBand(self, loc, isBottom=False):
        pass

    @abstractmethod
    def hasBands(self):
        pass

    @abstractmethod
    def aDesc(self):
        pass

    @abstractmethod
    def isSpecial(self, lower=False):
        pass

    @abstractmethod
    def checkTfm(self):
        pass

    def relPosition(self, c, lower=False):
        for subp in self.glyph.subpaths:
            if ((lower and subp[0].s.a < c.s.a) or
                    (not lower and subp[0].s.a > c.s.a)):
                return True
        return False

    # I initially combined the two doBends but the result was more confusing
    # and difficult to debug than having them separate
    def doBendsNext(self, c):
        p0, p1, p2 = c.slopePoint(1), c.e, self.glyph.nextSlopePoint(c)
        if feq(p0.o, p1.o):
            return
        osame = self.diffSign(p2.o - p1.o, p1.o - p0.o)
        tbend = self.testBend(p0, p1, p2)
#        self.info("DS: %f %f %f" % (p0.a, p1.a, p2.a))
#        self.info("YS: %r, TT: %r, DS: %r, FQ: %r, TB: %r" %
#                  (osame, self.testTan(p1 - p2),
#                   self.diffSign(p2.a - p1.a, p1.a - p0.a),
#                   self.flatQuo(p0, p1, doOppo=True) > 0, tbend))
        if osame or (self.testTan(p1 - p2) and
                     (self.diffSign(p2.a - p1.a, p1.a - p0.a) or
                      (self.flatQuo(p0, p1, doOppo=True) > 0 and tbend))):
            delta = self.BendLength / 2
            doboth = False
            if p0.a <= p1.a < p2.a or p0.a < p1.a <= p2.a:
                pass
            elif p2.a < p1.a <= p0.a or p2.a <= p1.a < p0.a:
                delta = -delta
            elif osame:
                tst = p0.o > p1.o
                if self.isV():
                    delta = -delta
                if tst == self.isCCW(p0, p1, p2):
                    delta = -delta
            else:
                if self.isV():
                    delta = -delta
                doboth = True
            strt = p1.a - delta
            end = p1.a + delta
            self.addSegment(strt, end, p1.o, c, None,
                            hintSegment.sType.BEND, 'next bend forward')
            if doboth:
                self.addSegment(end, strt, p1.o, c, None,
                                hintSegment.sType.BEND, 'next bend reverse')

    def doBendsPrev(self, c):
        p0, p1, p2 = c.s, c.slopePoint(0), self.glyph.prevSlopePoint(c)
        cs = self.glyph.prevInSubpath(c)
        if feq(p0.o, p1.o):
            return
        osame = self.diffSign(p2.o - p0.o, p0.o - p1.o)
        tbend = self.testBend(p2, p0, p1)
#        self.info("DS: %f %f %f" % (p0.a, p1.a, p2.a))
#        self.info("YS: %r, TT: %r, DS: %r, FQ: %r, TB: %r" %
#                  (osame, self.testTan(p0 - p2),
#                   self.diffSign(p2.a - p0.a, p0.a - p1.a),
#                   self.flatQuo(p1, p0, doOppo=True) > 0, tbend))
        if osame or (self.testTan(p0 - p2) and
                     (self.diffSign(p2.a - p0.a, p0.a - p1.a) or
                      (self.flatQuo(p1, p0, doOppo=True) > 0 and tbend))):
            delta = self.BendLength / 2
            if p2.a <= p0.a < p1.a or p2.a < p0.a <= p1.a:
                pass
            elif p1.a < p0.a <= p2.a or p1.a <= p0.a < p2.a:
                delta = -delta
            elif osame:
                tst = p2.o < p0.o
                if self.isV():
                    delta = -delta
                if tst == self.isCCW(p2, p0, p1):
                    delta = -delta
            else:
                if self.isV():
                    delta = -delta
            strt = p0.a - delta
            end = p0.a + delta
            self.addSegment(strt, end, p0.o, cs, None, hintSegment.sType.BEND,
                            'prev bend forward')

    def nodeIsFlat(self, c, doPrev=False):
        if not c:
            return
        if doPrev:
            d = (self.glyph.prevSlopePoint(c) - c.cs).abs()
        else:
            d = (self.glyph.nextSlopePoint(c) - c.ce).abs()
#        self.info("dmn %g   dmx %g" % (d.o, d.a))
        return d.o <= self.OppoFlatMin and d.a >= self.FlatMax

    def sameDir(self, c, doPrev=False):
        if not c:
            return False
        if doPrev:
            p = self.glyph.prevInSubpath(c, skipTiny=True)
            p0, p1, p2 = c.e, c.s, p.s
        else:
            n = self.glyph.nextInSubpath(c, skipTiny=True)
            p0, p1, p2 = c.s, c.e, n.e
#        self.info("p0: %f,%f  p1: %f,%f  p2: %f,%f" % (*p0, *p1, *p2))
        if (self.diffSign(p0.y - p1.y, p1.y - p2.y) or
                self.diffSign(p0.x - p1.x, p1.x - p2.x)):
            return False
        return not self.testBend(p0, p1, p2)

    def extremaSegment(self, pe, extp, extt, isMn):
        a, b, c, d = pe.cubicParameters()
        loc = round(extp.o) + (-self.ExtremaDist if isMn else self.ExtremaDist)

        horiz = not self.isV()  # When finding vertical stems solve for x
        sl = solveCubic(a[horiz], b[horiz], c[horiz], d[horiz] - loc)
        pl = [(pt(a[0] * t * t * t + b[0] * t * t + c[0] * t + d[0],
                  a[1] * t * t * t + b[1] * t * t + c[1] * t + d[1]), t)
              for t in sl if 0 <= t <= 1]

        # Find closest solution on each side of extp
        mn_p = mx_p = None
        mn_td = mx_td = 2
        for p, t in pl:
            td = abs(t - extt)
            if t < extt and td < mn_td:
                mn_p, mn_td = p, td
            elif t > extt and td < mx_td:
                mx_p, mx_td = p, td

        # If a side isn't found the spline end on that side should be
        # within the ExtremaDist of extp.o
        if not mn_p:
            mn_p = pe.s
            assert abs(mn_p.o - extp.o) < self.ExtremaDist + 0.01
        if not mx_p:
            mx_p = pe.e
            assert abs(mx_p.o - extp.o) < self.ExtremaDist + 0.01

        return mn_p.a, mx_p.a

    def pickSpot(self, p0, p1, dist, pp0, pp1, prv, nxt):
#        self.info("%g,%g  %g,%g  %g  %g,%g  %g,%g  %g,%g  %g,%g" %
#                  (*p0, *p1, dist, *pp0, *pp1, *prv, *nxt))
        inBand0 = self.inBand(p0.o, dist >= 0)
        inBand1 = self.inBand(p1.o, dist >= 0)
        if inBand0 and not inBand1:
#            self.info("here1")
            return p0.o
        if inBand1 and not inBand0:
#            self.info("here2")
            return p1.o
        if feq(p0.o, pp0.o) and fne(p1.o, pp1.o):
#            self.info("here3")
            return p0.o
        if fne(p0.o, pp0.o) and feq(p1.o, pp1.o):
#            self.info("here4")
            return p1.o
        if feq(p0.o, prv.o) and fne(p1.o, nxt.o):
#            self.info("here5")
            return p0.o
        if fne(p0.o, prv.o) and feq(p1.o, nxt.o):
#            self.info("here6")
            return p1.o
        if inBand0 and inBand1:
            upper, lower = (p1.o, p0.o) if p0.o < p1.o else (p0.o, p1.o)
#            self.info("here12")
            return upper if dist < 0 else lower
        if abs(pp0.a - p0.a) > abs(pp1.a - p1.a):
#            self.info("here7")
            return p0.o
        if abs(pp1.a - p1.a) > abs(pp0.a - p0.a):
#            self.info("here8")
            return p1.o
        if feq(p0.o, prv.o) and feq(p1.o, nxt.o):
            if abs(p0.a - prv.a) > abs(p1.a - nxt.a):
#                self.info("here9")
                return p0.o
#            self.info("here10 %g %g  %g %g" % (*p0, *p1))
            return p1.o
#        self.info("here11")
        return (p0.o + p1.o) / 2

    def genSegs(self):
        self.Bonus = 0
        for c in self.glyph:
            prv = self.glyph.prevInSubpath(c)
            self.info("Element %d %d   x %g y %g" %
                      (c.position[0], c.position[1] + 1, c.e.x, c.e.y))
            if c.isStart():
                self.Bonus = 0
                if (self.isSpecial(lower=False) and
                        self.relPosition(c, lower=False)):
                    self.Bonus = self.SpecialCharBonus
                elif (self.isSpecial(lower=True) and
                      self.relPosition(c, lower=True)):
                    self.Bonus = self.SpecialCharBonus
            if c.isLine() and not c.isTiny():
                q = self.flatQuo(c.s, c.e)
                if q > 0:
                    if feq(c.s.o, c.e.o):
                        self.addSegment(c.s.a, c.e.a, c.s.o, prv, c,
                                        hintSegment.sType.LINE, "flat line")
                    else:
                        if q < .25:
                            q = .25
                        adist = self.adjustDist(c.e.a - c.s.a, q) / 2
                        aavg = (c.s.a + c.e.a) / 2
                        # XXX if we're picking the spot why aren't we
                        # picking the pair of path elements to pass
                        sp = self.pickSpot(c.s, c.e, adist, c.s, c.e,
                                           self.glyph.prevSlopePoint(c),
                                           self.glyph.nextSlopePoint(c))
#                        self.info("sp is %g  %g  %g" % (sp, aavg, adist))
                        self.addSegment(aavg - adist, aavg + adist, sp, prv, c,
                                        hintSegment.sType.LINE, "line")
                        d = (c.s - c.e).abs()
                        if d.o <= 2 and (d.a > 10 or d.normsq() > 100):
                            self.info("The line from %g %g to %g %g" %
                                      (*c.s, *c.e) +
                                      " is not exactly " + self.aDesc())
                else:
                    self.doBendsNext(c)
                    self.doBendsPrev(c)
            elif not c.isLine():
#                self.info("flex is " + str(c.flex))
                if c.flex == 1:
                    fl1 = c
                    fl1prv = prv
                elif c.flex == 2:
                    if self.flatQuo(fl1.s, c.e) > 0:
                        self.addSegment(fl1.s.a, c.e.a, c.e.o, fl1prv, c,
                                        hintSegment.sType.LINE, "flex")
                if c.flex != 2:
                    q = self.flatQuo(c.cs, c.s)
                    if q == 0:
                        self.doBendsPrev(c)
                    else:
#                       self.info("eq: %r, ne: %r, nIF: %r, sD: %r, blah: %r" %
#                                 (feq(c.cs.o, c.s.o), fne(c.ce.o, c.e.o),
#                                  self.nodeIsFlat(i, doPrev=True),
#                                  self.sameDir(i, doPrev=True),
#                                  feq(c.cs.o, c.s.o) or
#                                  (fne(c.ce.o, c.e.o) and
#                                   (self.nodeIsFlat(i, doPrev=True) or
#                                    notself.sameDir(i, doPrev=True)))))
                        if (feq(c.cs.o, c.s.o) or
                            (fne(c.ce.o, c.e.o) and
                             (self.nodeIsFlat(c, doPrev=True) or
                              not self.sameDir(c, doPrev=True)))):
                            q2 = self.flatQuo(c.ce, c.s)
                            if (q2 > 0 and
                                self.sameSign(c.cs.a - c.s.a, c.ce.a - c.s.a) and
                                abs(c.ce.a - c.s.a) > abs(c.cs.a - c.s.a)):
                                adist = self.adjustDist(self.CPTo(c.cs.a, c.ce.a) - c.s.a, q2)
                                end = self.adjustDist(self.CPTo(c.s.a, c.cs.a) - c.s.a, q)
                                if abs(end) > abs(adist):
                                    adist = end
                                self.addSegment(c.s.a, c.s.a + adist, c.s.o, prv, c,
                                        hintSegment.sType.CURVE, "not flex2 if")
                            else:
                                # XXX bugfix in V
                                adist = self.adjustDist(self.CPTo(c.s.a, c.cs.a) - c.s.a, q)
                                self.addSegment(c.s.a, c.s.a + adist, c.s.o, prv, c,
                                        hintSegment.sType.CURVE, "not flex2 else")
                if c.flex != 1:
                    q = self.flatQuo(c.ce, c.e)
#                    self.info("q=%f" % q)
#                    self.info("t1 %r  (t2 %r  t3 %r  t4 %r)" %
#                              (feq(c.ce.o, c.e.o), fne(c.cs.o, c.s.o),
#                               self.nodeIsFlat(i, doPrev=False),
#                               self.sameDir(i, doPrev=False)))
                    if q == 0:
                        self.doBendsNext(c)
                    elif (feq(c.ce.o, c.e.o) or
                          (fne(c.cs.o, c.s.o) and
                           (self.nodeIsFlat(c, doPrev=False) or
                            not self.sameDir(c, doPrev=False)))):
                        adist = self.adjustDist(c.e.a - self.CPFrom(c.ce.a, c.e.a), q)
                        q2 = self.flatQuo(c.s, c.e)
                        if q2 > 0:
                            ad2 = self.adjustDist(c.e.a - c.s.a, q2)
                        else:
                            ad2 = 0
#                        self.info("q=%f, q2=%f, adist=%f, ad2=%f, CPF=%f" %
#                                  (q, q2, adist, ad2,
#                                   self.CPFrom(c.ce.a, c.e.a)))
                        if q2 > 0 and abs(ad2) > abs(adist):
                            if (feq(c.s.o, c.cs.o) and feq(c.cs.o, c.ce.o) and
                                    feq(c.ce.o, c.e.o)):
                                pass  # XXX reportLinearCurve
                            adist = ad2 / 2
                            aavg = (c.s.a + c.e.a) / 2
                            sp = self.pickSpot(c.s, c.e, adist, c.cs, c.ce,
                                               self.glyph.prevSlopePoint(c),
                                               self.glyph.nextSlopePoint(c))
                            self.addSegment(aavg - adist, aavg + adist, sp, c,
                                            None, hintSegment.sType.CURVE,
                                            "not flex1 if")
                        else:
                            q2 = self.flatQuo(c.cs, c.e)
                            if (q2 > 0 and
                                self.sameSign(c.cs.a - c.e.a, c.ce.a - c.e.a) and
                                abs(c.ce.a - c.e.a) < abs(c.cs.a - c.e.a)):
                                aend = self.adjustDist(c.e.a - self.CPFrom(c.cs.a, c.ce.a), q2)
                                if abs(aend) > abs(adist):
                                    adist = aend
#                                self.info("aend = %f, adist = %f, q2 = %f" % (aend, adist, q2))
                                self.addSegment(c.e.a - adist, c.e.a, c.e.o, c,
                                                None, hintSegment.sType.CURVE,
                                                "not flex1 else 1")
                            else:
                                self.addSegment(c.e.a - adist, c.e.a, c.e.o, c,
                                                None, hintSegment.sType.CURVE,
                                                "not flex1 else 2")
                if c.flex is None:
                    d, extp, extt, isMin = c.getBounds().farthestExtreme(not self.isV())
                    if d > 2:
                        frst, lst = self.extremaSegment(c, extp, extt, isMin)
                        aavg = (frst + lst) / 2
                        if feq(frst, lst):
                            adist = (c.e.a - c.s.a) / 10
                        else:
                            adist = (lst - frst) / 2
                        if abs(adist) < self.BendLength:
                            adist = math.copysign(adist, self.BendLength)
                        self.addSegment(aavg - adist, aavg + adist,
                                        round(extp.o + 0.5), c, None,
                                        hintSegment.sType.CURVE,
                                        "curve segment")

        self.hs.compactLists()
        self.hs.remExtraBends(self)
        self.hs.cleanup()
        self.checkTfm()

    def showSegs(self):
        self.debug("Generated segments")
        for pe in self.glyph:
            self.debug("for path element x %g y %g" % (pe.e.x, pe.e.y))
            pestate = self.hs.getPEState(pe)
            seglist = pestate.segments if pestate else []
            if seglist:
                for seg in seglist:
                    seg.show("generated", self)
            else:
                self.debug("None")

        # Stems
    def genStemVals(self):
        ll, ul = self.segmentLists()
#        for ls in ll:
#            self.info("l loc: %g, min: %g, max: %g" %
#                      (ls.loc, ls.min, ls.max))
#        for ls in ul:
#            self.info("u loc: %g, min: %g, max: %g" %
#                      (ls.loc, ls.min, ls.max))
        for ls in ll:
            for us in ul:
#                self.info("us.min %g  ls.max %g  us.max %g  ls.min %g" %
#                          (us.min, ls.max, us.max, ls.min))
                if ls.loc > us.loc:
#                    self.info("continue")
                    continue
                val, spc = self.evalPair(ls, us)
#                self.info("bloc: %g, uloc: %g, spc: %g, val: %g" %
#                          (ls.loc, us.loc, spc, val))
                self.stemMiss(ls, us)
                self.addStemValue(ls.loc, us.loc, val, spc, ls, us)

        if self.hasBands():
            ghostSeg = hintSegment(0, 0, 0, None, hintSegment.sType.GHOST,
                                   0, self.isV(), "ghost")
            for s in ll:
                if self.inBand(s.loc, isBottom=True):
                    ghostSeg.loc = s.loc + self.GhostWidth
                    cntr = (s.max + s.min) / 2
                    ghostSeg.max = round(cntr + self.GhostLength / 2)
                    ghostSeg.min = round(cntr - self.GhostLength / 2)
                    self.addStemValue(s.loc, ghostSeg.loc, self.GhostVal,
                                      self.GhostSpecial, s, ghostSeg)
            for s in ul:
                if self.inBand(s.loc, isBottom=False):
                    ghostSeg.loc = s.loc - self.GhostWidth
                    cntr = (s.max + s.min) / 2
                    ghostSeg.max = round(cntr + self.GhostLength / 2)
                    ghostSeg.min = round(cntr - self.GhostLength / 2)
                    self.addStemValue(ghostSeg.loc, s.loc, self.GhostVal,
                                      self.GhostSpecial, ghostSeg, s)

#        for sv in self.hs.stemValues:
#            sv.show(self.isV(), "before", self)
        self.combineStemValues()
#        for sv in self.hs.stemValues:
#            sv.show(self.isV(), "after", self)

    def evalPair(self, ls, us):
        spc = 0
        loc_d = abs(us.loc - ls.loc)
        if loc_d < self.MinDist:
            return 0, spc
        inBBand = self.inBand(ls.loc, isBottom=True)
        inTBand = self.inBand(us.loc, isBottom=False)
#        self.info("bloc %g, tloc %g, inBBand %r, inTBand %r" %
#                  (ls.loc, us.loc, inBBand, inTBand))
        if inBBand and inTBand:
            return 0, spc
        if inBBand or inTBand:
            spc += 2
#        self.info("us.min %g  ls.max %g  us.max %g  ls.min %g" %
#                  (us.min, ls.max, us.max, ls.min))
        if us.min <= ls.max and us.max >= ls.min:  # overlap
            overlen = min(us.max, ls.max) - max(us.min, ls.min)
            minlen = min(us.max - us.min, ls.max - ls.min)
#            self.info("loc_d: %g  overlen: %g  minlen: %g" %
#                      (loc_d, overlen, minlen))
            if feq(overlen, minlen):
                dist = loc_d
            else:
                dist = loc_d * (1 + .4 * (1 - overlen / minlen))
        else:
            o_d = min(abs(us.min - ls.max), abs(us.max - ls.min))
            dist = round(loc_d * self.NoOverlapPenalty + o_d * o_d / 40)
#            self.info("one: %g, two: %g, dist: %g, o_d: %g, loc_d: %g" %
#                      (loc_d * self.NoOverlapPenalty, o_d * o_d / 40,
#                       dist, o_d, loc_d))
            if o_d > loc_d:  # XXX this didn't work before
                dist *= o_d / loc_d
#                self.info("dist: %g" % dist)
        dist = max(dist, 2 * self.MinDist)
        if min(ls.bonus, us.bonus) > 0:
            spc += 2
        for dsw in self.dominantStems():
            if dsw == abs(loc_d):
                spc += 1
                break
#        self.info("l1 %g, l2 %g, dist %g, d %g" %
#                  (ls.max - ls.min, us.max - us.min, dist, loc_d))
        dist = max(dist, 2)
        bl = max(ls.max - ls.min, 2)
        ul = max(us.max - us.min, 2)
        rl = bl * bl
        ru = ul * ul
        q = dist * dist
        v = 1000 * rl * ru / (q * q)
#        self.info("rl %g, ru %g, q %g, v %g" %
#                  (rl * 256**2, ru * 256**2, q * 256**2, v))
        if loc_d > self.BigDist:
            fac = self.BigDist / loc_d
#            self.info("fac : %g" % fac)
            if fac > .5:
#                self.info("fac**8 %g" % fac**8)
                v *= fac**8
            else:
                return 0, spc
#        self.info("v %g" % v)
        v = max(v, self.MinValue)
        v = min(v, self.MaxValue)
        return v, spc

    def stemMiss(self, ls, us):
        loc_d = abs(us.loc - ls.loc)
        if loc_d < self.MinDist:
            return 0
        if us.min > ls.max or us.max < ls.min:  # no overlap
            return  # XXX removed overlap dist adjustment
        d, nearStem = min(((abs(s - loc_d), s) for s in self.dominantStems()))
        if d == 0 or d > 2:
            return
        curved = (ls.type == hintSegment.sType.CURVE or
                  us.type == hintSegment.sType.CURVE)
        self.info("%s %s stem near miss: %g instead of %g at %g to %g." %
                  (self.aDesc(), "curve" if curved else "linear", loc_d,
                   nearStem, ls.loc, us.loc))

    def addStemValue(self, lloc, uloc, val, spc, lseg, useg):
#        self.info("lloc %g, uloc %g, val %g, spc %g" %
#                  (lloc, uloc, val, spc))
        if val == 0:
#            self.info("here1")
            return
        if (not self.Pruning or val < self.PruneValue) and spc <= 0:
#            self.info("here2")
            return
        if (lseg.type == hintSegment.sType.BEND and
                useg.type == hintSegment.sType.BEND):
#            self.info("here3")
            return
        ghst = (lseg.type == hintSegment.sType.GHOST or
                useg.type == hintSegment.sType.GHOST)
        if not ghst and (not self.Pruning or val <= self.PruneD) and spc <= 0:
            if (lseg.type == hintSegment.sType.BEND or
                    useg.type == hintSegment.sType.BEND):
#                self.info("here4")
                return
            lpesub = lseg.pe().position[0]
            upesub = useg.pe().position[0]
#            self.info("before5")
            if lpesub != upesub:
                lsb = self.glyph.getBounds(lpesub)
                usb = self.glyph.getBounds(upesub)
                if not lsb.within(usb) and not usb.within(lsb):
#                    self.info("here5")
                    return
        if not useg:
#            self.info("here6")
            return

        sv = stemValue(lloc, uloc, val, spc, lseg, useg, ghst)
        self.insertStemValue(sv)

    def insertStemValue(self, sv, note="add"):
        svl = self.hs.stemValues
        i = bisect.bisect_left(svl, sv)
        if sv.isGhost:
            j = i
            while j < len(svl) and svl[j] == sv:
                if (not svl[j].isGhost and
                        (svl[j].lseg == sv.lseg or svl[j].useg == sv.useg) and
                        svl[j].val > sv.val):
                    # Don't add
                    return
                j += 1
        svl.insert(i, sv)
        sv.show(self.isV(), note, self)

    def combineStemValues(self):
        svl = self.hs.stemValues
        l = len(svl)
        i = 0
        while i < l:
            val = svl[i].val
            j = i + 1
            while j < l and svl[i] == svl[j]:
                if svl[j].isGhost:
                    val = svl[j].val
                else:
                    val += svl[j].val + 2 * math.sqrt(val * svl[j].val)
                j += 1
            for k in range(i, j):
                svl[k].val = val
            i = j

    # merge
    def pruneStemVals(self):
        for c in self.hs.stemValues if self.isV() else reversed(self.hs.stemValues):
            otherLow = otherHigh = False
            uInBand = self.inBand(c.uloc, isBottom=False)
            lInBand = self.inBand(c.lloc, isBottom=True)
            for d in self.hs.stemValues if self.isV() else reversed(self.hs.stemValues):
#                c.show(self.isV(), "prune c", self)
#                d.show(self.isV(), "prune d", self)
#                self.info("otherLow %r, otherHigh %r" % (otherLow, otherHigh))
                if d.pruned:  # XXX and (gDoAligns || !gDoStems)
                    continue
#                self.info("vm %r" % (not d.val > c.val * self.VeryMuchPF))
                if (not c.isGhost and d.isGhost and
                        not d.val > c.val * self.VeryMuchPF):
                    continue
                if feq(c.lloc, d.lloc) and feq(c.uloc, d.uloc):
                    continue
                if self.isV() and d.val <= c.val * self.PruneFactor:
                    continue
                csl = self.closeSegs(c.lseg, d.lseg)
                csu = self.closeSegs(c.useg, d.useg)
#                self.info("csl: %r, csu: %r" % (csl, csu))
                if c.val < 100 and d.val > c.val * self.MuchPF:
                    cs_tst = csl or csu
                else:
                    cs_tst = csl and csu
#                self.info("c.lloc: %g, d.lloc: %g" % (c.lloc, d.lloc))
#                self.info("t1: %r, t2: %r, t3: %r, t4: %r" %
#                          (c.lloc - self.PruneDistance <= d.lloc,
#                           c.uloc + self.PruneDistance >= d.uloc,
#                           cs_tst, self.isV() or c.val < 16 or
#                           ((not uInBand or feq(c.uloc, d.uloc)) and
#                           (not lInBand or feq(c.lloc, d.lloc)))))
                if (d.val > c.val * self.PruneFactor and
                    c.lloc - self.PruneDistance <= d.lloc and
                    c.uloc + self.PruneDistance >= d.uloc and cs_tst and
                    (self.isV() or c.val < 16 or
                     ((not uInBand or feq(c.uloc, d.uloc)) and
                      (not lInBand or feq(c.lloc, d.lloc))))):
                    self.prune(c, d, "close and higher value")
                    break
                if c.lseg is not None and c.useg is not None:
#                    self.info("d.val %g c.val %g ldiff %g udiff %g ddiff %g cdiff %g csl %r csu %r lib %r uib %r" % (d.val, c.val, abs(c.lloc - d.lloc), abs(c.uloc - d.uloc), abs(d.uloc - d.lloc), abs(c.uloc - c.lloc), csl, csu, lInBand, uInBand))
                    if abs(c.lloc - d.lloc) < 1:
                        if (not otherLow and
                                c.val < d.val * self.PruneFactorLt and
                                abs(d.uloc - d.lloc) < abs(c.uloc - c.lloc) and
                                csl):
                            otherLow = True
                        if ((self.isV() or
                                (d.val > c.val * self.PruneFactor and
                                 not uInBand)) and
                                c.useg.type == hintSegment.sType.BEND and csl):
                            self.prune(c, d, "lower bend")
                            break
                    if abs(c.uloc - d.uloc) < 1:
                        if (not otherHigh and
                                c.val < d.val * self.PruneFactorLt and
                                abs(d.uloc - d.lloc) < abs(c.uloc - c.lloc) and
                                csu):
                            otherHigh = True
                        if ((self.isV() or
                                (d.val > c.val * self.PruneFactor and
                                 not lInBand)) and
                                c.lseg.type == hintSegment.sType.BEND and csu):
                            self.prune(c, d, "upper bend")
                            break
                    if otherLow and otherHigh:
                        self.prune(c, d, "low and high")
                        break
        self.hs.stemValues = [sv for sv in self.hs.stemValues if not sv.pruned]

    def closeSegs(self, s1, s2):
        if not s1 or not s2:
#            self.info("here1")
            return False
        if s1 is s2 or not s1.pe or not s2.pe or s1.pe is s2.pe:
#            self.info("here2")
            return True
        if s1.loc < s2.loc:
            loca, locb = s1.loc, s2.loc
        else:
            locb, loca = s1.loc, s2.loc
        if (locb - loca) > 5 * self.CloseMerge:
#            self.info("here3")
            return False
#        self.info("loca: %g, locb: %g" % (loca, locb))
        loca -= self.CloseMerge
        locb += self.CloseMerge
        n = s1.pe()
        p = self.glyph.prevInSubpath(n)
        pe2 = s2.pe()
        ngood = pgood = True
        for cnt in range(len(self.glyph.subpaths[n.position[0]])):
            if not ngood and not pgood:
#                self.info("here4")
                return False
            assert n and p
            if (ngood and n is pe2) or (pgood and p is pe2):
#                self.info("here5")
                return True
            if n.e.o > locb or n.e.o < loca:
#                self.info("n not good %r %r %r" % (ncurrent.e.o, locb, loca))
                ngood = False
            if p.e.o > locb or p.e.o < loca:
#                self.info("p not good %r %r %r" % (pcurrent.e.o, locb, loca))
                pgood = False
            n = self.glyph.nextInSubpath(n)
            p = self.glyph.prevInSubpath(p)
#            self.info("tick")
#        self.info("here6")
        return False

    def prune(self, sv, other_sv, desc):
        self.debug("Prune %s val: %s" % (self.aDesc(), desc))
        sv.show(self.isV(), "pruned", self)
        other_sv.show(self.isV(), "pruner", self)
        sv.pruned = True

    def closePathElems(self, pe1, pe2, loc1, loc2):
        if pe1 is pe2:
            return True
        if loc1 > loc2:
            loc1, loc2 = loc2, loc1
        if (loc2 - loc1) > 5 * self.CloseMerge:
            return False
        loc1 -= self.CloseMerge
        loc2 += self.closeMerge

    def findBestValues(self):
        svl = self.hs.stemValues
        svll = len(svl)
        for i in range(svll):
            if svl[i].best is not None:
                continue
            blst = [svl[i]]
            b = i
            for j in range(i + 1, svll):
                if (svl[j].best is not None or fne(svl[j].lloc, svl[b].lloc) or
                        fne(svl[j].uloc, svl[b].lloc)):
                    continue
                blst.append(svl[j])
                if ((svl[j].spc == svl[b].spc and svl[j].val > svl[b].val) or
                        svl[j].spc > svl[b].spc):
                    b = j
            for sv in blst:
                sv.best = svl[b]

    def replaceVals(self, oldl, oldu, newl, newu, newbest):
        for sv in self.hs.stemValues:
            if fne(sv.lloc, oldl) or fne(sv.uloc, oldu) or sv.merge:
                continue
            self.debug("Replace %s hints pair at %g %g by %g %g" %
                       (self.aDesc(), oldl, oldu, newl, newu))
            self.debug("\told value %g %g new value %g %g" %
                       (sv.val, sv.spc, newbest.val, newbest.spc))
            sv.lloc = newl
            sv.uloc = newu
            sv.val = newbest.val
            sv.spc = newbest.spc
            sv.best = newbest
            sv.merge = True

    def mergeVals(self):
        self.findBestValues()

        if not self.options.report_zones:
            return
        svl = self.hs.stemValues
        for sv in svl:
            sv.merge = False
        while True:
            try:
                _, bst = max(((sv.best.compVal(self.SFactor), sv) for sv in svl
                              if not sv.merge))
            except ValueError:
                break
            bst.merge = True
            for sv in svl:
                replace = False
                if sv.merge or bst.isGhost != sv.isGhost:
                    continue
                if feq(bst.lloc, sv.lloc) and feq(bst.uloc, sv.uloc):
                    continue
                svuIB = self.inBand(sv.uloc, isBottom=False)
                svlIB = self.inBand(sv.lloc, isBottom=True)
                btuIB = self.inBand(bst.uloc, isBottom=False)
                btlIB = self.inBand(bst.lloc, isBottom=True)
#                self.info("t1 %r  t2 %r  t3 %r" %
#                          (feq(bst.lloc, sv.lloc) and
#                           self.closeSegs(bst.lseg, sv.lseg) and
#                           not btlIB and not svuIB and not btuIB,
#                           feq(bst.uloc, sv.uloc) and
#                           self.closeSegs(bst.useg, sv.useg) and
#                           not btuIB and not svlIB and not btlIB,
#                           abs(bst.lloc - sv.lloc) <= self.MaxMerge
#                           and abs(bst.uloc - sv.uloc) <= self.MaxMerge and
#                           (self.isV() or feq(bst.lloc, sv.lloc) or
#                            not svlIB) and
#                           (self.isV() or feq(bst.uloc, sv.uloc) or
#                            not svuIB)))
#                self.info("%g %g %r %r %r %r" %
#                          (abs(bst.lloc - sv.lloc), abs(bst.uloc - sv.uloc),
#                           feq(bst.lloc, sv.lloc),
#                           not svlIB, feq(bst.uloc, sv.uloc),
#                           not svuIB))
                if ((feq(bst.lloc, sv.lloc) and
                     self.closeSegs(bst.lseg, sv.lseg) and
                     not btlIB and not svuIB and not btuIB) or
                    (feq(bst.uloc, sv.uloc) and
                     self.closeSegs(bst.useg, sv.useg) and
                     not btuIB and not svlIB and not btlIB) or
                    (abs(bst.lloc - sv.lloc) <= self.MaxMerge and
                     abs(bst.uloc - sv.uloc) <= self.MaxMerge and
                     (self.isV() or feq(bst.lloc, sv.lloc) or not svlIB) and
                     (self.isV() or feq(bst.uloc, sv.uloc) or not svuIB))):
                    if (feq(bst.best.spc, sv.spc) and
                            feq(bst.best.val, sv.val) and
                            not self.isV()):
                        if svlIB:
                            if bst.lloc > sv.lloc:
#                                self.info("here1")
                                replace = True
                        elif svuIB:
                            if bst.uloc < sv.uloc:
#                                self.info("here2")
                                replace = True
                    else:
#                        self.info("here5")
                        replace = True
                elif (feq(bst.best.spc, sv.spc) and bst.lseg is not None and
                      bst.useg is not None):
                    if sv.lseg is not None and sv.useg is not None:
                        if (abs(bst.lloc - sv.lloc) <= 1 and
                                abs(bst.uloc - sv.uloc) <= self.MaxBendMerge):
                            if (sv.useg.type == hintSegment.sType.BEND and
                                    not svuIB):
#                                self.info("here3")
                                replace = True
                        elif (abs(bst.uloc - sv.uloc) <= 1 and
                              abs(bst.tloc - sv.lloc) <= self.MaxBendMerge):
                            if (sv.lseg.type == hintSegment.sType.BEND and
                                    not svlIB):
#                                self.info("here4")
                                replace = True
                if replace:
                    self.replaceVals(sv.lloc, sv.uloc, bst.lloc, bst.uloc,
                                     bst.best)

    def bestStemVals(self):
        for sv in self.hs.stemValues:
            sv.pruned = True
        ll, ul = self.segmentLists()
        self.findBestValForSegs(ul, True)
        self.findBestValForSegs(ll, False)
        self.hs.stemValues = [sv for sv in self.hs.stemValues if not sv.pruned]

    def findBestValForSegs(self, segl, isU):
        for seg in segl:
            ghst = None
            best = self.findBestVal(seg, isU, False)
            if best is not None and best.isGhost:
                nonGhst = self.findBestVal(seg, isU, True)
                if nonGhst is not None and nonGhst.val >= 2:
                    ghst = best
                    best = nonGhst
            if best is not None:
                if not (best.val < 1 / 16 and
                        (ghst is None or ghst.val < 1 / 16)):
                    best.pruned = False
                    seg.hintval = best

    def findBestVal(self, seg, isU, locFlag):
        best = None
        svl = self.hs.stemValues

        def OKcond(sv):
            vs, vl = (sv.useg, sv.uloc) if isU else (sv.lseg, sv.lloc)
            if locFlag:
                loctest = not sv.isGhost
            else:
                loctest = vs is seg or self.closeSegs(vs, seg)
#            self.info("d %g  loctest %r  cVFS %r" %
#                      (abs(vl - seg.loc), loctest,
#                       self.considerValForSeg(sv, seg, isU)))
            return (abs(vl - seg.loc) <= self.MaxMerge and loctest and
                    self.considerValForSeg(sv, seg, isU))

        try:
            _, best = max(((sv.compVal(self.SpcBonus, self.GhostFactor), sv)
                           for sv in svl if OKcond(sv)))
        except ValueError:
            pass
        self.debug("findBestVal: loc %g min %g max %g" %
                   (seg.loc, seg.min, seg.max))
        if best:
            best.show(self.isV(), "best", self)
        else:
            self.debug("NULL")
        return best

    def considerValForSeg(self, sv, seg, isU):
        if sv.spc > 0 or self.inBand(seg.loc, not isU):
            return True
        return not (not self.Pruning or sv.val < self.PruneB)

    def checkVals(self):
        lPrev = uPrev = -1e20
        for sv in self.hs.stemValues:
            l, u = sv.lloc, sv.uloc
            w = abs(u - l)
            minDiff = 0
            try:
                minDiff, mdw = min(((abs(w - dw), dw)
                                    for dw in self.dominantStems()))
            except ValueError:
                pass
            if feq(minDiff, 0) or minDiff > 2:
                continue
            if fne(l, lPrev) or fne(u, uPrev):
                line = self.findLineSeg(l, True) and self.findLineSeg(u, False)
                if not sv.isGhost:
                    self.info("%s %s stem near miss: " %
                              (self.aDesc(), "linear" if line else "curve") +
                              "%g instead of %g at %g to %g" % (w, mdw, l, u))

    def findLineSeg(self, loc, isBottom=False):
        for s in self.segmentLists()[0 if isBottom else 1]:
            if feq(s.loc, loc) and s.type == hintSegment.sType.LINE:
                return True
        return False

    def reportStems(self):
        glyphTop = -1e40
        glyphBot = 1e40
        isV = self.isV()
        for sv in self.hs.stemValues:
            l, u = sv.lloc, sv.uloc
            glyphBot = min(l, glyphBot)
            glyphTop = max(u, glyphTop)
            if not sv.isGhost:
                line = self.findLineSeg(l, True) and self.findLineSeg(u, False)
                self.report.stem(l, u, line, isV)
                if not isV:
                    self.report.stemZone(l, u)  # XXX ?

        if not isV and glyphTop > glyphBot:
            self.report.charZone(glyphBot, glyphTop)

    def mainVals(self):
        mainValues = []
        rejectValues = []
        svl = copy(self.hs.stemValues)
        prevBV = 0
        while True:
            try:
                _, best = max(((sv.compVal(self.SpcBonus), sv) for sv in svl
                               if self.mainOK(sv.spc, sv.val, mainValues,
                                              prevBV)))
            except ValueError:
                break
#            best.show(self.isV(), "inbest", self)
            lseg, useg = best.lseg, best.useg
            if best.isGhost:
                for sv in svl:
                    if (feq(best.lloc, sv.lloc) and feq(best.uloc, sv.uloc) and
                            not sv.isGhost):
                        lseg, useg = sv.lseg, sv.useg
                        break
                if lseg.type == hintSegment.sType.GHOST:
                    newbest = useg.hintval
                    if (newbest is not None and newbest is not best and
                            newbest in svl):
                        best = newbest
                elif useg.type == hintSegment.sType.GHOST:
                    newbest = lseg.hintval
                    if (newbest is not None and newbest is not best and
                            newbest in svl):
                        best = newbest
            svl.remove(best)
            prevBV = best.val
            bisect.insort(mainValues, best)
            llocb, ulocb = best.lloc, best.uloc
            if best.isGhost:
                if best.lseg.type == hintSegment.sType.GHOST:
                    llocb = ulocb
                else:
                    ulocb = llocb
            llocb -= self.BandMargin
            ulocb += self.BandMargin
            i = 0
            while i < len(svl):
                llocsv, ulocsv = svl[i].lloc, svl[i].uloc
                if svl[i].isGhost:
                    if svl[i].lseg.type == hintSegment.sType.GHOST:
                        llocsv = ulocsv
                    else:
                        ulocsv = llocsv
#                self.info("blloc %g, buloc %g, svlloc %g, svuloc %g" %
#                          (llocb, ulocb, llocsv, ulocsv))
                if llocsv <= ulocb and ulocsv >= llocb:
#                    svl[i].show(self.isV(), "rejecting", self)
                    rejectValues.append(svl[i])
                    del svl[i]
                else:
                    i += 1
        rejectValues.extend(svl)
        # if len(mainValues) == 0:  # XXX prob redundant with AddBBoxHV
        #     self.hintBnds()
        self.hs.mainValues = mainValues
        self.hs.rejectValues = rejectValues

    def mainOK(self, spc, val, hasHints, prevBV):
        if spc > 0:
            return True
        if not hasHints:
            return not self.Pruning or val >= self.PruneD
        if not self.Pruning or val > self.PruneA:
            return True
        if not self.Pruning or val < self.PruneB:
            return False
        return not self.Pruning or prevBV <= val * self.PruneC

    def tryCounterHinting(self):
        minloc = midloc = maxloc = 1e40
        mindelta = middelta = maxdelta = 0
        hvl = self.hs.mainValues
        hvll = len(hvl)
        if hvll < 3:
            return False
        bestv = hvl[-3].val
        pbestv = hvl[-4].val if hvll > 3 else 0
        if pbestv > 1000 or bestv < pbestv * 10:
            return False
        for hv in hvl[-3:]:
            loc = hv.lloc
            delta = hv.uloc - loc
            loc += delta / 2
            if loc < minloc:
                maxloc, midloc, minloc = midloc, minloc, loc
                maxdelta, middelta, mindelta = middelta, mindelta, delta
            elif loc < midloc:
                maxloc, midloc = midloc, loc
                maxdelta, middelta = middelta, delta
            else:
                maxloc = loc
                maxdelta = delta
        if (abs(mindelta - maxdelta) < self.DeltaDiffMin and
                abs((maxloc - midloc) - (midloc - minloc)) < self.DeltaDiffMin):
            self.hs.rejectValues.extend(hvl[0:-3])
            self.hs.mainValues = hvl[-3:]
            return True
        if (abs(mindelta - maxdelta) < self.DeltaDiffReport and
                abs((maxloc - midloc) - (midloc - minloc)) < self.DeltaDiffReport):
            self.info("Near miss for %s counter hints." % self.aDesc())
        return False

    def addBBox(self, doSubpaths=False):
        gl = self.glyph
        if doSubpaths:
            paraml = range(len(gl.subpaths))
        else:
            paraml = [None]
        for param in paraml:
            pbs = gl.getBounds(param)
            mn_pt, mx_pt = pt(tuple(pbs.b[0])), pt(tuple(pbs.b[1]))
            peidx = 0 if self.isV() else 1
            for hv in self.hs.mainValues:
                if hv.lloc <= mx_pt.o and mn_pt.o <= hv.uloc:
                    return
            lseg = hintSegment(mn_pt.o, mn_pt.a, mx_pt.a, pbs.extpes[0][peidx],
                               hintSegment.sType.LINE, 0, self.isV(), "l bbox")
            useg = hintSegment(mx_pt.o, mn_pt.a, mx_pt.a, pbs.extpes[1][peidx],
                               hintSegment.sType.LINE, 0, self.isV(), "u bbox")
            hv = stemValue(mn_pt.o, mx_pt.o, 100, 0, lseg, useg, False)
            if not doSubpaths:
                self.insertStemValue(hv, "bboxadd")
            self.hs.mainValues.insert(0, hv)
            self.info("bbb: %r" % self.hs.stemValues)

    def addHintPositon(self, hv):
        pass


class hhinter(hinter):
    def startFlex(self):
        pt.setAlign(False)

    def startHint(self):
        pt.setAlign(False)
        blues = self.fddict.BlueValuesPairs + self.fddict.OtherBlueValuesPairs
        self.topPairs = [pair for pair in blues if not pair[4]]
        self.bottomPairs = [pair for pair in blues if pair[4]]

    startMaskConvert = startFlex

    def dominantStems(self):
        return self.fddict.DominantH

    def isV(self):
        return False

    def inBand(self, loc, isBottom=False):
        if self.name in self.options.noBlues:
            return False
        if isBottom:
            pl = self.bottomPairs
        else:
            pl = self.topPairs
        for p in pl:
            if (p[0] + self.fddict.BlueFuzz >= loc and
                    p[1] - self.fddict.BlueFuzz <= loc):
                return True
        return False

    def hasBands(self):
        return len(self.topPairs) + len(self.bottomPairs) > 0

    def isSpecial(self, lower=False):
        return False

    def aDesc(self):
        return 'horizontal'

    def checkTfm(self):
        self.checkTfmVal(self.hs.decreasingSegs, self.topPairs)
        self.checkTfmVal(self.hs.increasingSegs, self.bottomPairs)

    def checkTfmVal(self, sl, pl):
        for s in sl:
            if not self.checkInsideBands(s.loc, pl):
                self.checkNearBands(s.loc, pl)

    def checkInsideBands(self, loc, pl):
        for p in pl:
            if loc <= p[0] and loc >= p[1]:
                return True
        return False

    def checkNearBands(self, loc, pl):
        for p in pl:
            if loc >= p[1] - self.NearFuzz and loc < p[1]:
                self.info("Near miss above horizontal zone at %f instead of %f."
                          % (loc, p[1]))
            if loc <= p[0] + self.NearFuzz and loc > p[0]:
                self.info("Near miss below horizontal zone at %f instead of %f."
                          % (loc, p[0]))

    def segmentLists(self):
        return self.hs.increasingSegs, self.hs.decreasingSegs

    def isCounterGlyph(self):
        return self.name in self.options.hCounterGlyphs


class vhinter(hinter):
    def startFlex(self):
        pt.setAlign(True)

    startMaskConvert = startHint = startFlex

    def isV(self):
        return True

    def dominantStems(self):
        return self.fddict.DominantV

    def inBand(self, loc, isBottom=False):
        return False

    def hasBands(self):
        return False

    def isSpecial(self, lower=False):
        if lower:
            return self.name in self.options.lowerSpecials
        else:
            return self.name in self.options.upperSpecials

    def aDesc(self):
        return 'vertical'

    def checkTfm(self):
        pass

    def segmentLists(self):
        return self.hs.decreasingSegs, self.hs.increasingSegs

    def isCounterGlyph(self):
        return self.name in self.options.vCounterGlyphs
