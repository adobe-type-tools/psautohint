# Copyright 2021 Adobe. All rights reserved.

"""
Associates segments with points on a pathElement, then builds
these into candidate stem pairs that are evaluated and pruned
to an optimal set.
"""

import logging
import bisect
import math
from copy import copy, deepcopy
from abc import abstractmethod

from fontTools.misc.bezierTools import solveCubic

from .glyphData import pt, feq, fne, stem
from .hintstate import hintSegment, stemValue, glyphHintState, links
from .report import GlyphReport
from .logging import logging_reconfig

log = logging.getLogger(__name__)


class dimensionHinter:
    """
    Common hinting implementation inherited by vertical and horizontal
    variants
    """
    @staticmethod
    def diffSign(a, b):
        return fne(a, 0) and fne(b, 0) and ((a > 0) != (b > 0))

    @staticmethod
    def sameSign(a, b):
        return fne(a, 0) and fne(b, 0) and ((a > 0) == (b > 0))

    def __init__(self, options):
        self.StemLimit = 22  # ((kStackLimit) - 2) / 2), kStackLimit == 46
        """Initialize constant values and miscelaneous state"""
        self.MaxStemDist = 150  # initial maximum stem width allowed for hints
        self.InitBigDist = self.MaxStemDist
        self.BigDistFactor = 23.0 / 20.0
        # MinDist must be <= 168 for ITC Garamond Book It p, q, thorn
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
        # PruneB and PruneValue must be <= 0.01 for Yakout/Light/heM
        self.PruneB = 0.0117187
        self.PruneC = 100
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
        # BandMargin must be < 46 for Americana-Bold d bowl vs stem hinting
        self.BandMargin = 30
        self.MaxFlare = 10
        self.MaxBendMerge = 6
        self.MinHintElementLength = 12
        self.AutoLinearCurveFix = True
        self.MaxFlex = 20
        self.FlexLengthRatioCutoff = 0.11  # .33^2 (ratio of 1:3 or better)
        self.FlexCand = 4
        self.SCurveTangent = 0.025
        self.CPfrac = 0.4
        self.OppoFlatMax = 4
        self.FlatMin = 50
        self.ExtremaDist = 20
        self.NearFuzz = 6
        self.NoOverlapPenalty = 7.0 / 5.0
        # XXX GapDistDenom might have been miscorrected from 40 in ebb49206
        self.GapDistDenom = 40
        self.CloseMerge = 20
        # MaxMerge must be < 3 for Cushing-BookItalic z
        self.MaxMerge = 2
        self.MinHighSegVal = 1.0 / 16.0
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
        """Initialize the state for processing a specific glyph"""
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
        """Reset state for rehinting same glyph"""
        self.Bonus = None
        self.Pruning = True
        if self.isV():
            self.hs = self.glyph.vhs = glyphHintState()
        else:
            self.hs = self.glyph.hhs = glyphHintState()

    def info(self, msg):
        """Log info level message"""
        log.info(f"{self.name}: " + msg)

    def warning(self, msg):
        """Log warning level message"""
        log.warning(f"{self.name}: " + msg)

    def error(self, msg):
        """Log error level message"""
        log.error(f"{self.name}: " + msg)

    def debug(self, msg):
        """Log debug level message"""
        log.debug(f"{self.name}: " + msg)

    # Methods filled in by subclasses
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

    # Flex
    def linearFlexOK(self):
        return False

    def addFlex(self, force=True, inited=False):
        """Path-level interface to add flex hints to current glyph"""
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
        """pathElement-level interface to add flex hints to current glyph"""
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
        if do < 3 * c.s.avg(n.e).a_dist(c.e):
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
            self.info("Remove short spline at %g %g to add flex." % c.e)
            return
        elif real_n.is_close:
            self.info("Move closepath from %g %.gto add flex." % c.e)
            return

        if fne(c.s.a, n.e.a):
            self.info("Curves from %g %g to %g %g " % (*c.s, *n.e) +
                      "near miss for adding flex")
            return

        if (feq(n.s.a, n.cs.a) and feq(n.cs.a, n.ce.a) and
                feq(n.ce.a, n.e.a) and not self.linearFlexOK()):
            self.info("Make curves from %g %g to %g %g" % (*c.s, *n.e) +
                      "non-linear to add flex")  # XXXwhat if only one line?
            return

        c.flex = 1
        n.flex = 2
        self.glyph.flex_count += 1
        self.glyph.changed = True
        if not self.HasFlex:
            self.info("Added flex operators to this glyph." + str(self.isV()))
            self.HasFlex = True

    def calcHintValues(self, lnks, force=True, tryCounter=True):
        """
        Top-level method for calculating stem hints for a glyph in one
        dimension
        """
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
        self.prepForSegs()
        self.genSegs()
        self.limitSegs()
        self.debug("generate %s stem values" % self.aDesc())
        self.Pruning = not (tryCounter and self.isCounterGlyph())
        self.genStemVals()
        self.pruneStemVals()
        self.highestStemVals()
        self.mergeVals()
        self.limitVals()
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
                else:
                    self.highestStemVals()  # for bbox segments
        if len(self.hs.mainValues) == 0:
            self.addBBox(False)
        self.debug("%s results" % self.aDesc())
        if self.hs.counterHinted:
            self.debug("Using %s counter hints." % self.aDesc())
        for hv in self.hs.mainValues:
            hv.show(self.isV(), "result", self)
        self.hs.pruneHintSegs()

        pt.clearAlign()

    # Segments
    def addSegment(self, fr, to, loc, pe1, pe2, typ, desc):
        if pe1 is not None and isinstance(pe1.segment_sub, int):
            subpath, offset = pe1.position
            pe1 = self.glyph.subpaths[subpath][offset]
        if pe2 is not None and isinstance(pe2.segment_sub, int):
            subpath, offset = pe2.position
            pe2 = self.glyph.subpaths[subpath][offset]
        if pe1 == pe2:
            pe2 = None
        self.hs.addSegment(fr, to, loc, pe1, pe2, typ, self.Bonus,
                           self.isV(), desc, self)

    def CPFrom(self, cp2, cp3):
        """Return point cp3 adjusted relative to cp2 by CPFrac"""
        return (cp3 - cp2) * (1.0 - self.CPfrac) + cp2

    def CPTo(self, cp0, cp1):
        """Return point cp1 adjusted relative to cp0 by CPFrac"""
        return (cp1 - cp0) * self.CPfrac + cp0

    def adjustDist(self, v, q):
        return v * q

    def testTan(self, p):
        """Test angle of p (treated as vector) relative to BendTangent"""
        return abs(p.a) > (abs(p.o) * self.BendTangent)

    @staticmethod
    def interpolate(q, v0, q0, v1, q1):
        return v0 + (q - q0) * (v1 - v0) / (q1 - q0)

    def flatQuo(self, p1, p2, doOppo=False):
        """
        Returns a measure of the flatness of the line between p1 and p2

        1 means exactly flat wrt dimension a (or o if doOppo)
        0 means not interestingly flat in dimension a. (or o if doOppo)
        Intermediate values represent degrees of interesting flatness
        """
        d = (p1 - p2).abs()
        if doOppo:
            d = pt(d.y, d.x)
        if feq(d.o, 0):
            return 1
        if feq(d.a, 0):
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
        return result

    def testBend(self, p0, p1, p2):
        """Test of the angle between p0-p1 and p1-p2"""
        d1 = p1 - p0
        d2 = p2 - p1
        dp = d1.dot(d2)
        q = d1.normsq() * d2.normsq()
        # Shouldn't hit this in practice
        if feq(q, 0):
            return False
        return dp * dp / q <= 0.5

    def isCCW(self, p0, p1, p2):
        d0 = p1 - p0
        d1 = p2 - p1
        return d0.x * d1.y >= d1.x * d0.y

    # Generate segments

    def relPosition(self, c, lower=False):
        """
        Return value indicates whether c is in the upper (or lower)
        subpath of the glyph (assuming a strict ordering of subpaths
        in this dimension)
        """
        for subp in self.glyph.subpaths:
            if ((lower and subp[0].s.a < c.s.a) or
                    (not lower and subp[0].s.a > c.s.a)):
                return True
        return False

    # I initially combined the two doBends but the result was more confusing
    # and difficult to debug than having them separate
    def doBendsNext(self, c):
        """
        Adds a BEND segment (short segments marking somewhat flat
        areas) at the end of a spline. In some cases the segment is
        added in both "directions"
        """
        p0, p1, p2 = c.slopePoint(1), c.e, self.glyph.nextSlopePoint(c)
        if feq(p0.o, p1.o) or p2 is None:
            return
        osame = self.diffSign(p2.o - p1.o, p1.o - p0.o)
        if osame or (self.testTan(p1 - p2) and
                     (self.diffSign(p2.a - p1.a, p1.a - p0.a) or
                      (self.flatQuo(p0, p1, doOppo=True) > 0 and
                       self.testBend(p0, p1, p2)))):
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
        """
        Adds a BEND segment (short segments marking somewhat flat
        areas) at the start of a spline. In some cases the segment is
        added in both "directions"
        """
        p0, p1, p2 = c.s, c.slopePoint(0), self.glyph.prevSlopePoint(c)
        if feq(p0.o, p1.o) or p2 is None:
            return
        cs = self.glyph.prevInSubpath(c, segSub=True)
        osame = self.diffSign(p2.o - p0.o, p0.o - p1.o)
        if osame or (self.testTan(p0 - p2) and
                     (self.diffSign(p2.a - p0.a, p0.a - p1.a) or
                      (self.flatQuo(p1, p0, doOppo=True) > 0 and
                       self.testBend(p2, p0, p1)))):
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
        """
        Returns true if the junction of this spline and the next
        (or previous) is sufficiently flat, measured by OppoFlatMax
        and FlatMin
        """
        if not c:
            return
        if doPrev:
            sp = self.glyph.prevSlopePoint(c)
            if sp is None:
                return False
            d = (sp - c.cs).abs()
        else:
            sp = self.glyph.nextSlopePoint(c)
            if sp is None:
                return False
            d = (sp - c.ce).abs()
        return d.o <= self.OppoFlatMax and d.a >= self.FlatMin

    def sameDir(self, c, doPrev=False):
        """
        Returns True if the next (or previous) spline continues in roughly
        the same direction as c
        """
        if not c:
            return False
        if doPrev:
            p = self.glyph.prevInSubpath(c, skipTiny=True, segSub=True)
            if p is None:
                return
            p0, p1, p2 = c.e, c.s, p.s
        else:
            n = self.glyph.nextInSubpath(c, skipTiny=True, segSub=True)
            if n is None:
                return
            p0, p1, p2 = c.s, c.e, n.e
        if (self.diffSign(p0.y - p1.y, p1.y - p2.y) or
                self.diffSign(p0.x - p1.x, p1.x - p2.x)):
            return False
        return not self.testBend(p0, p1, p2)

    def extremaSegment(self, pe, extp, extt, isMn):
        """
        Given a curved pathElement pe and a point on that spline extp at
        t == extt, calculates a segment intersecting extp where all portions
        of the segment are within ExtremaDist of pe
        """
        a, b, c, d = pe.cubicParameters()
        loc = round(extp.o) + (-self.ExtremaDist
                               if isMn else self.ExtremaDist)

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
        """
        Picks a segment location based on candidates p0 and p1 and
        other locations and metrics picked from the spline and
        the adjacent splines. Locations within BlueValue bands are
        priviledged.
        """
        inBand0 = self.inBand(p0.o, dist >= 0)
        inBand1 = self.inBand(p1.o, dist >= 0)
        if inBand0 and not inBand1:
            return p0.o
        if inBand1 and not inBand0:
            return p1.o
        if feq(p0.o, pp0.o) and fne(p1.o, pp1.o):
            return p0.o
        if fne(p0.o, pp0.o) and feq(p1.o, pp1.o):
            return p1.o
        if (prv and feq(p0.o, prv.o)) and (not nxt or fne(p1.o, nxt.o)):
            return p0.o
        if (not prv or fne(p0.o, prv.o)) and (prv and feq(p1.o, nxt.o)):
            return p1.o
        if inBand0 and inBand1:
            upper, lower = (p1.o, p0.o) if p0.o < p1.o else (p0.o, p1.o)
            return upper if dist < 0 else lower
        if abs(pp0.a - p0.a) > abs(pp1.a - p1.a):
            return p0.o
        if abs(pp1.a - p1.a) > abs(pp0.a - p0.a):
            return p1.o
        if prv and feq(p0.o, prv.o) and nxt and feq(p1.o, nxt.o):
            if abs(p0.a - prv.a) > abs(p1.a - nxt.a):
                return p0.o
            return p1.o
        return (p0.o + p1.o) / 2

    def cpDirection(self, p0, p1, p2):
        """
        Utility function for detecting singly-inflected curves.
        See original C code or "Fast Detection o the Geometric Form of
        Two-Dimensional Cubic Bezier Curves" by Stephen Vincent
        """
        det = (p0.x * (p1.y - p2.y) +
               p1.x * (p2.y - p0.y) +
               p2.x * (p0.y - p1.y))
        if det > 0:
            return 1
        elif det < 0:
            return -1
        return 0

    def prepForSegs(self):
        for c in self.glyph:
            if (not c.isLine() and
                    (self.cpDirection(c.s, c.cs, c.ce) !=
                     self.cpDirection(c.cs, c.ce, c.e))):
                if c.splitAtInflectionsForSegs():
                    self.debug("splitting at inflection point in %d %d" %
                               (c.position[0], c.position[1] + 1))

    def genSegs(self):
        """
        Calls genSegsForPathElement for each pe and cleans up the
        generated segment lists
        """
        self.Bonus = 0
        c = self.glyph.next(self.glyph, segSub=True)
        while c:
            self.genSegsForPathElement(c)
            c = self.glyph.next(c, segSub=True)

        self.hs.compactLists()
        self.hs.remExtraBends(self)
        self.hs.cleanup()
        self.checkTfm()

    def genSegsForPathElement(self, c):
        """
        Calculates and adds segments for pathElement c. These segments
        indicate "flat" areas of the glyph in the relevant dimension
        weighted by segment length.
        """
        prv = self.glyph.prevInSubpath(c, segSub=True)
        self.debug("Element %d %d" % (c.position[0], c.position[1] + 1))
        if c.isStart():
            # Certain glyphs with one contour above (or # below) all others
            # are marked as "Special", so that segments on that contour
            # are given a bonus weight increasing the likelihood of their
            # segment locations ending up in the final set of stem hints.
            self.Bonus = 0
            if (self.isSpecial(lower=False) and
                    self.relPosition(c, lower=False)):
                self.Bonus = self.SpecialCharBonus
            elif (self.isSpecial(lower=True) and
                  self.relPosition(c, lower=True)):
                self.Bonus = self.SpecialCharBonus
        if c.isLine() and not c.isTiny():
            # If the line is completely flat, add the line itself as a
            # segment. Otherwise if it is somewhat flat add a segment
            # reduced in length by adjustDist between the start and end
            # points at the location picked by pickSpot. Warn if the
            # line is close to but not quite horizontal.
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
                    # XXX If we're picking the spot why aren't we
                    # picking the pair of path elements to pass
                    # to addSegment?
                    sp = self.pickSpot(c.s, c.e, adist, c.s, c.e,
                                       self.glyph.prevSlopePoint(c),
                                       self.glyph.nextSlopePoint(c))
                    self.addSegment(aavg - adist, aavg + adist, sp, prv, c,
                                    hintSegment.sType.LINE, "line")
                    d = (c.s - c.e).abs()
                    if d.o <= 2 and (d.a > 10 or d.normsq() > 100):
                        self.info("The line from %g %g to %g %g" %
                                  (*c.s, *c.e) +
                                  " is not exactly " + self.aDesc())
            else:
                # If the line is not somewhat flat just add BEND segments
                self.doBendsNext(c)
                self.doBendsPrev(c)
        elif not c.isLine():
            if c.flex == 2:
                # Flex-hinted curves have a segment corresponding to the
                # line between the curve-pair endpoints. For a pair flat
                # in the relevant dimension this is the line used for the
                # pair at lower resolutions.
                fl1prv = self.glyph.prevInSubpath(prv, segSub=True)
                if self.flatQuo(prv.s, c.e) > 0:
                    self.addSegment(prv.s.a, c.e.a, c.e.o, fl1prv, c,
                                    hintSegment.sType.LINE, "flex")
            if c.flex != 2:
                # flex != 2 => the start point is not in the middle of a
                # flex hint, so this code is for hinting the start points
                # of curves.
                #
                # If the start of the segment is not interestingly flat
                # just add BEND segments. Otherwise add a CURVE segment
                # for the start of the curve if the start slope is flat
                # or the node is (relative to the previous spline) either
                # flat or it bends away sharply.
                #
                # q2 measures the slope of the start point to the second
                # control point and just functions to modify the
                # segment length calculation.
                q = self.flatQuo(c.cs, c.s)
                if q == 0:
                    self.doBendsPrev(c)
                else:
                    if (feq(c.cs.o, c.s.o) or
                        (fne(c.ce.o, c.e.o) and
                         (self.nodeIsFlat(c, doPrev=True) or
                          not self.sameDir(c, doPrev=True)))):
                        q2 = self.flatQuo(c.ce, c.s)
                        if (q2 > 0 and self.sameSign(c.cs.a - c.s.a,
                                                     c.ce.a - c.s.a) and
                                abs(c.ce.a - c.s.a) > abs(c.cs.a - c.s.a)):
                            adist = self.adjustDist(self.CPTo(c.cs.a,
                                                              c.ce.a) -
                                                    c.s.a, q2)
                            end = self.adjustDist(self.CPTo(c.s.a,
                                                            c.cs.a) -
                                                  c.s.a, q)
                            if abs(end) > abs(adist):
                                adist = end
                            self.addSegment(c.s.a, c.s.a + adist, c.s.o,
                                            prv, c,
                                            hintSegment.sType.CURVE,
                                            "curve start 1")
                        else:
                            adist = self.adjustDist(self.CPTo(c.s.a,
                                                              c.cs.a) -
                                                    c.s.a, q)
                            self.addSegment(c.s.a, c.s.a + adist, c.s.o,
                                            prv, c,
                                            hintSegment.sType.CURVE,
                                            "curve start 2")
            if c.flex != 1:
                # flex != 1 => the end point is not in the middle of a
                # flex hint, so this code is for hinting the end points
                # of curves.
                #
                # If the end of the segment is not interestingly flat
                # just add BEND segments. Otherwise add a CURVE segment
                # for the start of the curve if the start slope is flat
                # or the node is (relative to the previous spline) either
                # flat or it bends away sharply.
                #
                # The second and third segment types correspond to the
                # first and second start point types. The first handles
                # the special case of a close-to-flat curve, treating
                # it somewhat like a close-to-flat line.
                q = self.flatQuo(c.ce, c.e)
                if q == 0:
                    self.doBendsNext(c)
                elif (feq(c.ce.o, c.e.o) or
                      (fne(c.cs.o, c.s.o) and
                       (self.nodeIsFlat(c, doPrev=False) or
                        not self.sameDir(c, doPrev=False)))):
                    adist = self.adjustDist(c.e.a -
                                            self.CPFrom(c.ce.a, c.e.a), q)
                    q2 = self.flatQuo(c.s, c.e)
                    if q2 > 0:
                        ad2 = self.adjustDist(c.e.a - c.s.a, q2)
                    else:
                        ad2 = 0
                    if q2 > 0 and abs(ad2) > abs(adist):
                        ccs, cce = c.cs, c.ce
                        if (feq(c.s.o, c.cs.o) and feq(c.cs.o, c.ce.o) and
                                feq(c.ce.o, c.e.o)):
                            if (self.options.allowChanges and
                                    self.AutoLinearCurveFix):
                                c.convertToLine()
                                ccs, cce = c.s, c.e
                                self.info("Curve from %s to %s" % (c.s,
                                                                   c.e) +
                                          "changed to a line.")
                            else:
                                self.info("Curve from %s to %s" % (c.s,
                                                                   c.e) +
                                          "should be changed to a line.")
                        adist = ad2 / 2
                        aavg = (c.s.a + c.e.a) / 2
                        sp = self.pickSpot(c.s, c.e, adist, ccs, cce,
                                           self.glyph.prevSlopePoint(c),
                                           self.glyph.nextSlopePoint(c))
                        self.addSegment(aavg - adist, aavg + adist, sp, c,
                                        None, hintSegment.sType.CURVE,
                                        "curve end 1")
                    else:
                        q2 = self.flatQuo(c.cs, c.e)
                        if (q2 > 0 and self.sameSign(c.cs.a - c.e.a,
                                                     c.ce.a - c.e.a) and
                                abs(c.ce.a - c.e.a) < abs(c.cs.a - c.e.a)):
                            aend = self.adjustDist(c.e.a -
                                                   self.CPFrom(c.cs.a,
                                                               c.ce.a),
                                                   q2)
                            if abs(aend) > abs(adist):
                                adist = aend
                            self.addSegment(c.e.a - adist, c.e.a, c.e.o, c,
                                            None, hintSegment.sType.CURVE,
                                            "curve end 2")
                        else:
                            self.addSegment(c.e.a - adist, c.e.a, c.e.o, c,
                                            None, hintSegment.sType.CURVE,
                                            "curve end 3")
            if c.flex is None:
                # Curves that are not part of a flex hint can have an
                # extrema that is flat in the relative dimension. This
                # calls farthestExtreme() and if there is such an extrema
                # more than 2 points away from the start and end points
                # it calls extremaSegment() and adds the segment.
                tmp = c.getBounds().farthestExtreme(not self.isV())
                d, extp, extt, isMin = tmp
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
                                    "curve extrema")

    def limitSegs(self):
        maxsegs = max(len(self.hs.increasingSegs), len(self.hs.decreasingSegs))
        if (not self.options.explicitGlyphs and
                maxsegs > self.options.maxSegments):
            self.warning("Calculated %d segments, skipping %s stem testing" %
                         (maxsegs, self.aDesc()))
            self.hs.deleteSegments()

    def showSegs(self):
        """
        Adds a debug log message for each generated segment.
        This information is redundant with the genSegs info except that
        it shows the result of processing with compactLists(),
        remExtraBends(), etc.
        """
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

    # Generate candidate stems with values

    def genStemVals(self):
        """
        Pairs segments of opposite direction and adds them as potential
        stems weighted by evalPair(). Also adds ghost stems for segments
        within BlueValue bands
        """
        ll, ul = self.segmentLists()

        for ls in ll:
            for us in ul:
                if ls.loc > us.loc:
                    continue
                val, spc = self.evalPair(ls, us)
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
        self.combineStemValues()

    def evalPair(self, ls, us):
        """
        Calculates the initial "value" and "special" weights of a potential
        stem.

        Stems in one BlueValue band are given a spc boost but stems in
        both are ignored (presuambly because the Blues and OtherBlues are
        sufficient for hinting).

        Otherwise the value is based on:
           o The distance between the segment locations
           o The segment lengths
           o the extent of segment overlap (in the opposite direction)
           o Segment "bonus" values
        """
        spc = 0
        loc_d = abs(us.loc - ls.loc)
        if loc_d < self.MinDist:
            return 0, spc
        inBBand = self.inBand(ls.loc, isBottom=True)
        inTBand = self.inBand(us.loc, isBottom=False)
        if inBBand and inTBand:
            return 0, spc
        if inBBand or inTBand:
            spc += 2
        if us.min <= ls.max and us.max >= ls.min:  # overlap
            overlen = min(us.max, ls.max) - max(us.min, ls.min)
            minlen = min(us.max - us.min, ls.max - ls.min)
            if feq(overlen, minlen):
                dist = loc_d
            else:
                dist = loc_d * (1 + .4 * (1 - overlen / minlen))
        else:
            o_d = min(abs(us.min - ls.max), abs(us.max - ls.min))
            dist = round(loc_d * self.NoOverlapPenalty +
                         o_d * o_d / self.GapDistDenom)
            if o_d > loc_d:  # XXX this didn't work before
                dist *= o_d / loc_d
        dist = max(dist, 2 * self.MinDist)
        if min(ls.bonus, us.bonus) > 0:
            spc += 2
        for dsw in self.dominantStems():
            if dsw == abs(loc_d):
                spc += 1
                break
        dist = max(dist, 2)
        bl = max(ls.max - ls.min, 2)
        ul = max(us.max - us.min, 2)
        rl = bl * bl
        ru = ul * ul
        q = dist * dist
        v = 1000 * rl * ru / (q * q)
        if loc_d > self.BigDist:
            fac = self.BigDist / loc_d
            if fac > .5:
                v *= fac**8
            else:
                return 0, spc
        v = max(v, self.MinValue)
        v = min(v, self.MaxValue)
        return v, spc

    def stemMiss(self, ls, us):
        """
        Adds an info message for each stem within two em-units of a dominant
        stem width
        """
        loc_d = abs(us.loc - ls.loc)

        if loc_d < self.MinDist:
            return 0

        if us.min > ls.max or us.max < ls.min:  # no overlap
            return

        overlen = min(us.max, ls.max) - max(us.min, ls.min)
        minlen = min(us.max - us.min, ls.max - ls.min)
        if feq(overlen, minlen):
            dist = loc_d
        else:
            dist = loc_d * (1 + .4 * (1 - overlen / minlen))
        if dist < self.MinDist / 2:
            return

        d, nearStem = min(((abs(s - loc_d), s) for s in self.dominantStems()))
        if d == 0 or d > 2:
            return
        curved = (ls.type == hintSegment.sType.CURVE or
                  us.type == hintSegment.sType.CURVE)
        self.info("%s %s stem near miss: %g instead of %g at %g to %g." %
                  (self.aDesc(), "curve" if curved else "linear", loc_d,
                   nearStem, ls.loc, us.loc))

    def addStemValue(self, lloc, uloc, val, spc, lseg, useg):
        """Adapts the stem parameters into a stemValue object and adds it"""
        if val == 0:
            return
        if (self.Pruning and val < self.PruneValue) and spc <= 0:
            return
        if (lseg.type == hintSegment.sType.BEND and
                useg.type == hintSegment.sType.BEND):
            return
        ghst = (lseg.type == hintSegment.sType.GHOST or
                useg.type == hintSegment.sType.GHOST)
        if not ghst and (self.Pruning and val <= self.PruneD) and spc <= 0:
            if (lseg.type == hintSegment.sType.BEND or
                    useg.type == hintSegment.sType.BEND):
                return
            lpesub = lseg.pe().position[0]
            upesub = useg.pe().position[0]
            if lpesub != upesub:
                lsb = self.glyph.getBounds(lpesub)
                usb = self.glyph.getBounds(upesub)
                if not lsb.within(usb) and not usb.within(lsb):
                    return
        if not useg:
            return

        sv = stemValue(lloc, uloc, val, spc, lseg, useg, ghst)
        self.insertStemValue(sv)

    def insertStemValue(self, sv, note="add"):
        """
        Adds a stemValue object into the stemValues list in sort order,
        skipping redundant GHOST stems
        """
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
        """
        Adjusts the values of stems with the same locations to give them
        each the same combined value.
        """
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

    # Prune unneeded candidate stems

    def pruneStemVals(self):
        """
        Prune (remove) candidate stems based on comparisons to other stems.
        """
        ignorePruned = not self.isV() and (self.options.report_zones or
                                           not self.options.report_stems)
        svl = self.hs.stemValues
        for c in svl:
            otherLow = otherHigh = False
            uInBand = self.inBand(c.uloc, isBottom=False)
            lInBand = self.inBand(c.lloc, isBottom=True)
            for d in svl:
                if d.pruned and not ignorePruned:
                    continue
                if (not c.isGhost and d.isGhost and
                        not d.val > c.val * self.VeryMuchPF):
                    continue
                if feq(c.lloc, d.lloc) and feq(c.uloc, d.uloc):
                    continue
                if self.isV() and d.val <= c.val * self.PruneFactor:
                    continue
                csl = self.closeSegs(c.lseg, d.lseg)
                csu = self.closeSegs(c.useg, d.useg)
                if c.val < 100 and d.val > c.val * self.MuchPF:
                    cs_tst = csl or csu
                else:
                    cs_tst = csl and csu
                if (d.val > c.val * self.PruneFactor and
                    c.lloc - self.PruneDistance <= d.lloc and
                    c.uloc + self.PruneDistance >= d.uloc and cs_tst and
                    (self.isV() or c.val < 16 or
                     ((not uInBand or feq(c.uloc, d.uloc)) and
                      (not lInBand or feq(c.lloc, d.lloc))))):
                    self.prune(c, d, "close and higher value")
                    break
                if c.lseg is not None and c.useg is not None:
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
        """
        Returns true if the segments (and the path between them)
        are within CloseMerge of one another
        """
        if not s1 or not s2:
            return False
        if s1 is s2 or not s1.pe or not s2.pe or s1.pe is s2.pe:
            return True
        if s1.loc < s2.loc:
            loca, locb = s1.loc, s2.loc
        else:
            locb, loca = s1.loc, s2.loc
        if (locb - loca) > 5 * self.CloseMerge:
            return False
        loca -= self.CloseMerge
        locb += self.CloseMerge
        n = s1.pe()
        p = self.glyph.prevInSubpath(n)
        pe2 = s2.pe()
        ngood = pgood = True
        # To be close in this sense the segments should be in the same
        # subpath and are connected by a route that stays within CloseMerge
        # So we trace around the s1.pe's subpath looking for pe2 in each
        # direction and give up once we've iterated the length of the subpath.
        for cnt in range(len(self.glyph.subpaths[n.position[0]])):
            if not ngood and not pgood:
                return False
            assert n and p
            if (ngood and n is pe2) or (pgood and p is pe2):
                return True
            if n.e.o > locb or n.e.o < loca:
                ngood = False
            if p.e.o > locb or p.e.o < loca:
                pgood = False
            n = self.glyph.nextInSubpath(n)
            p = self.glyph.prevInSubpath(p)
        return False

    def prune(self, sv, other_sv, desc):
        """
        Sets the pruned property on sv and logs it and the "better" stemValue
        """
        self.debug("Prune %s val: %s" % (self.aDesc(), desc))
        sv.show(self.isV(), "pruned", self)
        other_sv.show(self.isV(), "pruner", self)
        sv.pruned = True

    # Associate segments with the highest valued close stem

    def highestStemVals(self):
        """
        Associates each segment in both lists with the highest related stemVal,
        pruning stemValues with no association
        """
        for sv in self.hs.stemValues:
            sv.pruned = True
        ll, ul = self.segmentLists()
        self.findHighestValForSegs(ul, True)
        self.findHighestValForSegs(ll, False)
        self.hs.stemValues = [sv for sv in self.hs.stemValues
                              if not sv.pruned]

    def findHighestValForSegs(self, segl, isU):
        """Associates each segment in segl with the highest related stemVal"""
        for seg in segl:
            ghst = None
            highest = self.findHighestVal(seg, isU, False)
            if highest is not None and highest.isGhost:
                nonGhst = self.findHighestVal(seg, isU, True)
                if nonGhst is not None and nonGhst.val >= 2:
                    ghst = highest
                    highest = nonGhst
            if highest is not None:
                if not (highest.val < self.MinHighSegVal and
                        (ghst is None or ghst.val < self.MinHighSegVal)):
                    highest.pruned = False
                    seg.hintval = highest

    def findHighestVal(self, seg, isU, locFlag):
        """Finds the highest stemVal related to seg"""
        highest = None
        svl = self.hs.stemValues

        def OKcond(sv):
            vs, vl = (sv.useg, sv.uloc) if isU else (sv.lseg, sv.lloc)
            if locFlag:
                loctest = not sv.isGhost
            else:
                loctest = vs is seg or self.closeSegs(vs, seg)
            return (abs(vl - seg.loc) <= self.MaxMerge and loctest and
                    self.considerValForSeg(sv, seg, isU))

        try:
            _, highest = max(((sv.compVal(self.SpcBonus, self.GhostFactor), sv)
                              for sv in svl if OKcond(sv)))
        except ValueError:
            pass
        self.debug("findHighestVal: loc %g min %g max %g" %
                   (seg.loc, seg.min, seg.max))
        if highest:
            highest.show(self.isV(), "highest", self)
        else:
            self.debug("NULL")
        return highest

    def considerValForSeg(self, sv, seg, isU):
        """Utility test for findHighestVal"""
        if sv.spc > 0 or self.inBand(seg.loc, not isU):
            return True
        return not (self.Pruning and sv.val < self.PruneB)

    # Merge related candidate stems

    def findBestValues(self):
        """
        Looks among stemValues with the same locations and finds the one
        with the highest spc/val. Assigns that stemValue to the .best
        property of that set
        """
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
        """
        Finds each stemValue at oldl, oldu and gives it a new "best"
        stemValue reference and its val and spc.
        """
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
        """
        Finds stem pairs with sides close to one another (in different
        senses) and uses replaceVals() to substitute one for another
        """
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
                                replace = True
                        elif svuIB:
                            if bst.uloc < sv.uloc:
                                replace = True
                    else:
                        replace = True
                elif (feq(bst.best.spc, sv.spc) and bst.lseg is not None and
                      bst.useg is not None):
                    if sv.lseg is not None and sv.useg is not None:
                        if (abs(bst.lloc - sv.lloc) <= 1 and
                                abs(bst.uloc - sv.uloc) <= self.MaxBendMerge):
                            if (sv.useg.type == hintSegment.sType.BEND and
                                    not svuIB):
                                replace = True
                        elif (abs(bst.uloc - sv.uloc) <= 1 and
                              abs(bst.lloc - sv.lloc) <= self.MaxBendMerge):
                            if (sv.lseg.type == hintSegment.sType.BEND and
                                    not svlIB):
                                replace = True
                if replace:
                    self.replaceVals(sv.lloc, sv.uloc, bst.lloc, bst.uloc,
                                     bst.best)

    # Limit number of stems

    def limitVals(self):
        """
        Limit the number of stem values in a dimension
        """
        svl = self.hs.stemValues
        if len(svl) <= self.StemLimit:
            return

        self.info("Trimming stem list to %g from %g" %
                  (self.StemLimit, len(svl)))
        # This will leave some segments with .highest entries that aren't
        # part of the stemValues list, but those won't get .idx values so
        # things will mostly work out. We could do better trying to find
        # alternative hints for segments. (Thee previous code just chopped
        # the list at one end.)
        ol = [(sv.compVal(self.SpcBonus), sv) for sv in svl]
        ol.sort(reverse=True)

        svl[:] = sorted((sv for weight, sv in ol[:self.StemLimit]))

    # Reporting

    def checkVals(self):
        """Reports stems with widths close to a dominant stem width"""
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
            lPrev, uPrev = l, u

    def findLineSeg(self, loc, isBottom=False):
        """Returns LINE segments with the passed location"""
        for s in self.segmentLists()[0 if isBottom else 1]:
            if feq(s.loc, loc) and s.type == hintSegment.sType.LINE:
                return True
        return False

    def reportStems(self):
        """Reports stem zones and char ("alignment") zones"""
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

    # "Main" stems

    def mainVals(self):
        """Picks set of highest-valued non-overlapping stems"""
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
            mainValues.append(best)
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
                if llocsv <= ulocb and ulocsv >= llocb:
                    rejectValues.append(svl[i])
                    del svl[i]
                else:
                    i += 1
        rejectValues.extend(svl)
        self.hs.mainValues = mainValues
        self.hs.rejectValues = rejectValues

    def mainOK(self, spc, val, hasHints, prevBV):
        """Utility test for mainVals"""
        if spc > 0:
            return True
        if not hasHints:
            return not self.Pruning or val >= self.PruneD
        if not self.Pruning or val > self.PruneA:
            return True
        if self.Pruning and val < self.PruneB:
            return False
        return not self.Pruning or prevBV <= val * self.PruneC

    def tryCounterHinting(self):
        """
        Attempts to counter-hint the dimension with the first three
        (e.g. highest value) mainValue stems
        """
        minloc = midloc = maxloc = 1e40
        mindelta = middelta = maxdelta = 0
        hvl = self.hs.mainValues
        hvll = len(hvl)
        if hvll < 3:
            return False
        bestv = hvl[0].val
        pbestv = hvl[3].val if hvll > 3 else 0
        if pbestv > 1000 or bestv < pbestv * 10:
            return False
        for hv in hvl[0:3]:
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
                abs((maxloc - midloc) - (midloc - minloc)) <
                self.DeltaDiffMin):
            self.hs.rejectValues.extend(hvl[3:])
            self.hs.mainValues = hvl[0:3]
            return True
        if (abs(mindelta - maxdelta) < self.DeltaDiffReport and
                abs((maxloc - midloc) - (midloc - minloc)) <
                self.DeltaDiffReport):
            self.info("Near miss for %s counter hints." % self.aDesc())
        return False

    def addBBox(self, doSubpaths=False):
        """
        Adds the top and bottom (or left and right) sides of the glyph
        as a stemValue -- serves as a backup hint stem when few are found

        When called with doSubpaths == True adds stem hints for the
        top/bottom or right/left of each subpath
        """
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
            self.hs.getPEState(pbs.extpes[0][peidx],
                               True).segments.append(lseg)
            useg = hintSegment(mx_pt.o, mn_pt.a, mx_pt.a, pbs.extpes[1][peidx],
                               hintSegment.sType.LINE, 0, self.isV(), "u bbox")
            self.hs.getPEState(pbs.extpes[1][peidx],
                               True).segments.append(useg)
            hv = stemValue(mn_pt.o, mx_pt.o, 100, 0, lseg, useg, False)
            self.insertStemValue(hv, "bboxadd")
            self.hs.mainValues.append(hv)
            self.hs.mainValues.sort(key=lambda sv: sv.compVal(self.SpcBonus),
                                    reverse=True)

    # masks

    def convertToMasks(self):
        """
        This method builds up the information needed to mostly get away from
        looking at stem values when distributing hintmasks.

        hs.stems: Contains the eventual hstems or vstems object that will be
                  copied into the glyphData object.
        hs.stemConflicts: A map of which stems conflict with which other stems.
        hs.ghostCompat: [i][j] is true if stem i is a ghost and stem j can
                        substitute for it.
        """
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
            svl = sorted(self.hs.mainValues)
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
        if self.hs.counterHinted and hasConflicts:
            self.warning("XXX TEMPORARY WARNING: conflicting counter hints")
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
        """Convert the hints desired by pathElement to a conflict-free mask"""
        l = len(self.hs.stems)
        mask = [False] * l
        for seg in pestate.segments:
            if not seg.hintval or seg.hintval.idx is None:
                continue
            mask[seg.hintval.idx] = True
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
                            self.debug("Resolved conflicting hints at %g %g" %
                                       (c.e.x, c.e.y))
                        else:
                            mask[i] = mask[j] = False
                            self.debug("Could not resolve conflicting hints" +
                                       " at %g %g" % (c.e.x, c.e.y) +
                                       ", removing both")
        if True in mask:
            maskstr = ''.join(('1' if i else '0' for i in mask))
            self.debug("%s mask %s at %g %g" %
                       (self.aDesc(), maskstr, c.e.x, c.e.y))
            pestate.mask = mask
        else:
            pestate.mask = None

    def OKToRem(self, loc, spc):
        return (spc == 0 or
                (not self.inBand(loc, False) and not self.inBand(loc, True)))


