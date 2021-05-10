# Copyright 2014 Adobe. All rights reserved.

"""
Utilities for converting between T2 charstrings and the bez data format.
"""

import copy
import logging
import os
import re
import subprocess
import tempfile
import itertools

from fontTools.misc.psCharStrings import (T2OutlineExtractor,
                                          SimpleT2Decompiler)
from fontTools.ttLib import TTFont, newTable
from fontTools.misc.roundTools import noRound, otRound
from fontTools.varLib.varStore import VarStoreInstancer
from fontTools.varLib.cff import CFF2CharStringMergePen, MergeOutlineExtractor
# import subset.cff is needed to load the implementation for
# CFF.desubroutinize: the module adds this class method to the CFF and CFF2
# classes.
import fontTools.subset.cff

from . import fdTools, FontParseError

# keep linting tools quiet about unused import
assert fontTools.subset.cff is not None

log = logging.getLogger(__name__)

kStackLimit = 46
kStemLimit = 96


class SEACError(Exception):
    pass


def _add_method(*clazzes):
    """Returns a decorator function that adds a new method to one or
    more classes."""
    def wrapper(method):
        done = []
        for clazz in clazzes:
            if clazz in done:
                continue  # Support multiple names of a clazz
            done.append(clazz)
            assert clazz.__name__ != 'DefaultTable', \
                'Oops, table class not found.'
            assert not hasattr(clazz, method.__name__), \
                "Oops, class '%s' has method '%s'." % (clazz.__name__,
                                                       method.__name__)
            setattr(clazz, method.__name__, method)
        return None
    return wrapper


def hintOn(i, hintMaskBytes):
    # used to add the active hints to the bez string,
    # when a T2 hintmask operator is encountered.
    byteIndex = int(i / 8)
    byteValue = hintMaskBytes[byteIndex]
    offset = 7 - (i % 8)
    return ((2**offset) & byteValue) > 0


class T2ToBezExtractor(T2OutlineExtractor):
    # The T2OutlineExtractor class calls a class method as the handler for each
    # T2 operator.
    # I use this to convert the T2 operands and arguments to bez operators.
    # Note: flex is converted to regular rrcurveto's.
    # cntrmasks just map to hint replacement blocks with the specified stems.
    def __init__(self, localSubrs, globalSubrs, nominalWidthX, defaultWidthX,
                 read_hints=True, round_coords=True):
        T2OutlineExtractor.__init__(self, None, localSubrs, globalSubrs,
                                    nominalWidthX, defaultWidthX)
        self.vhints = []
        self.hhints = []
        self.bezProgram = []
        self.read_hints = read_hints
        self.firstMarkingOpSeen = False
        self.closePathSeen = False
        self.subrLevel = 0
        self.round_coords = round_coords
        self.hintMaskBytes = None

    def execute(self, charString):
        self.subrLevel += 1
        SimpleT2Decompiler.execute(self, charString)
        self.subrLevel -= 1
        if (not self.closePathSeen) and (self.subrLevel == 0):
            self.closePath()

    def _point(self, point):
        if self.round_coords:
            return " ".join("%d" % round(pt) for pt in point)
        return " ".join("%3f" % pt for pt in point)

    def rMoveTo(self, point):
        point = self._nextPoint(point)
        if not self.firstMarkingOpSeen:
            self.firstMarkingOpSeen = True
            self.bezProgram.append("sc\n")
        log.debug("moveto %s, curpos %s", point, self.currentPoint)
        self.bezProgram.append("%s mt\n" % self._point(point))
        self.sawMoveTo = True

    def rLineTo(self, point):
        if not self.sawMoveTo:
            self.rMoveTo((0, 0))
        point = self._nextPoint(point)
        log.debug("lineto %s, curpos %s", point, self.currentPoint)
        self.bezProgram.append("%s dt\n" % self._point(point))

    def rCurveTo(self, pt1, pt2, pt3):
        if not self.sawMoveTo:
            self.rMoveTo((0, 0))
        pt1 = list(self._nextPoint(pt1))
        pt2 = list(self._nextPoint(pt2))
        pt3 = list(self._nextPoint(pt3))
        log.debug("curveto %s %s %s, curpos %s", pt1, pt2, pt3,
                  self.currentPoint)
        self.bezProgram.append("%s ct\n" % self._point(pt1 + pt2 + pt3))

    def op_endchar(self, index):
        self.endPath()
        args = self.popallWidth()
        if args:  # It is a 'seac' composite character. Don't process
            raise SEACError

    def endPath(self):
        # In T2 there are no open paths, so always do a closePath when
        # finishing a sub path.
        if self.sawMoveTo:
            log.debug("endPath")
            self.bezProgram.append("cp\n")
        self.sawMoveTo = False

    def closePath(self):
        self.closePathSeen = True
        log.debug("closePath")
        if self.bezProgram and self.bezProgram[-1] != "cp\n":
            self.bezProgram.append("cp\n")
        self.bezProgram.append("ed\n")

    def updateHints(self, args, hint_list, bezCommand):
        self.countHints(args)

        # first hint value is absolute hint coordinate, second is hint width
        if not self.read_hints:
            return

        lastval = args[0]
        arg = str(lastval)
        hint_list.append(arg)
        self.bezProgram.append(arg + " ")

        for i in range(len(args))[1:]:
            val = args[i]
            lastval += val

            if i % 2:
                arg = str(val)
                hint_list.append(arg)
                self.bezProgram.append("%s %s\n" % (arg, bezCommand))
            else:
                arg = str(lastval)
                hint_list.append(arg)
                self.bezProgram.append(arg + " ")

    def op_hstem(self, index):
        args = self.popallWidth()
        self.hhints = []
        self.updateHints(args, self.hhints, "rb")
        log.debug("hstem %s", self.hhints)

    def op_vstem(self, index):
        args = self.popallWidth()
        self.vhints = []
        self.updateHints(args, self.vhints, "ry")
        log.debug("vstem %s", self.vhints)

    def op_hstemhm(self, index):
        args = self.popallWidth()
        self.hhints = []
        self.updateHints(args, self.hhints, "rb")
        log.debug("stemhm %s %s", self.hhints, args)

    def op_vstemhm(self, index):
        args = self.popallWidth()
        self.vhints = []
        self.updateHints(args, self.vhints, "ry")
        log.debug("vstemhm %s %s", self.vhints, args)

    def getCurHints(self, hintMaskBytes):
        curhhints = []
        curvhints = []
        numhhints = len(self.hhints)

        for i in range(int(numhhints / 2)):
            if hintOn(i, hintMaskBytes):
                curhhints.extend(self.hhints[2 * i:2 * i + 2])
        numvhints = len(self.vhints)
        for i in range(int(numvhints / 2)):
            if hintOn(i + int(numhhints / 2), hintMaskBytes):
                curvhints.extend(self.vhints[2 * i:2 * i + 2])
        return curhhints, curvhints

    def doMask(self, index, bezCommand):
        args = []
        if not self.hintMaskBytes:
            args = self.popallWidth()
            if args:
                self.vhints = []
                self.updateHints(args, self.vhints, "ry")
            self.hintMaskBytes = int((self.hintCount + 7) / 8)

        self.hintMaskString, index = self.callingStack[-1].getBytes(
            index, self.hintMaskBytes)

        if self.read_hints:
            curhhints, curvhints = self.getCurHints(self.hintMaskString)
            strout = ""
            mask = [strout + hex(ch) for ch in self.hintMaskString]
            log.debug("%s %s %s %s %s", bezCommand, mask, curhhints, curvhints,
                      args)

            self.bezProgram.append("beginsubr snc\n")
            for i, hint in enumerate(curhhints):
                self.bezProgram.append("%s " % hint)
                if i % 2:
                    self.bezProgram.append("rb\n")
            for i, hint in enumerate(curvhints):
                self.bezProgram.append("%s " % hint)
                if i % 2:
                    self.bezProgram.append("ry\n")
            self.bezProgram.extend(["endsubr enc\n", "newcolors\n"])
        return self.hintMaskString, index

    def op_hintmask(self, index):
        hintMaskString, index = self.doMask(index, "hintmask")
        return hintMaskString, index

    def op_cntrmask(self, index):
        hintMaskString, index = self.doMask(index, "cntrmask")
        return hintMaskString, index

    def countHints(self, args):
        self.hintCount = self.hintCount + int(len(args) / 2)


