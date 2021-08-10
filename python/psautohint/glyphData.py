# Copyright 2021 Adobe. All rights reserved.

"""
Internal state of T1 (cubic Bezier) glyph with hints
"""

import numbers
import threading
import operator
from copy import deepcopy
from builtins import tuple as _tuple
from fontTools.misc.bezierTools import solveQuadratic, calcCubicParameters

import logging
log = logging.getLogger(__name__)


def feq(a, b):
    return abs(a - b) < 1.52e-5


def fne(a, b):
    return abs(a - b) >= 1.52e-5


class pt(tuple):
    __slots__ = ()
    tl = threading.local()
    tl.align = None

    @classmethod
    def setAlign(_cls, vertical=False):
        if vertical:
            _cls.tl.align = 2
        else:
            _cls.tl.align = 1

    @classmethod
    def clearAlign(_cls):
        _cls.tl.align = None

    def __new__(_cls, x=0, y=0, round_coords=False):
        if isinstance(x, tuple):
            y = x[1]
            x = x[0]
        if round_coords:
            x = round(x)
            y = round(y)
        return _tuple.__new__(_cls, (x, y))

    @property
    def x(self):
        return self[0]

    @property
    def y(self):
        return self[1]

    @property
    def a(self):
        if self.tl.align == 1:
            return self[0]
        elif self.tl.align == 2:
            return self[1]
        else:
            raise RuntimeError("glyphData.pt property a used without " +
                               "setting align")

    @property
    def o(self):
        if self.tl.align == 1:
            return self[1]
        elif self.tl.align == 2:
            return self[0]
        else:
            raise RuntimeError("glyphData.pt property o used without " +
                               "setting align")

    def ao(self):
        if self.tl.align == 1:
            return (self.x, self.y)
        elif self.tl.align == 2:
            return (self.y, self.x)
        else:
            raise RuntimeError("glyphData.pt method ao used without " +
                               "setting align")

    # for two pts
    def __add__(self, other):
        if not isinstance(other, pt):
            raise TypeError('Both arguments to pt.__add__ must be pts')
        return pt(self[0] + other[0], self[1] + other[1])

    def __sub__(self, other):
        if not isinstance(other, pt):
            raise TypeError('Both arguments to pt.__sub__ must be pts')
        return pt(self[0] - other[0], self[1] - other[1])

    def avg(self, other):
        if not isinstance(other, pt):
            raise TypeError('Both arguments to pt.avg must be pts')
        return pt((self[0] + other[0]) * 0.5, (self[1] + other[1]) * 0.5)

    def dot(self, other):
        if not isinstance(other, pt):
            raise TypeError('Both arguments to pt.dot must be pts')
        return self[0] * other[0] + self[1] * other[1]

    def distsq(self, other):
        if not isinstance(other, pt):
            raise TypeError('Both arguments to pt.distsq must be pts')
        dx = self[0] - other[0]
        dy = self[1] - other[1]
        return dx * dx + dy * dy

    def a_dist(self, other):
        return abs((self - other).a)

    def o_dist(self, other):
        return abs((self - other).o)

    def normsq(self):
        return self[0] * self[0] + self[1] * self[1]

    def abs(self):
        return pt(abs(self[0]), abs(self[1]))

    def round(self):
        return pt(round(self[0]), round(self[1]))

    # for pt and number
    def __mul__(self, other):
        if not isinstance(other, numbers.Number):
            raise TypeError('One argument to pt.__mul__ must be a scalar ' +
                            'number')
        return pt(self[0] * other, self[1] * other)

    def __rmul__(self, other):
        if not isinstance(other, numbers.Number):
            raise TypeError('One argument to pt.__rmul__ must be a scalar ' +
                            'number')
        return pt(self[0] * other, self[1] * other)

    def __eq__(self, other):
        return feq(self[0], other[0]) and feq(self[1], other[1])


class stem(tuple):
    BandMargin = 30
    __slots__ = ()

    def __new__(_cls, lb=0, rt=0):
        return _tuple.__new__(_cls, (lb, rt))

    @property
    def lb(self):
        return self[0]

    @property
    def rt(self):
        return self[1]

    def __str__(self):
        isgh = self.isGhost()
        isbad = self.isBad()
        if not isgh and not isbad:
            return "({0} -> {1})".format(self.lb, self.rt)
        elif isgh == "high":
            return "({0} (high ghost))".format(self.rt)
        elif isgh == "low":
            return "({0} (low ghost))".format(self.rt)
        else:
            return "(bad stem values {0}, {1})".format(self.lb, self.rt)