class hhinter(dimensionHinter):
    def startFlex(self):
        """Make pt.a map to x and pt.b map to y"""
        pt.setAlign(False)

    def startHint(self):
        """
        Make pt.a map to x and pt.b map to y and store BlueValue bands
        for easier processing
        """
        pt.setAlign(False)
        blues = self.fddict.BlueValuesPairs + self.fddict.OtherBlueValuesPairs
        self.topPairs = [pair for pair in blues if not pair[4]]
        self.bottomPairs = [pair for pair in blues if pair[4]]

    startMaskConvert = startFlex

    def dominantStems(self):
        return self.fddict.DominantH

    def isV(self):
        """Mark the hinter as horizontal rather than vertical"""
        return False

    def inBand(self, loc, isBottom=False):
        """Return true if loc is within the selected set of bands"""
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
                self.info("Near miss above horizontal zone at " +
                          "%f instead of %f." % (loc, p[1]))
            if loc <= p[0] + self.NearFuzz and loc > p[0]:
                self.info("Near miss below horizontal zone at " +
                          "%f instead of %f." % (loc, p[0]))

    def segmentLists(self):
        return self.hs.increasingSegs, self.hs.decreasingSegs

    def isCounterGlyph(self):
        return self.name in self.options.hCounterGlyphs