def convertT2GlyphToBez(t2CharString, read_hints=True, round_coords=True):
    # wrapper for T2ToBezExtractor which
    # applies it to the supplied T2 charstring
    subrs = getattr(t2CharString.private, "Subrs", [])
    extractor = T2ToBezExtractor(subrs,
                                 t2CharString.globalSubrs,
                                 t2CharString.private.nominalWidthX,
                                 t2CharString.private.defaultWidthX,
                                 read_hints,
                                 round_coords)
    extractor.execute(t2CharString)
    t2_width_arg = None
    if extractor.gotWidth and (extractor.width is not None):
        t2_width_arg = extractor.width - t2CharString.private.nominalWidthX
    return "".join(extractor.bezProgram), t2_width_arg


class HintMask:
    # class used to collect hints for the current
    # hint mask when converting bez to T2.
    def __init__(self, listPos):
        # The index into the t2list is kept so we can quickly find them later.
        # Note that t2list has one item per operator, and does not include the
        # initial hint operators - first op is always [rhv]moveto or endchar.
        self.listPos = listPos
        # These contain the actual hint values.
        self.h_list = []
        self.v_list = []
        self.mask = None

    def maskByte(self, hHints, vHints):
        # return hintmask bytes for known hints.
        num_hhints = len(hHints)
        num_vhints = len(vHints)
        self.byteLength = byteLength = int((7 + num_hhints + num_vhints) / 8)
        maskVal = 0
        byteIndex = 0
        mask = b""
        if self.h_list:
            mask, maskVal, byteIndex = self.addMaskBits(
                hHints, self.h_list, 0, mask, maskVal, byteIndex)
        if self.v_list:
            mask, maskVal, byteIndex = self.addMaskBits(
                vHints, self.v_list, num_hhints, mask, maskVal, byteIndex)

        if maskVal:
            mask += bytes([maskVal])

        if len(mask) < byteLength:
            mask += b"\0" * (byteLength - len(mask))
        self.mask = mask
        return mask

    @staticmethod
    def addMaskBits(allHints, maskHints, numPriorHints, mask, maskVal,
                    byteIndex):
        # sort in allhints order.
        sort_list = [[allHints.index(hint) + numPriorHints, hint] for hint in
                     maskHints if hint in allHints]
        if not sort_list:
            # we get here if some hints have been dropped # because of
            # the stack limit, so that none of the items in maskHints are
            # not in allHints
            return mask, maskVal, byteIndex

        sort_list.sort()
        (idx_list, maskHints) = zip(*sort_list)
        for i in idx_list:
            newbyteIndex = int(i / 8)
            if newbyteIndex != byteIndex:
                mask += bytes([maskVal])
                byteIndex += 1
                while byteIndex < newbyteIndex:
                    mask += b"\0"
                    byteIndex += 1
                maskVal = 0
            maskVal += 2**(7 - (i % 8))
        return mask, maskVal, byteIndex

    @property
    def num_bits(self):
        count = sum(
            [bin(mask_byte).count('1') for mask_byte in bytearray(self.mask)])
        return count


def make_hint_list(hints, need_hint_masks, is_h):
    # Add the list of T2 tokens that make up the initial hint operators
    hint_list = []
    lastPos = 0
    # In bez terms, the first coordinate in each pair is absolute,
    # second is relative.
    # In T2, each term is relative to the previous one.
    for hint in hints:
        if not hint:
            continue
        pos1 = hint[0]
        pos = pos1 - lastPos
        if pos % 1 == 0:
            pos = int(pos)
        hint_list.append(pos)
        pos2 = hint[1]
        if pos2 % 1 == 0:
            pos2 = int(pos2)
        lastPos = pos1 + pos2
        hint_list.append(pos2)

    if need_hint_masks:
        if is_h:
            op = "hstemhm"
            hint_list.append(op)
        # never need to append vstemhm: if we are using it, it is followed
        # by a mask command and vstemhm is inferred.
    else:
        if is_h:
            op = "hstem"
        else:
            op = "vstem"
        hint_list.append(op)
    return hint_list


bezToT2 = {
    "mt": 'rmoveto',
    "rmt": 'rmoveto',
    "dt": 'rlineto',
    "ct": 'rrcurveto',
    "cp": '',
    "ed": 'endchar'
}


kHintArgsNoOverlap = 0
kHintArgsOverLap = 1
kHintArgsMatch = 2


def checkStem3ArgsOverlap(arg_list, hint_list):
    status = kHintArgsNoOverlap
    for x0, x1 in arg_list:
        x1 = x0 + x1
        for y0, y1 in hint_list:
            y1 = y0 + y1
            if x0 == y0:
                if x1 == y1:
                    status = kHintArgsMatch
                else:
                    return kHintArgsOverLap
            elif x1 == y1:
                return kHintArgsOverLap
            else:
                if (x0 > y0) and (x0 < y1):
                    return kHintArgsOverLap
                if (x1 > y0) and (x1 < y1):
                    return kHintArgsOverLap
    return status


