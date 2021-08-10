# Copyright 2021 Adobe. All rights reserved.

"""
Storage for intermediate state built by the hinter object
"""

import weakref
import logging
import bisect
from enum import Enum

from .glyphData import feq

log = logging.getLogger(__name__)


class hintSegment:
    class sType(Enum):
        LINE = 0,
        BEND = 1,
        CURVE = 2
        GHOST = 3

    def __init__(self, aloc, oMin, oMax, pe, typ, bonus, isV, desc):
        self.loc = aloc
        self.min = oMin
        self.max = oMax
        self.bonus = bonus
        self.type = typ
        self.isV = isV
        self.desc = desc
        if pe:
            self.pe = weakref.ref(pe)
        else:
            self.pe = None
        self.hintval = None
        self.replacedBy = None
        self.deleted = False

    def __eq__(self, other):
        return feq(self.loc, other.loc)

    def __lt__(self, other):
        return self.loc < other.loc

    def current(self, orig=None):
        if self.replacedBy is None:
            return self
        if self is orig:
            # XXX throw exception about cycle
            return self
        if orig is None:
            orig = self
        return self.replacedBy.current(orig=self)

    def show(self, label, lg=log):
        if self.isV:
            pp = (label, 'v', self.loc, self.min, self.loc, self.max,
                  self.desc)
        else:
            pp = (label, 'h', self.min, self.loc, self.max, self.loc,
                  self.desc)
        lg.debug("%s %sseg %g %g to %g %g %s" % pp)


class stemValue:
    def __init__(self, lloc, uloc, val, spc, lseg, useg, isGhost=False):
        assert lloc <= uloc
        self.val = val
        self.spc = spc
        self.lloc = lloc
        self.uloc = uloc
        self.isGhost = isGhost
        self.pruned = False
        self.merge = False
        self.lseg = lseg
        self.useg = useg
        self.best = None
        self.initialVal = val
        self.idx = None

    def __eq__(self, other):
        slloc, suloc = self.ghosted()
        olloc, ouloc = other.ghosted()
        return slloc == olloc and suloc == ouloc

    def __lt__(self, other):
        slloc, suloc = self.ghosted()
        olloc, ouloc = other.ghosted()
        return (slloc < olloc or (slloc == olloc and suloc < ouloc))

    # c.f. page 22 of Adobe TN #5177 "The Type 2 Charstring Format"
    def ghosted(self):
        lloc, uloc = self.lloc, self.uloc
        if self.isGhost:
            if self.lseg.type == hintSegment.sType.GHOST:
                lloc = uloc
                uloc = lloc - 20
            else:
                uloc = lloc
                lloc = uloc + 21
        return (lloc, uloc)

    def compVal(self, spcFactor=1, ghostFactor=1):
        v = self.val
        if self.isGhost:
            v *= ghostFactor
        if self.spc > 0:
            v *= spcFactor
        return (v, self.initialVal)

    def show(self, isV, typ, lg=log):
        tags = ('v', 'l', 'r', 'b', 't') if isV else ('h', 'b', 't', 'l', 'r')
        start = "%s %sval %s %g %s %g v %g s %g %s" % (typ, tags[0], tags[1],
                                                       self.lloc, tags[2],
                                                       self.uloc, self.val,
                                                       self.spc,
                                                       'G' if self.isGhost
                                                       else '')
        if self.lseg is None:
            lg.debug(start)
            return
        lg.debug("%s %s1 %g %s1 %g  %s2 %g %s2 %g" %
                 (start, tags[3], self.lseg.min, tags[4], self.lseg.max,
                  tags[3], self.useg.min, tags[4], self.useg.max))


class pathElementHintState:
    def __init__(self):
        self.segments = []
        self.mask = []

    def cleanup(self):
        self.segments[:] = [s.current() for s in self.segments
                            if not s.deleted]

    def pruneHintSegs(self):
        self.segments[:] = [s for s in self.segments if s.hintval is not None]