class vhinter(dimensionHinter):
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
        """Check the Specials list for the current glyph"""
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


class glyphHinter:
    """
    Adapter between high-level autohint.py code and the 1D hinter.
    Also contains code that uses hints from both dimensions, primarily
    for hintmask distribution
    """
    impl = None

    @classmethod
    def initialize(_cls, options, fontDictList, logQueue=None):
        _cls.impl = _cls(options, fontDictList)
        if logQueue is not None:
            logging_reconfig(logQueue, options.verbose)

    @classmethod
    def hint(_cls, name, glyphTuple=None, fdIndex=0):
        if _cls.impl is None:
            raise RuntimeError("glyphHinter implementation not initialized")
        if isinstance(name, tuple):
            return _cls.impl._hint(*name)
        else:
            return _cls.impl._hint(name, glyphTuple, fdIndex)

    def __init__(self, options, fontDictList):
        self.options = options
        self.fontDictList = fontDictList
        self.hHinter = hhinter(options)
        self.vHinter = vhinter(options)
        self.name = ""
        self.cnt = 0
        if options.justReporting():
            self.taskDesc = 'analysis'
        else:
            self.taskDesc = 'hinting'

        self.FlareValueLimit = 1000
        self.MaxHalfMargin = 20  # XXX 10 might better match original C code
        self.PromotionDistance = 50

    def getSegments(self, glyph, pe, oppo=False):
        """Returns the list of segments for pe in the requested dimension"""
        gstate = glyph.vhs if (self.doV == (not oppo)) else glyph.hhs
        pestate = gstate.getPEState(pe)
        return pestate.segments if pestate else []

    def getMasks(self, glyph, pe):
        """
        Returns the masks of hints needed by/desired for pe in each dimension
        """
        masks = []
        for i, hs in enumerate((glyph.hhs, glyph.vhs)):
            mask = None
            if hs.keepHints:
                if pe.mask:
                    mask = copy(pe.mask[i])
            else:
                pes = hs.getPEState(pe)
                if pes and pes.mask:
                    mask = copy(pes.mask)
            if mask is None:
                mask = [False] * len(hs.stems)
            masks.append(mask)
        return masks

    def _hint(self, name, glyphTuple, fdIndex=0):
        """Top-level flex and stem hinting method for a glyph"""
        glyph = glyphTuple[0]
        fddict = self.fontDictList[fdIndex]
        an = self.options.nameAliases.get(name, name)
        if an != name:
            log.info("%s (%s): Begin %s (using fdDict '%s').",
                     an, name, self.taskDesc, fddict.DictName)
        else:
            log.info("%s: Begin %s (using fdDict '%s').",
                     name, self.taskDesc, fddict.DictName)

        self.doV = False
        gr = GlyphReport(name, self.options.report_all_stems)
        self.name = name
        self.hHinter.setGlyph(fddict, gr, glyph, name)
        self.vHinter.setGlyph(fddict, gr, glyph, name)

        glyph.changed = False

        if not self.options.noFlex:
            self.hHinter.addFlex()
            self.vHinter.addFlex(inited=True)

        lnks = links(glyph)

        self.hHinter.calcHintValues(lnks)
        self.vHinter.calcHintValues(lnks)

        if self.options.justReporting():
            return name, gr

        if self.hHinter.keepHints and self.vHinter.keepHints:
            return name, None

        if self.options.allowChanges:
            neworder = lnks.shuffle(self.hHinter)  # hHinter serves as log
            if neworder:
                glyph.reorder(neworder, self.hHinter)  # hHinter serves as log

        self.listHintInfo(glyph)

        if not self.hHinter.keepHints:
            self.remFlares(glyph)
        self.doV = True
        if not self.vHinter.keepHints:
            self.remFlares(glyph)

        self.hHinter.convertToMasks()
        self.vHinter.convertToMasks()

        self.distributeMasks(glyph)

        glyph.clearTempState()

        self.cnt += 1