def _add_cntr_maskHints(counter_mask_list, src_hints, is_h):
    for arg_list in src_hints:
        for mask in counter_mask_list:
            dst_hints = mask.h_list if is_h else mask.v_list
            if not dst_hints:
                dst_hints.extend(arg_list)
                overlap_status = kHintArgsMatch
                break
            overlap_status = checkStem3ArgsOverlap(arg_list, dst_hints)
            # The args match args in this control mask.
            if overlap_status == kHintArgsMatch:
                break
        if overlap_status != kHintArgsMatch:
            mask = HintMask(0)
            counter_mask_list.append(mask)
            dst_hints.extend(arg_list)


def build_counter_mask_list(h_stem3_list, v_stem3_list):

    v_counter_mask = HintMask(0)
    h_counter_mask = v_counter_mask
    counter_mask_list = [h_counter_mask]
    _add_cntr_maskHints(counter_mask_list, h_stem3_list, is_h=True)
    _add_cntr_maskHints(counter_mask_list, v_stem3_list, is_h=False)

    return counter_mask_list


def makeRelativeCTArgs(arg_list, curX, curY):
    newCurX = arg_list[4]
    newCurY = arg_list[5]
    arg_list[5] -= arg_list[3]
    arg_list[4] -= arg_list[2]

    arg_list[3] -= arg_list[1]
    arg_list[2] -= arg_list[0]

    arg_list[0] -= curX
    arg_list[1] -= curY
    return arg_list, newCurX, newCurY


def build_hint_order(hints):
    # MM hints have duplicate hints. We want to return a list of indices into
    # the original unsorted and unfiltered list. The list should be sorted, and
    # should filter out duplicates

    num_hints = len(hints)
    index_list = list(range(num_hints))
    hint_list = list(zip(hints, index_list))
    hint_list.sort()
    new_hints = [hint_list[i] for i in range(1, num_hints)
                 if hint_list[i][0] != hint_list[i - 1][0]]
    new_hints = [hint_list[0]] + new_hints
    hints, hint_order = list(zip(*new_hints))
    # hints is now a list of hint pairs, sorted by increasing bottom edge.
    # hint_order is now a list of the hint indices from the bez file, but
    # sorted in the order of the hint pairs.
    return hints, hint_order


def make_abs(hint_pair):
    bottom_edge, delta = hint_pair
    new_hint_pair = [bottom_edge, delta]
    if delta in [-20, -21]:  # It is a ghost hint!
        # We use this only in comparing overlap and order:
        # pretend the delta is 0, as it isn't a real value.
        new_hint_pair[1] = bottom_edge
    else:
        new_hint_pair[1] = bottom_edge + delta
    return new_hint_pair


def check_hint_overlap(hint_list, last_idx, bad_hint_idxs):
    # return True if there is an overlap.
    prev = hint_list[0]
    for i, hint_pair in enumerate(hint_list[1:], 1):
        if prev[1] >= hint_pair[0]:
            bad_hint_idxs.add(i + last_idx - 1)
        prev = hint_pair


def check_hint_pairs(hint_pairs, mm_hint_info, last_idx=0):
    # pairs must be in ascending order by bottom (or left) edge,
    # and pairs in a hint group must not overlap.

    # check order first
    bad_hint_idxs = set()
    prev = hint_pairs[0]
    for i, hint_pair in enumerate(hint_pairs[1:], 1):
        if prev[0] > hint_pair[0]:
            # If there is a conflict, we drop the previous hint
            bad_hint_idxs.add(i + last_idx - 1)
        prev = hint_pair

    # check for overlap in hint groups.
    if mm_hint_info.hint_masks:
        for hint_mask in mm_hint_info.hint_masks:
            if last_idx == 0:
                hint_list = hint_mask.h_list
            else:
                hint_list = hint_mask.v_list
            hint_list = [make_abs(hint_pair) for hint_pair in hint_list]
            check_hint_overlap(hint_list, last_idx, bad_hint_idxs)
    else:
        hint_list = [make_abs(hint_pair) for hint_pair in hint_pairs]
        check_hint_overlap(hint_list, last_idx, bad_hint_idxs)

    if bad_hint_idxs:
        mm_hint_info.bad_hint_idxs |= bad_hint_idxs


def update_hints(in_mm_hints, arg_list, hints, hint_mask, is_v=False):
    if in_mm_hints:
        hints.append(arg_list)
        i = len(hints) - 1
    else:
        try:
            i = hints.index(arg_list)
        except ValueError:
            i = len(hints)
            hints.append(arg_list)
    if hint_mask:
        hint_list = hint_mask.v_list if is_v else hint_mask.h_list
        if hints[i] not in hint_list:
            hint_list.append(hints[i])
    return i