class glyphHintState:
    def __init__(self):
        self.peStates = {}
        self.increasingSegs = []
        self.decreasingSegs = []
        self.stemValues = []
        self.mainValues = None
        self.rejectValues = None
        self.counterHinted = False
        self.stems = None  # in sorted glyphData format
        self.keepHints = None
        self.hasConflicts = None
        self.stemConflicts = None
        self.ghostCompat = None
        self.mainMask = None

    def getPEState(self, pe, make=False):
        s = self.peStates.get(pe, None)
        if s:
            return s
        if make:
            s = self.peStates[pe] = pathElementHintState()
            return s
        else:
            return None

    def addSegment(self, fr, to, loc, pe1, pe2, typ, bonus, isV, desc):
        if fr > to:
            mn, mx = to, fr
            lst = self.decreasingSegs
        else:
            mn, mx = fr, to
            lst = self.increasingSegs
        s = hintSegment(loc, mn, mx, pe2 if pe2 else pe1, typ, bonus, isV,
                        desc)
        assert not pe1 or not pe2 or pe1 is not pe2
        if pe1:
            self.getPEState(pe1, True).segments.append(s)
        if pe2:
            self.getPEState(pe2, True).segments.append(s)

        bisect.insort(lst, s)

    def compactList(self, l):
        i = 0
        while i < len(l):
            j = i + 1
            while j < len(l) and feq(l[j].loc, l[i].loc):
                if l[i].max >= l[j].min and l[i].min <= l[j].max:
                    if abs(l[i].max - l[i].min) > abs(l[j].max - l[j].min):
                        l[i].min = min(l[i].min, l[j].min)
                        l[i].max = max(l[i].max, l[j].max)
                        l[i].bonus = max(l[i].bonus, l[j].bonus)
                        l[j].replacedBy = l[i]
                        del l[j]
                        continue
                    else:
                        l[j].min = min(l[i].min, l[j].min)
                        l[j].max = max(l[i].max, l[j].max)
                        l[j].bonus = max(l[i].bonus, l[j].bonus)
                        l[i].replacedBy = l[j]
                        del l[i]
                        i -= 1
                        break
                j += 1
            i += 1

    def compactLists(self):
        self.compactList(self.decreasingSegs)
        self.compactList(self.increasingSegs)

    def remExtraBends(self, lg=log):
        li = self.increasingSegs
        ld = self.decreasingSegs
        i = 0
        while i < len(li):
            d = 0
            while d < len(ld):
                hsi = li[i]
                hsd = ld[d]
                if hsd.loc > hsi.loc:
                    break
                if (hsd.loc == hsi.loc and hsd.min < hsi.max and
                        hsd.max > hsi.min):
                    if (hsi.type == hintSegment.sType.BEND and
                            hsd.type != hintSegment.sType.BEND and
                            hsd.type != hintSegment.sType.GHOST and
                            (hsd.max - hsd.min) > (hsi.max - hsi.min) * 3):
                        li[i].deleted = True
                        del li[i]
                        i -= 1
                        lg.debug("rem seg loc %g from %g to %g" %
                                 (hsi.loc, hsi.min, hsi.max))
                        break
                    elif (hsd.type == hintSegment.sType.BEND and
                          hsi.type != hintSegment.sType.BEND and
                          hsi.type != hintSegment.sType.GHOST and
                          (hsi.max - hsi.min) > (hsd.max - hsd.min) * 3):
                        ld[d].deleted = True
                        del ld[d]
                        d -= 1
                        lg.debug("rem seg loc %g from %g to %g" %
                                 (hsd.loc, hsd.min, hsi.max))
                d += 1
            i += 1

    def cleanup(self):
        for pes in self.peStates.values():
            pes.cleanup()

    def pruneHintSegs(self):
        for pes in self.peStates.values():
            pes.pruneHintSegs()


class links:
    def __init__(self, glyph):
        l = len(glyph.subpaths)
        if l < 4 or l > 100:
            self.cnt = 0
            return
        self.cnt = l
        self.links = [[0] * l for i in range(l)]

    def logLinks(self, lg=log):
        if self.cnt == 0:
            return
        lg.debug("Links")
        lg.debug(' '.join((str(i).rjust(2) for i in range(self.cnt))))
        for j in range(self.cnt):
            lg.debug(' '.join((('Y' if self.links[j][i] else ' ').rjust(2)
                               for i in range(self.cnt))))

    def logShort(self, shrt, lab, lg):
        lg.debug(lab)
        lg.debug(' '.join((str(i).rjust(2) for i in range(self.cnt))))
        lg.debug(' '.join(((str(shrt[i]) if shrt[i] else ' ').rjust(2)
                           for i in range(self.cnt))))

    def mark(self, hntr):
        if self.cnt == 0:
            return
        for sv in hntr.hs.stemValues:
            if not sv.lseg or not sv.useg or not sv.lseg.pe or not sv.useg.pe:
                continue
            lsubp, usubp = sv.lseg.pe().position[0], sv.useg.pe().position[0]
            if lsubp == usubp:
                continue
            sv.show(hntr.isV(), "mark", hntr)
            hntr.debug(" : %d <=> %d" % (lsubp, usubp))
            self.links[lsubp][usubp] = 1
            self.links[usubp][lsubp] = 1

    def moveIdx(self, suborder, subidxs, outlinks, idx, lg):
        subidxs.remove(idx)
        suborder.append(idx)
        for i in range(len(outlinks)):
            outlinks[i] += self.links[idx][i]
        self.logShort(outlinks, "Outlinks", lg)

    def shuffle(self, lg=log):
        if self.cnt == 0:
            return None
        sumlinks = [sum(l) for l in zip(*self.links)]
        outlinks = [0] * self.cnt
        self.logLinks(lg)
        self.logShort(sumlinks, "Sumlinks", lg)
        subidxs = list(range(self.cnt))
        suborder = []
        while subidxs:
            # negate s to preserve all-links-equal subpath ordering
            _, bst = max(((sumlinks[s], -s) for s in subidxs))
            self.moveIdx(suborder, subidxs, outlinks, -bst, lg)
            while True:
                try:
                    _, _, bst = max(((outlinks[s], sumlinks[s], -s)
                                     for s in subidxs if outlinks[s] > 0))
                except ValueError:
                    break
                self.moveIdx(suborder, subidxs, outlinks, -bst, lg)
        return suborder