#        if self.cnt % 100 == 0:
#            print(name, self.cnt, gc.collect())
#            print(name, self.cnt)

        return name, (glyph,)

    def distributeMasks(self, glyph):
        """
        When necessary, chose the locations and contents of hintmasks for
        the glyph
        """
        log = self.hHinter
        stems = [None, None]
        masks = [None, None]
        lnstm = [0, 0]
        # Initial horizontal data
        # If keepHints was true hhs.stems was already set to glyph.hstems in
        # converttoMasks()
        stems[0] = glyph.hstems = glyph.hhs.stems
        lnstm[0] = len(stems[0])
        if self.hHinter.keepHints:
            if glyph.startmasks and glyph.startmasks[0]:
                masks[0] = glyph.startmasks[0]
            elif not glyph.hhs.hasConflicts:
                masks[0] = [True] * lnstm[0]
            else:
                pass  # XXX existing hints have conflicts but no start mask
        else:
            masks[0] = [False] * lnstm[0]

        # Initial vertical data
        stems[1] = glyph.vstems = glyph.vhs.stems
        lnstm[1] = len(stems[1])
        if self.vHinter.keepHints:
            if glyph.startmasks and glyph.startmasks[1]:
                masks[1] = glyph.startmasks[1]
            elif not glyph.hhs.hasConflicts:
                masks[1] = [True] * lnstm[1]
            else:
                pass  # XXX existing hints have conflicts but no start mask
        else:
            masks[1] = [False] * lnstm[1]

        self.buildCounterMasks(glyph)

        if not glyph.hhs.hasConflicts and not glyph.vhs.hasConflicts:
            glyph.startmasks = None
            glyph.is_hm = False
            return

        usedmasks = deepcopy(masks)
        if glyph.hhs.counterHinted:
            usedmasks[0] = [mv or uv for mv, uv in
                            zip(glyph.hhs.mainMask, usedmasks[0])]
        if glyph.vhs.counterHinted:
            usedmasks[1] = [mv or uv for mv, uv in
                            zip(glyph.vhs.mainMask, usedmasks[1])]

        glyph.is_hm = True
        glyph.startmasks = masks
        NOTSHORT, SHORT, CONFLICT = 0, 1, 2
        mode = NOTSHORT
        ns = None
        c = glyph.nextForHints(glyph)
        while c:
            if c.isShort() or c.flex == 2:
                if mode == NOTSHORT:
                    if ns:
                        mode = SHORT
                        oldmasks = masks
                        masks = deepcopy(masks)
                        incompatmasks = self.getMasks(glyph, ns)
                    else:
                        mode = CONFLICT
            else:
                ns = c
                if mode == SHORT:
                    oldmasks[:] = masks
                    masks = oldmasks
                    incompatmasks = None
                mode = NOTSHORT
            cmasks = self.getMasks(glyph, c)
            candmasks, conflict = self.joinMasks(masks, cmasks,
                                                 mode == CONFLICT)
            maskstr = ''.join(('1' if i else '0'
                               for i in (candmasks[0] + candmasks[1])))
            log.debug("mask %s at %g %g, mode %d, conflict: %r" %
                      (maskstr, c.e.x, c.e.y, mode, conflict))
            if conflict:
                if mode == NOTSHORT:
                    self.bridgeMasks(glyph, masks, cmasks, usedmasks, c)
                    masks = c.masks = cmasks
                elif mode == SHORT:
                    assert ns
                    newinc, _ = self.joinMasks(incompatmasks, cmasks, True)
                    self.bridgeMasks(glyph, oldmasks, newinc, usedmasks, ns)
                    masks = ns.masks = newinc
                    mode = CONFLICT
                else:
                    assert mode == CONFLICT
                    masks[:] = candmasks
            else:
                masks[:] = candmasks
                if mode == SHORT:
                    incompatmasks, _ = self.joinMasks(incompatmasks, cmasks,
                                                      False)
            c = glyph.nextForHints(c)
        if mode == SHORT:
            oldmasks[:] = masks
            masks = oldmasks
        self.bridgeMasks(glyph, masks, None, usedmasks, glyph.last())
        if False in usedmasks[0] or False in usedmasks[1]:
            self.delUnused(stems, usedmasks)
            self.delUnused(glyph.startmasks, usedmasks)
            for c in glyph.cntr:
                self.delUnused(c, usedmasks)
            foundPEMask = False
            for c in glyph:
                if c.masks:
                    foundPEMask = True
                    self.delUnused(c.masks, usedmasks)
            if not foundPEMask:
                glyph.startmasks = None
                glyph.is_hm = False

    def buildCounterMasks(self, glyph):
        """
        For glyph dimensions that are counter-hinted, make a cntrmask
        with all Trues in that dimension (because only h/vstem3 style counter
        hints are supported)
        """
        assert not glyph.hhs.keepHints or not glyph.vhs.keepHints
        if not glyph.hhs.keepHints:
            hcmsk = [glyph.hhs.counterHinted] * len(glyph.hhs.stems)
        if not glyph.vhs.keepHints:
            vcmsk = [glyph.vhs.counterHinted] * len(glyph.vhs.stems)
        if glyph.hhs.keepHints or glyph.vhs.keepHints and glyph.cntr:
            cntr = []
            for cm in glyph.cntr:
                hm = cm[0] if glyph.hhs.keepHints else hcmsk
                vm = cm[1] if glyph.vhs.keepHints else vcmsk
                cntr.append([hm, vm])
        elif glyph.hhs.counterHinted or glyph.vhs.counterHinted:
            cntr = [[hcmsk, vcmsk]]
        else:
            cntr = []
        glyph.cntr = cntr

    def joinMasks(self, m, cm, log):
        """
        Try to add the stems in cm to m, or start a new mask if there are
        conflicts.
        """
        conflict = False
        nm = [None, None]
        for hv in range(2):
            hs = self.vHinter.hs if hv == 1 else self.hHinter.hs
            l = len(m[hv])