def convertBezToT2(bezString, mm_hint_info=None):
    # convert bez data to a T2 outline program, a list of operator tokens.
    #
    # Convert all bez ops to simplest T2 equivalent.
    # Add all hints to vertical and horizontal hint lists as encountered.
    # Insert a HintMask class whenever a new set of hints is encountered.
    # Add all hints as prefix to t2Program
    # After all operators have been processed, convert HintMask items into
    # hintmask ops and hintmask bytes.
    # Review operator list to optimize T2 operators.
    #
    # If doing MM-hinting, extra work is needed to maintain merge
    # compatibility between the reference font and the region fonts.
    # Although hints are generated for exactly the same outline features
    # in all fonts, they will have different values. Consequently, the
    # hints in a region font may not sort to the same order as in the
    # reference font. In addition, they may be filtered differently. Only
    # unique hints are added from the bez file to the hint list. Two hint
    # pairs may differ in one font, but not in another.
    # We work around these problems by first not filtering the hint
    # pairs for uniqueness when accumulating the hint lists. For the
    # reference font, once we have collected all the hints, we remove any
    # duplicate pairs, but keep a list of the retained hint pair indices
    # into the unfiltered hint pair list. For the region fonts, we
    # select hints from the unfiltered hint pair lists by using the selected
    # index list from the reference font.
    # Note that this breaks the CFF spec for snapshotted instances of the
    # CFF2 VF variable font, as hints may not be in ascending order, and the
    # hint list may contain duplicate hints.

    in_mm_hints = mm_hint_info is not None
    bezString = re.sub(r"%.+?\n", "", bezString)  # suppress comments
    bezList = re.findall(r"(\S+)", bezString)
    if not bezList:
        return ""
    hhints = []
    vhints = []
    # Always assume a hint mask exists until proven
    # otherwise - make an initial HintMask.
    hint_mask = HintMask(0)
    hintMaskList = [hint_mask]
    vStem3Args = []
    hStem3Args = []
    v_stem3_list = []
    h_stem3_list = []
    arg_list = []
    t2List = []

    lastPathOp = None
    curX = 0
    curY = 0
    for token in bezList:
        try:
            val1 = round(float(token), 2)
            try:
                val2 = int(token)
                if int(val1) == val2:
                    arg_list.append(val2)
                else:
                    arg_list.append("%s 100 div" % int(val1 * 100))
            except ValueError:
                arg_list.append(val1)
            continue
        except ValueError:
            pass

        if token == "newcolors":
            lastPathOp = token
        elif token in ["beginsubr", "endsubr"]:
            lastPathOp = token
        elif token == "snc":
            lastPathOp = token
            # The index into the t2list is kept
            # so we can quickly find them later.
            hint_mask = HintMask(len(t2List))
            t2List.append([hint_mask])
            hintMaskList.append(hint_mask)
        elif token == "enc":
            lastPathOp = token
        elif token == "rb":
            update_hints(in_mm_hints, arg_list, hhints, hint_mask, False)
            arg_list = []
            lastPathOp = token
        elif token == "ry":
            update_hints(in_mm_hints, arg_list, vhints, hint_mask, True)
            arg_list = []
            lastPathOp = token
        elif token == "rm":  # vstem3 hints are vhints
            update_hints(in_mm_hints, arg_list, vhints, hint_mask, True)
            if (lastPathOp != token) and vStem3Args:
                # first rm, must be start of a new vstem3
                # if we already have a set of vstems in vStem3Args, save them,
                # and then clear the vStem3Args so we can add the new set.
                v_stem3_list.append(vStem3Args)
                vStem3Args = []

            vStem3Args.append(arg_list)
            arg_list = []
            lastPathOp = token
        elif token == "rv":  # hstem3 are hhints
            update_hints(in_mm_hints, arg_list, hhints, hint_mask, False)

            if (lastPathOp != token) and hStem3Args:
                # first rv, must be start of a new h countermask
                h_stem3_list.append(hStem3Args)
                hStem3Args = []

            hStem3Args.append(arg_list)
            arg_list = []
            lastPathOp = token
        elif token == "preflx1":
            # The preflx1/preflx2a sequence provides the same 'i' as the flex
            # sequence. The difference is that the preflx1/preflx2a sequence
            # provides the argument values needed for building a Type1 string
            # while the flex sequence is simply the 6 rrcurveto points.
            # Both sequences are always provided.
            lastPathOp = token
            arg_list = []
        elif token == "preflx2a":
            lastPathOp = token
            del t2List[-1]
            arg_list = []
        elif token == "flxa":
            lastPathOp = token
            argList1, curX, curY = makeRelativeCTArgs(arg_list[:6], curX, curY)
            argList2, curX, curY = makeRelativeCTArgs(arg_list[6:], curX, curY)
            arg_list = argList1 + argList2
            t2List.append([arg_list[:12] + [50], "flex"])
            arg_list = []
        elif token == "sc":
            lastPathOp = token
        else:
            if token in ["rmt", "mt", "dt", "ct"]:
                lastPathOp = token
            t2Op = bezToT2.get(token, None)
            if token in ["mt", "dt"]:
                newList = [arg_list[0] - curX, arg_list[1] - curY]
                curX = arg_list[0]
                curY = arg_list[1]
                arg_list = newList
            elif token == "ct":
                arg_list, curX, curY = makeRelativeCTArgs(arg_list, curX, curY)
            if t2Op:
                t2List.append([arg_list, t2Op])
            elif t2Op is None:
                raise KeyError("Unhandled operation %s %s" % (arg_list, token))
            arg_list = []

    # Add hints, if any. Must be done at the end of op processing to make sure
    # we have seen all the hints in the bez string. Note that the hintmask are
    # identified in the t2List by an index into the list; be careful NOT to
    # change the t2List length until the hintmasks have been converted.
    need_hint_masks = len(hintMaskList) > 1
    if vStem3Args:
        v_stem3_list.append(vStem3Args)
    if hStem3Args:
        h_stem3_list.append(hStem3Args)

    t2Program = []

    if hhints or vhints:
        if mm_hint_info is None:
            hhints.sort()
            vhints.sort()
        elif mm_hint_info.defined:
            # Apply hint order from reference font in MM hinting
            hhints = [hhints[j] for j in mm_hint_info.h_order]
            vhints = [vhints[j] for j in mm_hint_info.v_order]
        else:
            # Define hint order from reference font in MM hinting
            hhints, mm_hint_info.h_order = build_hint_order(hhints)
            vhints, mm_hint_info.v_order = build_hint_order(vhints)

        num_hhints = len(hhints)
        num_vhints = len(vhints)
        hint_limit = int((kStackLimit - 2) / 2)
        if num_hhints >= hint_limit:
            hhints = hhints[:hint_limit]
        if num_vhints >= hint_limit:
            vhints = vhints[:hint_limit]

        if mm_hint_info and mm_hint_info.defined:
            check_hint_pairs(hhints, mm_hint_info)
            last_idx = len(hhints)
            check_hint_pairs(vhints, mm_hint_info, last_idx)

        if hhints:
            t2Program = make_hint_list(hhints, need_hint_masks, is_h=True)
        if vhints:
            t2Program += make_hint_list(vhints, need_hint_masks, is_h=False)

        cntrmask_progam = None
        if mm_hint_info is None:
            if v_stem3_list or h_stem3_list:
                counter_mask_list = build_counter_mask_list(h_stem3_list,
                                                            v_stem3_list)
                cntrmask_progam = [['cntrmask', cMask.maskByte(hhints,
                                                               vhints)] for
                                   cMask in counter_mask_list]
        elif (not mm_hint_info.defined):
            if v_stem3_list or h_stem3_list:
                # this is the reference font - we need to build the list.
                counter_mask_list = build_counter_mask_list(h_stem3_list,
                                                            v_stem3_list)
                cntrmask_progam = [['cntrmask', cMask.maskByte(hhints,
                                                               vhints)] for
                                   cMask in counter_mask_list]
                mm_hint_info.cntr_masks = counter_mask_list
        else:
            # This is a region font - we need to used the reference font list.
            counter_mask_list = mm_hint_info.cntr_masks
            cntrmask_progam = [['cntrmask', cMask.mask] for
                               cMask in counter_mask_list]

        if cntrmask_progam:
            cntrmask_progam = itertools.chain(*cntrmask_progam)
            t2Program.extend(cntrmask_progam)

        if need_hint_masks:
            # If there is not a hintsub before any drawing operators, then
            # add an initial first hint mask to the t2Program.
            if (mm_hint_info is None) or (not mm_hint_info.defined):
                # a single font and a reference font for mm hinting are
                # processed the same way
                if hintMaskList[1].listPos != 0:
                    hBytes = hintMaskList[0].maskByte(hhints, vhints)
                    t2Program.extend(["hintmask", hBytes])
                    if in_mm_hints:
                        mm_hint_info.hint_masks.append(hintMaskList[0])

                # Convert the rest of the hint masks
                # to a hintmask op and hintmask bytes.
                for hint_mask in hintMaskList[1:]:
                    pos = hint_mask.listPos
                    hBytes = hint_mask.maskByte(hhints, vhints)
                    t2List[pos] = [["hintmask"], hBytes]
                    if in_mm_hints:
                        mm_hint_info.hint_masks.append(hint_mask)
            elif (mm_hint_info is not None):
                # This is a MM region font:
                # apply hint masks from reference font.
                try:
                    hm0_mask = mm_hint_info.hint_masks[0].mask
                except IndexError:
                    import pdb
                    pdb.set_trace()
                if isinstance(t2List[0][0], HintMask):
                    t2List[0] = [["hintmask"], hm0_mask]
                else:
                    t2Program.extend(["hintmask", hm0_mask])

                for hm in mm_hint_info.hint_masks[1:]:
                    t2List[hm.listPos] = [["hintmask"], hm.mask]

    for entry in t2List:
        try:
            t2Program.extend(entry[0])
            t2Program.append(entry[1])
        except Exception:
            raise KeyError("Failed to extend t2Program with entry %s" % entry)

    if in_mm_hints:
        mm_hint_info.defined = True
    return t2Program