#    def UFOstr(self, isV):
#        return "%sstem %.1g %.1g" % ( 'v' if isV else 'h', self.lb, se

    __repr__ = __str__

    def isGhost(self):
        width = self.rt - self.lb
        if width == -20:
            return "high"
        if width == -21:
            return "low"
        return False

    def isBad(self):
        return not self.isGhost() and self.rt - self.lb < 0

    def relVals(self, last=None):
        if last:
            l = last.rt
        else:
            l = 0
        return (self.lb - l, self.rt - self.lb)

    def overlaps(self, other):
        lloc, uloc = self.lb, self.rt
        sig = self.isGhost()
        # c.f. page 22 of Adobe TN #5177 "The Type 2 Charstring Format"
        if sig == 'high':
            uloc = lloc
        elif sig == 'low':
            lloc = uloc
        olloc, ouloc = other.lb, other.rt
        oig = other.isGhost()
        if oig == 'high':
            ouloc = olloc
        elif oig == 'low':
            olloc = ouloc
        uloc += stem.BandMargin
        lloc -= stem.BandMargin
        return olloc <= uloc and ouloc >= lloc

    def distance(self, loc):
        lloc, uloc = self.lb, self.rt
        sig = self.isGhost()
        # c.f. page 22 of Adobe TN #5177 "The Type 2 Charstring Format"
        if sig == 'high':
            uloc = lloc
        elif sig == 'low':
            lloc = uloc
        if loc >= lloc and loc <= uloc:
            return 0
        return min(abs(lloc - loc), abs(uloc - loc))


class boundsState:
    def mergePt(self, b, p, t, doExt=True):
        for i, cmp_o in enumerate([operator.lt, operator.gt]):
            for j in range(2):
                if cmp_o(p[j], b[i][j]):
                    b[i][j] = p[j]
                    if doExt:
                        self.extpts[i][j] = p
                        self.tmap[i][j] = t

    def linearBounds(self, c):
        self.tmap = [[0, 0], [0, 0]]
        self.extpts = [[c.s, c.s], [c.s, c.s]]
        lb = [[*c.s], [*c.s]]
        self.mergePt(lb, c.e, 1)
        return lb

    def calcCurveBounds(self, pe):
        curveb = deepcopy(self.lb)
        a, b, c, d = pe.cubicParameters()
        roots = []
        for dim in range(2):
            roots += [t for t in solveQuadratic(a[dim] * 3, b[dim] * 2, c[dim])
                      if 0 <= t < 1]

        for t in roots:
            p = pt(a[0] * t * t * t + b[0] * t * t + c[0] * t + d[0],
                   a[1] * t * t * t + b[1] * t * t + c[1] * t + d[1])
            self.mergePt(curveb, p, t)

        self.b = curveb

    def __init__(self, c):
        self.lb = self.linearBounds(c)
        if c.is_line:
            self.b = self.lb
        else:
            cpb = deepcopy(self.lb)
            self.mergePt(cpb, c.cs, .25, doExt=False)  # t value isn't used
            self.mergePt(cpb, c.ce, .75, doExt=False)  # t value isn't used
            if self.lb != cpb:
                self.calcCurveBounds(c)
            else:
                self.b = self.lb

    def farthestExtreme(self, doY=False):
        idx = 1 if doY else 0
        d = [abs(self.lb[i][idx] - self.b[i][idx]) for i in range(2)]
        if d[0] > d[1]:
            return d[0], self.extpts[0][idx], self.tmap[0][idx], False
        elif fne(d[1], 0):
            return d[1], self.extpts[1][idx], self.tmap[1][idx], True
        else:
            return 0, None, None, None


class pathBoundsState:
    def __init__(self, pe):
        self.b = pe.getBounds().b
        self.extpes = [[pe, pe], [pe, pe]]

    def merge(self, other):
        for i, cmp_o in enumerate([operator.lt, operator.gt]):
            for j in range(2):
                if cmp_o(other.b[i][j], self.b[i][j]):
                    self.b[i][j] = other.b[i][j]
                    self.extpes[i][j] = other.extpes[i][j]

    def within(self, other):
        return (self.b[0][0] >= other.b[0][0] and
                self.b[0][1] >= other.b[0][1] and
                self.b[1][0] <= other.b[1][0] and
                self.b[1][1] <= other.b[1][1])