#            if hs.counterHinted:
#                nm[hv] = [True] * l
#                continue
            c = cm[hv]
            n = nm[hv] = copy(m[hv])
            if hs.keepHints:
                conflict = True in c
                continue
            assert len(c) == l
            for i in range(l):
                iconflict = ireplaced = False
                if not c[i] or n[i]:
                    continue
                # look for conflicts
                for j in range(l):
                    if not hs.hasConflicts:
                        break
                    if j == i:
                        continue
                    if n[j] and hs.stemConflicts[i][j]:
                        # See if we can do a ghost stem swap
                        if hs.ghostCompat[i]:
                            for k in range(l):
                                if not n[k] or not hs.ghostCompat[i][k]:
                                    continue
                                else:
                                    ireplaced = True
                                    break
                        if not ireplaced:
                            iconflict = True
                    if ireplaced:
                        break
                if not iconflict and not ireplaced:
                    n[i] = True
                elif iconflict:
                    conflict = True
                    # XXX log conflict here if log is true
        return nm, conflict

    def bridgeMasks(self, glyph, o, n, used, pe):
        """
        For switching hintmasks: Clean up o by adding compatible stems from
        mainMask and add stems from o to n when they are close to pe

        used contains a running map of which stems have ever been included
        in a hintmask
        """
        stems = [glyph.hstems, glyph.vstems]
        po = pe.e if pe.isLine() else pe.cs
        carryMask = [[False] * len(o[0]), [False] * len(o[1])]
        for hv in range(2):
            # Carry a previous hint forward if it is compatible and close
            # to the current pathElement
            nloc = pe.e.x if hv == 1 else pe.e.y
            for i in range(len(o[hv])):
                if not o[hv][i]:
                    continue
                dlimit = max(self.hHinter.BandMargin / 2, self.MaxHalfMargin)
                if stems[hv][i].distance(nloc) < dlimit:
                    carryMask[hv][i] = True
            # If there are no hints in o in this dimension add the closest to
            # the current path element
            if True not in o[hv]:
                oloc = po.x if hv == 1 else po.y
                try:
                    _, ms = min(((stems[hv][i].distance(oloc), i)
                                 for i in range(len(o[hv]))))
                    o[hv][ms] = True
                except ValueError:
                    pass
        if self.mergeMain(glyph):
            no, _ = self.joinMasks(o, [glyph.hhs.mainMask, glyph.vhs.mainMask],
                                   False)
            o[:] = no
        for hv in range(2):
            used[hv] = [ov or uv for ov, uv in zip(o[hv], used[hv])]
        if n is not None:
            nm, _ = self.joinMasks(n, carryMask, False)
            n[:] = nm

    def mergeMain(self, glyph):
        return len(glyph.subpaths) <= 5

    def delUnused(self, l, ml):
        """If ml[d][i] is False delete that entry from ml[d]"""
        for hv in range(2):
            l[hv][:] = [l[hv][i] for i in range(len(l[hv])) if ml[hv][i]]

    def listHintInfo(self, glyph):
        """
        Output debug messages about which stems are associated with which
        segments
        """
        for pe in glyph:
            hList = self.getSegments(glyph, pe, False)
            vList = self.getSegments(glyph, pe, True)
            if hList or vList:
                self.hHinter.debug("hintlist x %g y %g" % (pe.e.x, pe.e.y))
                for seg in hList:
                    seg.hintval.show(False, "listhint", self.hHinter)
                for seg in vList:
                    seg.hintval.show(True, "listhint", self.vHinter)

    def remFlares(self, glyph):
        """
        When two paths are witin MaxFlare and connected by a path that
        also stays within MaxFlare, and both desire different stems,
        (sometimes) remove the lower-valued stem of the pair
        """
        for c in glyph:
            csl = self.getSegments(glyph, c)
            if not csl:
                continue
            n = glyph.nextInSubpath(c)
            cDone = False
            while c != n and not cDone:
                nsl = self.getSegments(glyph, n)
                if not nsl:
                    if not self.getSegments(glyph, n, True):
                        break
                    else:
                        n = glyph.nextInSubpath(n)
                        continue
                csi = 0
                while csi < len(csl):
                    cseg = csl[csi]
                    nsi = 0
                    while nsi < len(nsl):
                        nseg = nsl[nsi]
                        if cseg is not None and nseg is not None:
                            diff = abs(cseg.loc - nseg.loc)
                            if diff > self.hHinter.MaxFlare:
                                cDone = True
                                nsi += 1
                                continue
                            if not self.isFlare(cseg.loc, glyph, c, n):
                                cDone = True
                                nsi += 1
                                continue
                            chv, nhv = cseg.hintval, nseg.hintval
                            if (diff != 0 and
                                self.isUSeg(cseg.loc, chv.uloc, chv.lloc) ==
                                    self.isUSeg(nseg.loc, nhv.uloc, nhv.lloc)):
                                if (chv.compVal(self.hHinter.SpcBonus) >
                                        nhv.compVal(self.hHinter.SpcBonus)):
                                    if (nhv.spc == 0 and
                                            nhv.val < self.FlareValueLimit):
                                        self.reportRemFlare(n, c, "n")
                                        del nsl[nsi]
                                        nsi -= 1
                                else:
                                    if (chv.spc == 0 and
                                            chv.val < self.FlareValueLimit):
                                        self.reportRemFlare(c, n, "c")
                                        del csl[csi]
                                        csi -= 1
                                        break
                        nsi += 1
                    csi += 1
                n = glyph.nextInSubpath(n)

    def isFlare(self, loc, glyph, c, n):
        """Utility function for remFlares"""
        while c is not n:
            v = c.e.x if self.doV else c.e.y
            if abs(v - loc) > self.hHinter.MaxFlare:
                return False
            c = glyph.nextInSubpath(c)
        return True

    def isUSeg(self, loc, uloc, lloc):
        return abs(uloc - loc) <= abs(lloc - loc)

    def reportRemFlare(self, pe, pe2, desc):
        self.hHinter.debug("Removed %s flare at %g %g by %g %g : %s" %
                           ("vertical" if self.doV else "horizontal",
                            pe.e.x, pe.e.y, pe2.e.x, pe2.e.y, desc))