def _run_tx(args):
    try:
        subprocess.check_call(["tx"] + args, stderr=subprocess.DEVNULL)
    except (subprocess.CalledProcessError, OSError) as e:
        raise FontParseError(e)


class FixHintWidthDecompiler(SimpleT2Decompiler):
    # If we are using this class, we know the charstring has hints.
    def __init__(self, localSubrs, globalSubrs, private=None):
        self.hintMaskBytes = 0  # to silence false Codacy error.
        SimpleT2Decompiler.__init__(self, localSubrs, globalSubrs, private)
        self.has_explicit_width = None
        self.h_hint_args = self.v_hint_args = None
        self.last_stem_index = None

    def op_hstem(self, index):
        self.countHints(is_vert=False)
        self.last_stem_index = index
    op_hstemhm = op_hstem

    def op_vstem(self, index):
        self.countHints(is_vert=True)
        self.last_stem_index = index
    op_vstemhm = op_vstem

    def op_hintmask(self, index):
        if not self.hintMaskBytes:
            # Note that I am assuming that there is never an op_vstemhm
            # followed by an op_hintmask. Since this is applied after saving
            # the font with fontTools, this is safe.
            self.countHints(is_vert=True)
            self.hintMaskBytes = (self.hintCount + 7) // 8
        cs = self.callingStack[-1]
        hintMaskBytes, index = cs.getBytes(index, self.hintMaskBytes)
        return hintMaskBytes, index
    op_cntrmask = op_hintmask

    def countHints(self, is_vert):
        args = self.popall()
        if self.has_explicit_width is None:
            if (len(args) % 2) == 0:
                self.has_explicit_width = False
            else:
                self.has_explicit_width = True
                self.width_arg = args[0]
                args = args[1:]
        self.hintCount = self.hintCount + len(args) // 2
        if is_vert:
            self.v_hint_args = args
        else:
            self.h_hint_args = args