class pathElement:
    def __init__(self, *args, is_close=False, masks=None, flex=False,
                 position=None):
        self.is_line = False
        self.is_close = is_close
        for p in args:
            if not isinstance(p, pt):
                raise TypeError('Positional arguments to ' +
                                'pathElement.__init__ must be pt objects')
        if len(args) == 2:
            self.is_line = True
            self.s = args[0]
            self.e = args[1]
        elif len(args) == 4:
            if is_close:
                raise TypeError('is_close must be False for path curves')
            self.s = args[0]
            self.cs = args[1]
            self.ce = args[2]
            self.e = args[3]
        else:
            raise TypeError('Wrong number of positional arguments to ' +
                            'pathElement.__init__')
        self.masks = masks
        self.flex = flex
        self.bounds = None
        self.position = position

    def getBounds(self):
        if self.bounds:
            return self.bounds
        self.bounds = boundsState(self)
        return self.bounds

    def isLine(self):
        return self.is_line

    def isClose(self):
        return self.is_close

    def isNull(self):
        return self.is_close and self.s == self.e

    def isStart(self):
        return self.position[1] == 0

    def isTiny(self):
        d = (self.e - self.s).abs()
        return d.x < 2 and d.y < 2

    def isShort(self):
        d = (self.e - self.s).abs()
        mx, mn = sorted(tuple(d))
        return mx + mn * .336 < 6  # head.c IsShort

    def tup(self):
        if self.is_line:
            return (self.s, self.s, self.e, self.e)
        return (self.s, self.cs, self.ce, self.e)

    def start(self):
        return self.s

    def end(self):
        return self.e

    def clearHints(self, doVert=False):
        if doVert and self.masks is not None:
            self.masks = [self.masks[0], None] if self.masks[0] else None
        elif not doVert and self.masks is not None:
            self.masks = [None, self.masks[1]] if self.masks[1] else None

    def cubicParameters(self):
        return calcCubicParameters(self.s, self.cs, self.ce, self.e)

    def slopePoint(self, t):
        if t == 1:
            if self.is_line:
                return self.s
            if self.e == self.ce:
                return self.cs
            return self.ce
        else:
            if self.is_line:
                return self.e
            if self.s == self.cs:
                return self.ce
            return self.cs

    @staticmethod
    def stemBytes(masks):
        t = masks[0] + masks[1]
        lenb = len(t)
        lenB = (lenb + 7) // 8
        t += [False for i in range(lenB * 8 - lenb)]
        return int(''.join(('1' if i else '0' for i in t)),
                   2).to_bytes(lenB, byteorder='big')

    def relVals(self):
        if self.is_line:
            r = self.e - self.s
            return [*r]
        else:
            r1 = self.cs - self.s
            r2 = self.ce - self.cs
            r3 = self.e - self.ce
            return [*r1, *r2, *r3]

    def T2(self, is_start=None):
        prog = []

        if is_start:
            rmt = self.s - is_start
            prog.extend([*rmt, 'rmoveto'])

        if self.masks and self.flex == 2:
            print("XXX hintmask added in middle of flex")
        if self.masks:
            prog.extend(['hintmask', pathElement.stemBytes(self.masks)])

        rv = self.relVals()

        after = self.e

        if self.is_close:
            after = self.s
        elif self.flex == 1:
            prog.extend(rv)
        elif self.flex == 2:
            prog.extend(rv)
            prog.extend([50, 'flex'])
        elif not self.is_line:
            prog.extend(rv)
            prog.append('rrcurveto')
        else:
            prog.extend(rv)
            prog.append('rlineto')

        return prog, after