class CFFFontData:
    def __init__(self, path, font_format):
        self.inputPath = path
        self.font_format = font_format
        self.mm_hint_info_dict = {}
        self.t2_widths = {}
        self.is_cff2 = False
        self.is_vf = False
        self.vs_data_models = None
        if font_format == "OTF":
            # It is an OTF font, we can process it directly.
            font = TTFont(path)
            if "CFF " in font:
                cff_format = "CFF "
            elif "CFF2" in font:
                cff_format = "CFF2"
                self.is_cff2 = True
            else:
                raise FontParseError("OTF font has no CFF table <%s>." % path)
        else:
            # Else, package it in an OTF font.
            cff_format = "CFF "
            if font_format == "CFF":
                with open(path, "rb") as fp:
                    data = fp.read()
            else:
                fd, temp_path = tempfile.mkstemp()
                os.close(fd)
                try:
                    _run_tx(["-cff", "+b", "-std", path, temp_path])
                    with open(temp_path, "rb") as fp:
                        data = fp.read()
                finally:
                    os.remove(temp_path)

            font = TTFont()
            font['CFF '] = newTable('CFF ')
            font['CFF '].decompile(data, font)

        self.ttFont = font
        self.cffTable = font[cff_format]

        # for identifier in glyph-list:
        # Get charstring.
        self.topDict = self.cffTable.cff.topDictIndex[0]
        self.charStrings = self.topDict.CharStrings
        if 'fvar' in self.ttFont:
            # have not yet collected VF global data.
            self.is_vf = True
            fvar = self.ttFont['fvar']
            CFF2 = self.cffTable
            CFF2.desubroutinize()
            topDict = CFF2.cff.topDictIndex[0]
            # We need a new charstring object into which we can save the
            # hinted CFF2 program data. Copying an existing charstring is a
            # little easier than creating a new one and making sure that all
            # properties are set correctly.
            self.temp_cs = copy.deepcopy(self.charStrings['.notdef'])
            self.vs_data_models = self.get_vs_data_models(topDict,
                                                          fvar)

    def getGlyphList(self):
        return self.ttFont.getGlyphOrder()

    def getPSName(self):
        if self.is_cff2 and 'name' in self.ttFont:
            psName = next((name_rec.string for name_rec in self.ttFont[
                'name'].names if (name_rec.nameID == 6) and (
                    name_rec.platformID == 3)))
            psName = psName.decode('utf-16be')
        else:
            psName = self.cffTable.cff.fontNames[0]
        return psName

    def get_min_max(self, pTopDict, upm):
        if self.is_cff2 and 'hhea' in self.ttFont:
            font_max = self.ttFont['hhea'].ascent
            font_min = self.ttFont['hhea'].descent
        elif hasattr(pTopDict, 'FontBBox'):
            font_max = pTopDict.FontBBox[3]
            font_min = pTopDict.FontBBox[1]
        else:
            font_max = upm * 1.25
            font_min = -upm * 0.25
        alignment_min = min(-upm * 0.25, font_min)
        alignment_max = max(upm * 1.25, font_max)
        return alignment_min, alignment_max

    def convertToBez(self, glyphName, read_hints, round_coords, doAll=False):
        t2Wdth = None
        t2CharString = self.charStrings[glyphName]
        try:
            bezString, t2Wdth = convertT2GlyphToBez(t2CharString,
                                                    read_hints, round_coords)
            # Note: the glyph name is important, as it is used by the C-code
            # for various heuristics, including [hv]stem3 derivation.
            bezString = "% " + glyphName + "\n" + bezString
        except SEACError:
            log.warning("Skipping %s: can't process SEAC composite glyphs.",
                        glyphName)
            bezString = None
        self.t2_widths[glyphName] = t2Wdth
        return bezString

    def updateFromBez(self, bezData, glyphName, mm_hint_info=None):
        t2Program = convertBezToT2(bezData, mm_hint_info)
        if not self.is_cff2:
            t2_width_arg = self.t2_widths[glyphName]
            if t2_width_arg is not None:
                t2Program = [t2_width_arg] + t2Program
        if self.vs_data_models is not None:
            # It is a variable font. Accumulate the charstrings.
            self.glyph_programs.append(t2Program)
        else:
            # This is an MM source font. Update the font's charstring directly.
            t2CharString = self.charStrings[glyphName]
            t2CharString.program = t2Program

    def save(self, path):
        if path is None:
            path = self.inputPath

        if self.font_format == "OTF":
            self.ttFont.save(path)
            self.ttFont.close()
        else:
            data = self.ttFont["CFF "].compile(self.ttFont)
            if self.font_format == "CFF":
                with open(path, "wb") as fp:
                    fp.write(data)
            else:
                fd, temp_path = tempfile.mkstemp()
                os.write(fd, data)
                os.close(fd)

                try:
                    args = ["-t1", "-std"]
                    if self.font_format == "PFB":
                        args.append("-pfb")
                    _run_tx(args + [temp_path, path])
                finally:
                    os.remove(temp_path)

    def close(self):
        self.ttFont.close()

    def isCID(self):
        return hasattr(self.topDict, "FDSelect")

    def hasFDArray(self):
        return self.is_cff2 or hasattr(self.topDict, "FDSelect")

    def flattenBlends(self, blendList):
        if type(blendList[0]) is list:
            flatList = [blendList[i][0] for i in range(len(blendList))]
        else:
            flatList = blendList
        return flatList

    def getFontInfo(self, allow_no_blues, noFlex,
                    vCounterGlyphs, hCounterGlyphs, fdIndex=0):
        # The psautohint library needs the global font hint zones
        # and standard stem widths.
        # Format them into a single text string.
        # The text format is arbitrary, inherited from very old software,
        # but there is no real need to change it.
        pTopDict = self.topDict
        if hasattr(pTopDict, "FDArray"):
            pDict = pTopDict.FDArray[fdIndex]
        else:
            pDict = pTopDict
        privateDict = pDict.Private

        fdDict = fdTools.FDDict()
        fdDict.LanguageGroup = getattr(privateDict, "LanguageGroup", "0")

        if hasattr(pDict, "FontMatrix"):
            fdDict.FontMatrix = pDict.FontMatrix
        else:
            fdDict.FontMatrix = pTopDict.FontMatrix
        upm = int(1 / fdDict.FontMatrix[0])
        fdDict.OrigEmSqUnits = str(upm)

        fdDict.FontName = getattr(pTopDict, "FontName", self.getPSName())

        blueValues = getattr(privateDict, "BlueValues", [])[:]
        numBlueValues = len(blueValues)
        if numBlueValues < 4:
            low, high = self.get_min_max(pTopDict, upm)
            # Make a set of inactive alignment zones: zones outside of the
            # font BBox so as not to affect hinting. Used when source font has
            # no BlueValues or has invalid BlueValues. Some fonts have bad BBox
            # values, so I don't let this be smaller than -upm*0.25, upm*1.25.
            inactiveAlignmentValues = [low, low, high, high]
            if allow_no_blues:
                blueValues = inactiveAlignmentValues
                numBlueValues = len(blueValues)
            else:
                raise FontParseError("Font must have at least four values in "
                                     "its BlueValues array for PSAutoHint to "
                                     "work!")
        blueValues.sort()

        # The first pair only is a bottom zone, where the first value is the
        # overshoot position. The rest are top zones, and second value of the
        # pair is the overshoot position.
        blueValues = self.flattenBlends(blueValues)
        blueValues[0] = blueValues[0] - blueValues[1]
        for i in range(3, numBlueValues, 2):
            blueValues[i] = blueValues[i] - blueValues[i - 1]

        blueValues = [str(v) for v in blueValues]
        numBlueValues = min(numBlueValues, len(fdTools.kBlueValueKeys))
        for i in range(numBlueValues):
            key = fdTools.kBlueValueKeys[i]
            value = blueValues[i]
            setattr(fdDict, key, value)

        if hasattr(privateDict, "OtherBlues"):
            # For all OtherBlues, the pairs are bottom zones, and
            # the first value of each pair is the overshoot position.
            i = 0
            numBlueValues = len(privateDict.OtherBlues)
            blueValues = privateDict.OtherBlues[:]
            blueValues.sort()
            blueValues = self.flattenBlends(blueValues)
            for i in range(0, numBlueValues, 2):
                blueValues[i] = blueValues[i] - blueValues[i + 1]
            blueValues = [str(v) for v in blueValues]
            numBlueValues = min(numBlueValues,
                                len(fdTools.kOtherBlueValueKeys))
            for i in range(numBlueValues):
                key = fdTools.kOtherBlueValueKeys[i]
                value = blueValues[i]
                setattr(fdDict, key, value)

        if hasattr(privateDict, "StemSnapV"):
            vstems = privateDict.StemSnapV
        elif hasattr(privateDict, "StdVW"):
            vstems = [privateDict.StdVW]
        else:
            if allow_no_blues:
                # dummy value. Needs to be larger than any hint will likely be,
                # as the autohint program strips out any hint wider than twice
                # the largest global stem width.
                vstems = [upm]
            else:
                raise FontParseError("Font has neither StemSnapV nor StdVW!")
        vstems.sort()
        vstems = self.flattenBlends(vstems)
        if (len(vstems) == 0) or ((len(vstems) == 1) and (vstems[0] < 1)):
            vstems = [upm]  # dummy value that will allow PyAC to run
            log.warning("There is no value or 0 value for DominantV.")
        fdDict.DominantV = "[" + " ".join([str(v) for v in vstems]) + "]"

        if hasattr(privateDict, "StemSnapH"):
            hstems = privateDict.StemSnapH
        elif hasattr(privateDict, "StdHW"):
            hstems = [privateDict.StdHW]
        else:
            if allow_no_blues:
                # dummy value. Needs to be larger than any hint will likely be,
                # as the autohint program strips out any hint wider than twice
                # the largest global stem width.
                hstems = [upm]
            else:
                raise FontParseError("Font has neither StemSnapH nor StdHW!")
        hstems.sort()
        hstems = self.flattenBlends(hstems)
        if (len(hstems) == 0) or ((len(hstems) == 1) and (hstems[0] < 1)):
            hstems = [upm]  # dummy value that will allow PyAC to run
            log.warning("There is no value or 0 value for DominantH.")
        fdDict.DominantH = "[" + " ".join([str(v) for v in hstems]) + "]"

        if noFlex:
            fdDict.FlexOK = "false"
        else:
            fdDict.FlexOK = "true"

        # Add candidate lists for counter hints, if any.
        if vCounterGlyphs:
            temp = " ".join(vCounterGlyphs)
            fdDict.VCounterChars = "( %s )" % (temp)
        if hCounterGlyphs:
            temp = " ".join(hCounterGlyphs)
            fdDict.HCounterChars = "( %s )" % (temp)

        fdDict.BlueFuzz = getattr(privateDict, "BlueFuzz", 1)

        return fdDict

    def getfdIndex(self, name):
        gid = self.ttFont.getGlyphID(name)
        if hasattr(self.topDict, "FDSelect"):
            fdIndex = self.topDict.FDSelect[gid]
        else:
            fdIndex = 0
        return fdIndex

    def getfdInfo(self, allow_no_blues, noFlex, vCounterGlyphs, hCounterGlyphs,
                  glyphList, fdIndex=0):
        topDict = self.topDict
        fdGlyphDict = None

        # Get the default fontinfo from the font's top dict.
        fdDict = self.getFontInfo(
            allow_no_blues, noFlex, vCounterGlyphs, hCounterGlyphs, fdIndex)
        fontDictList = [fdDict]

        # Check the fontinfo file, and add any other font dicts
        srcFontInfo = os.path.dirname(self.inputPath)
        srcFontInfo = os.path.join(srcFontInfo, "fontinfo")
        if os.path.exists(srcFontInfo):
            with open(srcFontInfo, "r", encoding="utf-8") as fi:
                fontInfoData = fi.read()
            fontInfoData = re.sub(r"#[^\r\n]+", "", fontInfoData)
        else:
            return fdGlyphDict, fontDictList

        if "FDDict" in fontInfoData:
            maxY = topDict.FontBBox[3]
            minY = topDict.FontBBox[1]
            fdGlyphDict, fontDictList, finalFDict = fdTools.parseFontInfoFile(
                fontDictList, fontInfoData, glyphList, maxY, minY,
                self.getPSName())
            if hasattr(topDict, "FDArray"):
                private = topDict.FDArray[fdIndex].Private
            else:
                private = topDict.Private
            if finalFDict is None:
                # If a font dict was not explicitly specified for the
                # output font, use the first user-specified font dict.
                fdTools.mergeFDDicts(fontDictList[1:], private)
            else:
                fdTools.mergeFDDicts([finalFDict], private)
        return fdGlyphDict, fontDictList

    @staticmethod
    def args_to_hints(hint_args):
        hints = [hint_args[0:2]]
        prev = hints[0]
        for i in range(2, len(hint_args), 2):
            bottom = hint_args[i] + prev[0] + prev[1]
            hints.append([bottom, hint_args[i + 1]])
            prev = hints[-1]
        return hints

    @staticmethod
    def extract_hint_args(program):
        width = None
        h_hint_args = []
        v_hint_args = []
        for i, token in enumerate(program):
            if type(token) is str:
                if i % 2 != 0:
                    width = program[0]
                    del program[0]
                    idx = i - 1
                else:
                    idx = i

                if (token[:4] == 'vstem') or token[-3:] == 'mask':
                    h_hint_args = []
                    v_hint_args = program[:idx]

                elif token[:5] == 'hstem':
                    h_hint_args = program[:idx]
                    v_program = program[idx + 1:]

                    for j, vtoken in enumerate(v_program):
                        if type(vtoken) is str:
                            if (vtoken[:5] == 'vstem') or vtoken[-4:] == \
                                    'mask':
                                v_hint_args = v_program[:j]
                                break
                break

        return width, h_hint_args, v_hint_args

    def fix_t2_program_hints(self, program, mm_hint_info, is_reference_font):

        width_arg, h_hint_args, v_hint_args = self.extract_hint_args(program)

        # 1. Build list of good [vh]hints.
        bad_hint_idxs = list(mm_hint_info.bad_hint_idxs)
        bad_hint_idxs.sort()
        num_hhint_pairs = len(h_hint_args) // 2
        for idx in reversed(bad_hint_idxs):
            if idx < num_hhint_pairs:
                hint_args = h_hint_args
                bottom_idx = idx * 2
            else:
                hint_args = v_hint_args
                bottom_idx = (idx - num_hhint_pairs) * 2
            delta = hint_args[bottom_idx] + hint_args[bottom_idx + 1]
            del hint_args[bottom_idx:bottom_idx + 2]
            if len(hint_args) > bottom_idx:
                hint_args[bottom_idx] += delta

        # delete old hints from program
        if mm_hint_info.cntr_masks:
            last_hint_idx = program.index('cntrmask')
        elif mm_hint_info.hint_masks:
            last_hint_idx = program.index('hintmask')
        else:
            for op in ['vstem', 'hstem']:
                try:
                    last_hint_idx = program.index(op)
                    break
                except IndexError:
                    last_hint_idx = None
        if last_hint_idx is not None:
            del program[:last_hint_idx]

        # If there were v_hint_args, but they have now all been
        # deleted, the first token will still be 'vstem[hm]'. Delete it.
        if ((not v_hint_args) and program[0].startswith('vstem')):
            del program[0]

        # Add width and updated hints back.
        if width_arg is not None:
            hint_program = [width_arg]
        else:
            hint_program = []
        if h_hint_args:
            op_hstem = 'hstemhm' if mm_hint_info.hint_masks else 'hstem'
            hint_program.extend(h_hint_args)
            hint_program.append(op_hstem)
        if v_hint_args:
            hint_program.extend(v_hint_args)
            # Don't need to append op_vstem, as this is still in hint_program.
            program = hint_program + program

        # Re-calculate the hint masks.
        if is_reference_font:
            hhints = self.args_to_hints(h_hint_args)
            vhints = self.args_to_hints(v_hint_args)
            for hm in mm_hint_info.hint_masks:
                hm.maskByte(hhints, vhints)

        # Apply fixed hint masks
        if mm_hint_info.hint_masks:
            hm_pos_list = [i for i, token in enumerate(program)
                           if token == 'hintmask']
            for i, hm in enumerate(mm_hint_info.hint_masks):
                pos = hm_pos_list[i]
                program[pos + 1] = hm.mask

        # Now fix the control masks. We will weed out a control mask
        # if it ends up with fewer than 3 hints.
        cntr_masks = mm_hint_info.cntr_masks
        if is_reference_font and cntr_masks:
            # Update mask bytes,
            # and remove control masks with fewer than 3 bits.
            mask_byte_list = [cm.mask for cm in cntr_masks]
            for cm in cntr_masks:
                cm.maskByte(hhints, vhints)
            new_cm_list = [cm for cm in cntr_masks if cm.num_bits >= 3]
            new_mask_byte_list = [cm.mask for cm in new_cm_list]
            if new_mask_byte_list != mask_byte_list:
                mm_hint_info.new_cntr_masks = new_cm_list
        if mm_hint_info.new_cntr_masks:
            # Remove all the old cntrmask ops
            num_old_cm = len(cntr_masks)
            idx = program.index('cntrmask')
            del program[idx:idx + num_old_cm * 2]
            cm_progam = [['cntrmask', cm.mask] for cm in
                         mm_hint_info.new_cntr_masks]
            cm_progam = list(itertools.chain(*cm_progam))
            program[idx:idx] = cm_progam
        return program

    def fix_glyph_hints(self, glyph_name, mm_hint_info,
                        is_reference_font=None):
        # 1. Delete any bad hints.
        # 2. If reference font, recalculate the hint mask byte strings
        # 3. Replace hint masks.
        # 3. Fix cntr masks.
        if self.is_vf:
            # We get called once, and fix all the charstring programs.
            for i, t2_program in enumerate(self.glyph_programs):
                self.glyph_programs[i] = self.fix_t2_program_hints(
                    t2_program, mm_hint_info, is_reference_font=(i == 0))
        else:
            # we are called for each font in turn
            try:
                t2CharString = self.charStrings[glyph_name]
            except KeyError:
                return  # Happens with sparse sources - just skip the glyph.

            program = self.fix_t2_program_hints(t2CharString.program,
                                                mm_hint_info,
                                                is_reference_font)
            t2CharString.program = program

    def get_vf_bez_glyphs(self, glyph_name):
        charstring = self.charStrings[glyph_name]

        if 'vsindex' in charstring.program:
            op_index = charstring.program.index('vsindex')
            vsindex = charstring.program[op_index - 1]
        else:
            vsindex = 0
        self.vsindex = vsindex
        self.glyph_programs = []
        vs_data_model = self.vs_data_model = self.vs_data_models[vsindex]

        bez_list = []
        for vsi in vs_data_model.master_vsi_list:
            t2_program = interpolate_cff2_charstring(charstring, glyph_name,
                                                     vsi.interpolateFromDeltas,
                                                     vsindex)
            self.temp_cs.program = t2_program
            bezString, _ = convertT2GlyphToBez(self.temp_cs, True, True)
            #  DBG Adding glyph name is useful only for debugging.
            bezString = "% {}\n".format(glyph_name) + bezString
            bez_list.append(bezString)
        return bez_list

    @staticmethod
    def get_vs_data_models(topDict, fvar):
        otvs = topDict.VarStore.otVarStore
        region_list = otvs.VarRegionList.Region
        axis_tags = [axis_entry.axisTag for axis_entry in fvar.axes]
        vs_data_models = []
        for vsindex, var_data in enumerate(otvs.VarData):
            vsi = VarStoreInstancer(topDict.VarStore.otVarStore, fvar.axes, {})
            master_vsi_list = [vsi]
            for region_idx in var_data.VarRegionIndex:
                region = region_list[region_idx]
                loc = {}
                for i, axis in enumerate(region.VarRegionAxis):
                    loc[axis_tags[i]] = axis.PeakCoord
                vsi = VarStoreInstancer(topDict.VarStore.otVarStore, fvar.axes,
                                        loc)
                master_vsi_list.append(vsi)
            vdm = VarDataModel(var_data, vsindex, master_vsi_list)
            vs_data_models.append(vdm)
        return vs_data_models

    def merge_hinted_glyphs(self, name):
        new_t2cs = merge_hinted_programs(self.temp_cs, self.glyph_programs,
                                         name, self.vs_data_model)
        if self.vsindex:
            new_t2cs.program = [self.vsindex, 'vsindex'] + new_t2cs.program
        self.charStrings[name] = new_t2cs


def interpolate_cff2_charstring(charstring, gname, interpolateFromDeltas,
                                vsindex):
    # Interpolate charstring
    # e.g replace blend op args with regular args,
    # and discard vsindex op.
    new_program = []
    last_i = 0
    program = charstring.program
    for i, token in enumerate(program):
        if token == 'vsindex':
            if last_i != 0:
                new_program.extend(program[last_i:i - 1])
            last_i = i + 1
        elif token == 'blend':
            num_regions = charstring.getNumRegions(vsindex)
            numMasters = 1 + num_regions
            num_args = program[i - 1]
            # The program list starting at program[i] is now:
            # ..args for following operations
            # num_args values  from the default font
            # num_args tuples, each with numMasters-1 delta values
            # num_blend_args
            # 'blend'
            argi = i - (num_args * numMasters + 1)
            if last_i != argi:
                new_program.extend(program[last_i:argi])
            end_args = tuplei = argi + num_args
            master_args = []
            while argi < end_args:
                next_ti = tuplei + num_regions
                deltas = program[tuplei:next_ti]
                val = interpolateFromDeltas(vsindex, deltas)
                master_val = program[argi]
                master_val += otRound(val)
                master_args.append(master_val)
                tuplei = next_ti
                argi += 1
            new_program.extend(master_args)
            last_i = i + 1
    if last_i != 0:
        new_program.extend(program[last_i:])
    return new_program