class glyphData:
    def __init__(self, round_coords, name=''):
        self.round_coords = round_coords

        self.subpaths = []
        self.hstems = []
        self.vstems = []
        self.startmasks = None
        self.cntr = []
        self.name = name
        self.wdth = None

        self.is_hm = None
        self.flex_count = 0
        self.lastcp = None

        self.nextmasks = None
        self.current_end = pt()
        self.nextflex = None
        self.changed = False
        self.pathEdited = False
        self.boundsMap = {}

        self.hhs = self.vhs = None

    # pen methods:

    def moveTo(self, ptup):
        self.lastcp = None
        self.subpaths.append([])
        self.current_end = pt(ptup, round_coords=self.round_coords)

    def lineTo(self, ptup):
        self.lastcp = None
        newpt = pt(ptup, round_coords=self.round_coords)
        self.subpaths[-1].append(pathElement(self.current_end, newpt,
                                             masks=self.getStemMasks(),
                                             flex=self.checkFlex(False),
                                             position=self.getPosition()))
        self.current_end = newpt

    def curveTo(self, ptup1, ptup2, ptup3):
        self.lastcp = None
        newpt1 = pt(ptup1, round_coords=self.round_coords)
        newpt2 = pt(ptup2, round_coords=self.round_coords)
        newpt3 = pt(ptup3, round_coords=self.round_coords)
        self.subpaths[-1].append(pathElement(self.current_end, newpt1, newpt2,
                                             newpt3, masks=self.getStemMasks(),
                                             flex=self.checkFlex(True),
                                             position=self.getPosition()))
        self.current_end = newpt3

    # closePath is a courtesy of the caller, not an instruction, so
    # we rely on its semantics here
    def closePath(self):
        if len(self.subpaths[-1]) == 0:  # No content after moveTo
            t = self.current_end
        else:
            t = self.subpaths[-1][0].start()
        self.subpaths[-1].append(pathElement(self.current_end, t,
                                             masks=self.getStemMasks(),
                                             flex=self.checkFlex(False),
                                             is_close=True,
                                             position=self.getPosition()))
        self.lastcp = self.subpaths[-1][-1]
        self.current_end = t

    def getPosition(self):
        return (len(self.subpaths) - 1, len(self.subpaths[-1]))

    # "hintpen" methods:
    def nextIsFlex(self):
        self.flex_count += 1
        self.nextflex = 1

    def hStem(self, data, is_hm):
        if self.is_hm is not None and self.is_hm != is_hm:
            print("XXX mismatched hm")
        self.is_hm = is_hm
        self.hstems = self.toStems(data)

    def vStem(self, data, is_hm):
        if is_hm is not None:
            if self.is_hm is not None and self.is_hm != is_hm:
                print("XXX vstem mismatched hm")
            self.is_hm = is_hm
        self.vstems = self.toStems(data)

    def hintmask(self, hhints, vhints):
        if not self.is_hm:
            print("XXX hintmask without hstemhm")
            self.is_hm = True
        if len(self.subpaths) == 0:
            self.startmasks = [hhints, vhints]
        else:
            if not self.startmasks and not self.cntr:
                print("XXX no initial hintmask in %s" % self.name)
            # In the glyphdata format the end of a path is implicit in the
            # charstring but explicit in the subpath, while a moveto is
            # explicit in the charstring and implicit in the subpath. So
            # we record mid-stream hintmasks prior to movetos in the
            # prior "is_close" line. When writing out these will wind up
            # in the same place in the stream.
            if self.lastcp:
                self.lastcp.masks = [hhints, vhints]
            else:
                self.nextmasks = [hhints, vhints]

    def cntrmask(self, hhints, vhints):
        self.cntr.append([hhints, vhints])

    def finish(self):
        pass

    def setWidth(self, width):
        self.wdth = width

    def getWidth(self):
        return self.wdth

    # status
    def isEmpty(self):
        return not len(self.subpaths) > 0

    def hasFlex(self):
        return self.flex_count > 0

    def lastPathIsClosed(self):
        if len(self.subpaths) == 0 or len(self.subpaths[-1]) == 0:
            return False
        return self.subpaths[-1][-1].isClose()

    def hasHints(self, doVert=False, both=False):
        if both:
            return len(self.vstems) > 0 and len(self.hstems) > 0
        elif doVert:
            return len(self.vstems) > 0
        else:
            return len(self.hstems) > 0

    def syncPositions(self):
        if self.pathEdited:
            for sp in range(len(self.subpaths)):
                for ofst in range(len(self.subpaths[sp])):
                    self.subpaths[sp][ofst].position = (sp, ofst)

    def setPathEdited(self):
        self.pathEdited = True
        self.boundsMap = {}

    def getBounds(self, subpath=None):
        b = self.boundsMap.get(subpath, None)
        if b is not None:
            return b
        if subpath is None:
            b = deepcopy(self.getBounds(0))
            for i in range(1, len(self.subpaths)):
                b.merge(self.getBounds(i))
            self.boundsMap[subpath] = b
            return b
        else:
            b = pathBoundsState(self.subpaths[subpath][0])
            for c in self.subpaths[subpath][1:]:
                b.merge(pathBoundsState(c))
            self.boundsMap[subpath] = b
            return b

    def T2(self, version=1):
        prog = []

        if self.hstems:
            prog.extend(self.fromStems(self.hstems))
            prog.append('hstemhm' if self.is_hm else 'hstem')
        if self.vstems:
            prog.extend(self.fromStems(self.vstems))
            if not self.is_hm:
                prog.append('vstem')
            elif len(self.cntr) == 0 and self.startmasks is None:
                prog.append('vstemhm')

        for c in self.cntr:
            prog.extend(['cntrmask', pathElement.stemBytes(c)])
        if self.startmasks:
            prog.extend(['hintmask', pathElement.stemBytes(self.startmasks)])

        curpt = pt(0, 0)

        for c in self:
            c_t2, curpt = c.T2(is_start=curpt if c.isStart() else None)
            prog.extend(c_t2)

        if version == 1:
            prog.append('endchar')
        return prog