def merge_hinted_programs(charstring, t2_programs, gname, vs_data_model):
    num_masters = vs_data_model.num_masters
    var_pen = CFF2CharStringMergePen([], gname, num_masters, 0)
    charstring.outlineExtractor = MergeOutlineExtractor

    for i, t2_program in enumerate(t2_programs):
        var_pen.restart(i)
        charstring.program = t2_program
        charstring.draw(var_pen)

    new_charstring = var_pen.getCharString(
        private=charstring.private,
        globalSubrs=charstring.globalSubrs,
        var_model=vs_data_model, optimize=True)
    return new_charstring


@_add_method(VarStoreInstancer)
def get_scalars(self, vsindex, region_idx):
    varData = self._varData
    # The index key needs to be the master value index, which includes
    # the default font value. VarRegionIndex provides the region indices.
    scalars = {0: 1.0}  # The default font always has a weight of 1.0
    region_index = varData[vsindex].VarRegionIndex
    for idx in range(region_idx):  # omit the scalar for the region.
        scalar = self._getScalar(region_index[idx])
        if scalar:
            scalars[idx + 1] = scalar
    return scalars


class VarDataModel(object):

    def __init__(self, var_data, vsindex, master_vsi_list):
        self.master_vsi_list = master_vsi_list
        self.var_data = var_data
        self._num_masters = len(master_vsi_list)
        self.delta_weights = [{}]  # for default font value
        for region_idx, vsi in enumerate(master_vsi_list[1:]):
            scalars = vsi.get_scalars(vsindex, region_idx)
            self.delta_weights.append(scalars)

    @property
    def num_masters(self):
        return self._num_masters

    def getDeltas(self, master_values, *, round=noRound):
        assert len(master_values) == len(self.delta_weights)
        out = []
        for i, scalars in enumerate(self.delta_weights):
            delta = master_values[i]
            for j, scalar in scalars.items():
                if scalar:
                    delta -= out[j] * scalar
            out.append(round(delta))
        return out