#    def drawPoints(self, pen):

    # XXX deal with or avoid reordering when preserving any hints
    def reorder(self, neworder, lg):
        lg.debug("Reordering subpaths: %r" % neworder)
        spl = self.subpaths
        assert len(neworder) == len(spl)
        self.subpaths = [spl[i] for i in neworder]
        self.setPathEdited()

    def first(self):
        if self.subpaths and self.subpaths[0]:
            return self.subpaths[0][0]
        return None

    def last(self):
        if self.subpaths and self.subpaths[-1]:
            return self.subpaths[-1][-1]
        return None

    def next(self, c):
        if c is None:
            return None
        if c is self:
            if self.subpaths and self.subpaths[0]:
                return self.subpaths[0][0]
            return None
        self.syncPositions()
        subpath, offset = c.position
        offset += 1
        if offset < len(self.subpaths[subpath]):
            return self.subpaths[subpath][offset]
        subpath += 1
        offset = 0
        if subpath < len(self.subpaths):
            return self.subpaths[subpath][offset]
        return None

    def prev(self, c):
        if c is self or c is None:
            return None
        self.syncPositions()
        subpath, offset = c.position
        if offset > 0:
            return self.subpaths[subpath][offset - 1]
        subpath -= 1
        if subpath >= 0:
            return self.subpaths[subpath][-1]
        return None

    def inSubpath(self, c, i, skipTiny, closeWrapOK):
        if c is None or (c is self and i == -1):
            return None
        if c is self:
            return self.next(c)
        self.syncPositions()
        subpath, offset = c.position
        sp = self.subpaths[subpath]
        l = len(sp)
        o = (offset + i) % l
        if o == offset:
            return None
        while skipTiny and sp[o].isTiny():
            o = (o + i) % l
            if o == offset:
                return None
        if (not closeWrapOK and
                ((i > 0 and o < offset) or (i < 0 and o > offset))):
            return None
        return sp[o]

    def nextInSubpath(self, c, skipTiny=False, closeWrapOK=True):
        return self.inSubpath(c, 1, skipTiny, closeWrapOK)

    def prevInSubpath(self, c, skipTiny=False, closeWrapOK=True):
        return self.inSubpath(c, -1, skipTiny, closeWrapOK)

    def nextSlopePoint(self, c):
        n = self.nextInSubpath(c, skipTiny=True)
        return None if n is None else n.slopePoint(0)

    def prevSlopePoint(self, c):
        p = self.prevInSubpath(c, skipTiny=True)
        return None if p is None else p.slopePoint(1)

    class glyphiter:
        __slots__ = ('gd', 'pos')

        def __init__(self, gd):
            self.gd = self.pos = gd

        def __next__(self):
            self.pos = self.gd.next(self.pos)
            if self.pos is None:
                raise StopIteration
            return self.pos

    def __iter__(self):
        return self.glyphiter(self)

    # utility

    def getStemMasks(self):
        s = self.nextmasks
        self.nextmasks = None
        return s

    def checkFlex(self, is_curve):
        if self.nextflex is None:
            return None
        elif not is_curve:
            # XXX warn about flex on non-curve
            self.nextflex = None
            return None
        elif self.nextflex == 1:
            self.nextflex = 2
            return 1
        elif self.nextflex == 2:
            self.nextflex = None
            return 2
        else:
            print("XXX bad flex value: internal error")
            self.nextflex = None
            return None

    def toStems(self, data):
        high = 0
        sl = []
        for i in range(len(data) // 2):
            low = high + data[i * 2]
            high = low + data[i * 2 + 1]
            sl.append(stem(low, high))
        return sl

    def fromStems(self, stems):
        l = None
        data = []
        for s in stems:
            data.extend(s.relVals(last=l))
            l = s
        return data

    def clearFlex(self):
        if self.flex_count == 0:
            return
        for c in self:
            c.flex = None
        self.flex_count = 0

    def clearHints(self, doVert=False):
        if doVert:
            self.vstems = []
            if self.startmasks and self.startmasks[0]:
                self.startmasks = [self.startmasks[0], None]
            else:
                self.startmasks = None
        else:
            self.hstems = []
            if self.startmasks and self.startmasks[1]:
                self.startmasks = [None, self.startmasks[1]]
            else:
                self.startmasks = None
        cntr = []
        for cn in self.cntr:
            if doVert and cn[0]:
                cntr.append((cn[0], None))
            elif not doVert and cn[1]:
                cntr.append((None, cn[1]))
        self.cntr = cntr
        for c in self:
            c.clearHints(doVert=doVert)
