# Copyright 2014 Adobe. All rights reserved.

"""
Utilities for converting between T2 charstrings and the bez data format.
Used by AC and focus/CheckOutlines.
"""

from __future__ import print_function, absolute_import

import os
import re
import sys

from fontTools.misc.psCharStrings import T2OutlineExtractor, SimpleT2Decompiler
from fontTools.misc.py23 import *

from psautohint import fdTools

debug = False


def debugMsg(*args):
    if debug:
        print(args)


kStackLimit = 46
kStemLimit = 96


class ACFontError(Exception):
    pass


class SEACError(Exception):
    pass


def hintOn(i, hintMaskBytes):
    # used to add the active hints to the bez string,
    # when a T2 hintmask operator is encountered.
    byteIndex = int(i / 8)
    byteValue = byteord(hintMaskBytes[byteIndex])
    offset = 7 - (i % 8)
    return ((2**offset) & byteValue) > 0


class T2ToBezExtractor(T2OutlineExtractor):
    # The T2OutlineExtractor class calls a class method as the handler for each
    # T2 operator.
    # I use this to convert the T2 operands and arguments to bez operators.
    # Note: flex is converted to regular rrcurveto's.
    # cntrmasks just map to hint replacement blocks with the specified stems.
    def __init__(self, localSubrs, globalSubrs, nominalWidthX, defaultWidthX,
                 allowDecimals=False):
        T2OutlineExtractor.__init__(self, None, localSubrs, globalSubrs,
                                    nominalWidthX, defaultWidthX)
        self.vhints = []
        self.hhints = []
        self.bezProgram = []
        self.firstMarkingOpSeen = False
        self.closePathSeen = False
        self.subrLevel = 0
        self.allowDecimals = allowDecimals
        self.hintMaskBytes = None

    def execute(self, charString):
        self.subrLevel += 1
        SimpleT2Decompiler.execute(self, charString)
        self.subrLevel -= 1
        if (not self.closePathSeen) and (self.subrLevel == 0):
            self.closePath()

    def rMoveTo(self, point):
        point = self._nextPoint(point)
        if not self.firstMarkingOpSeen:
            self.firstMarkingOpSeen = True
            self.bezProgram.append("sc\n")
        debugMsg("moveto", point, "curpos", self.currentPoint)
        x = point[0]
        y = point[1]
        if (not self.allowDecimals):
            x = int(round(x))
            y = int(round(y))
            self.bezProgram.append("%s %s mt\n" % (x, y))
        else:
            self.bezProgram.append("%.2f %.2f mt\n" % (x, y))
        self.sawMoveTo = 1

    def rLineTo(self, point):
        point = self._nextPoint(point)
        if not self.firstMarkingOpSeen:
            self.firstMarkingOpSeen = True
            self.bezProgram.append("sc\n")
            self.bezProgram.append("0 0 mt\n")
        debugMsg("lineto", point, "curpos", self.currentPoint)
        if not self.sawMoveTo:
            self.rMoveTo((0, 0))
        x = point[0]
        y = point[1]
        if (not self.allowDecimals):
            x = int(round(x))
            y = int(round(y))
            self.bezProgram.append("%s %s dt\n" % (x, y))
        else:
            self.bezProgram.append("%.2f %.2f dt\n" % (x, y))

    def rCurveTo(self, pt1, pt2, pt3):
        pt1 = list(self._nextPoint(pt1))
        pt2 = list(self._nextPoint(pt2))
        pt3 = list(self._nextPoint(pt3))
        if not self.firstMarkingOpSeen:
            self.firstMarkingOpSeen = True
            self.bezProgram.append("sc\n")
            self.bezProgram.append("0 0 mt\n")
        debugMsg("curveto", pt1, pt2, pt3, "curpos", self.currentPoint)
        if not self.sawMoveTo:
            self.rMoveTo((0, 0))
        if (not self.allowDecimals):
            for pt in [pt1, pt2, pt3]:
                pt[0] = int(round(pt[0]))
                pt[1] = int(round(pt[1]))
            self.bezProgram.append(
                "%s %s %s %s %s %s ct\n" %
                (pt1[0], pt1[1], pt2[0], pt2[1], pt3[0], pt3[1]))
        else:
            self.bezProgram.append(
                "%.2f %.2f %.2f %.2f %.2f %.2f ct\n" %
                (pt1[0], pt1[1], pt2[0], pt2[1], pt3[0], pt3[1]))

    def op_endchar(self, index):
        self.endPath()
        args = self.popallWidth()
        if args:  # It is a 'seac' composite character. Don't process
            raise SEACError

    def endPath(self):
        # In T2 there are no open paths, so always do a closePath when
        # finishing a sub path.
        if self.sawMoveTo:
            debugMsg("endPath")
            self.bezProgram.append("cp\n")
        self.sawMoveTo = 0

    def closePath(self):
        self.closePathSeen = True
        debugMsg("closePath")
        if self.bezProgram and self.bezProgram[-1] != "cp\n":
            self.bezProgram.append("cp\n")
        self.bezProgram.append("ed\n")

    def op_hstem(self, index):
        args = self.popallWidth()
        self.hhints = []
        self.countHints(args)
        debugMsg("hstem", self.hhints)

    def op_vstem(self, index):
        args = self.popallWidth()
        self.vhints = []
        self.countHints(args)
        debugMsg("vstem", self.vhints)

    def op_hstemhm(self, index):
        args = self.popallWidth()
        self.hhints = []
        self.countHints(args)
        debugMsg("stemhm", self.hhints, args)

    def op_vstemhm(self, index):
        args = self.popallWidth()
        self.vhints = []
        self.countHints(args)
        debugMsg("vstemhm", self.vhints, args)

    def doMask(self, index, bezCommand):
        args = []
        if not self.hintMaskBytes:
            args = self.popallWidth()
            if args:
                self.vhints = []
                self.countHints(args)
            self.hintMaskBytes = int((self.hintCount + 7) / 8)

        self.hintMaskString, index = self.callingStack[-1].getBytes(
            index, self.hintMaskBytes)

        return self.hintMaskString, index

    def op_hintmask(self, index):
        hintMaskString, index = self.doMask(index, "hintmask")
        return hintMaskString, index

    def op_cntrmask(self, index):
        hintMaskString, index = self.doMask(index, "cntrmask")
        return hintMaskString, index

    def countHints(self, args):
        self.hintCount = self.hintCount + int(len(args) / 2)


def convertT2GlyphToBez(t2CharString, allowDecimals=False):
    # wrapper for T2ToBezExtractor which
    # applies it to the supplied T2 charstring
    subrs = getattr(t2CharString.private, "Subrs", [])
    extractor = T2ToBezExtractor(subrs,
                                 t2CharString.globalSubrs,
                                 t2CharString.private.nominalWidthX,
                                 t2CharString.private.defaultWidthX,
                                 allowDecimals)
    extractor.execute(t2CharString)
    if extractor.gotWidth:
        t2Wdth = extractor.width - t2CharString.private.nominalWidthX
    else:
        t2Wdth = None
    return "".join(extractor.bezProgram), extractor.hintCount > 0, t2Wdth


class HintMask:
    # class used to collect hints for the current
    # hint mask when converting bez to T2.
    def __init__(self, listPos):
        # The index into the t2list is kept so we can quickly find them later.
        self.listPos = listPos
        # These contain the actual hint values.
        self.hList = []
        self.vList = []

    def maskByte(self, hHints, vHints):
        # return hintmask bytes for known hints.
        numHHints = len(hHints)
        numVHints = len(vHints)
        maskVal = 0
        byteIndex = 0
        self.byteLength = byteLength = int((7 + numHHints + numVHints) / 8)
        mask = b""
        self.hList.sort()
        for hint in self.hList:
            try:
                i = hHints.index(hint)
            except ValueError:
                # we get here if some hints have been dropped
                # because of the stack limit
                continue
            newbyteIndex = int(i / 8)
            if newbyteIndex != byteIndex:
                mask += bytechr(maskVal)
                byteIndex += 1
                while byteIndex < newbyteIndex:
                    mask += b"\0"
                    byteIndex += 1
                maskVal = 0
            maskVal += 2**(7 - (i % 8))

        self.vList.sort()
        for hint in self.vList:
            try:
                i = numHHints + vHints.index(hint)
            except ValueError:
                # we get here if some hints have been dropped
                # because of the stack limit
                continue
            newbyteIndex = int(i / 8)
            if newbyteIndex != byteIndex:
                mask += bytechr(maskVal)
                byteIndex += 1
                while byteIndex < newbyteIndex:
                    mask += b"\0"
                    byteIndex += 1
                maskVal = 0
            maskVal += 2**(7 - (i % 8))

        if maskVal:
            mask += bytechr(maskVal)

        if len(mask) < byteLength:
            mask += b"\0" * (byteLength - len(mask))
        self.mask = mask
        return mask


def makeHintList(hints, needHintMasks, isH):
    # Add the list of T2 tokens that make up the initial hint operators
    hintList = []
    lastPos = 0
    # In bez terms, the first coordinate in each pair is absolute,
    # second is relative.
    # In T2, each term is relative to the previous one.
    for hint in hints:
        if not hint:
            continue
        pos1 = hint[0]
        pos = pos1 - lastPos
        if isinstance(pos, float) and (int(pos) == pos):
            pos = int(pos)
        hintList.append(pos)
        pos2 = hint[1]
        if isinstance(pos2, float) and (int(pos2) == pos2):
            pos2 = int(pos2)
        lastPos = pos1 + pos2
        hintList.append(pos2)

    if needHintMasks:
        if isH:
            op = "hstemhm"
            hintList.append(op)
        # never need to append vstemhm: if we are using it, it is followed
        # by a mask command and vstemhm is inferred.
    else:
        if isH:
            op = "hstem"
        else:
            op = "vstem"
        hintList.append(op)

    return hintList


bezToT2 = {
    "mt": 'rmoveto',
    "rmt": 'rmoveto',
    "hmt": 'hmoveto',
    "vmt": 'vmoveto',
    "dt": 'rlineto',
    "rdt": 'rlineto',
    "hdt": 'hlineto',
    "vdt": 'vlineto',
    "ct": 'rrcurveto',
    "rct": 'rrcurveto',
    "rcv": 'rrcurveto',  # Morisawa's alternate name for 'rct'.
    "vhct": 'vhcurveto',
    "hvct": 'hvcurveto',
    "cp": '',
    "ed": 'endchar'
}


def optimizeT2Program(t2List):
    # Assumes T2 operands are in a list with one entry per operand, and each
    # entry is a list of [argList, opToken].
    # Matches logic in tx, and Adobe low level library.
    # Note that I am expecting only rlineto, vlineto, hlineto, vhcurveto,
    # hvcurveto, rrcurveto from AC.
    # The other optimized operators, rcurveline and rlinecurve are not
    # supported as input here.
    newT2List = []
    arglist = []
    kNoOp = "noop"
    pendingOp = kNoOp
    sequenceOp = kNoOp
    for entry in t2List:
        op = entry[1]
        args = entry[0]

        if op == "vlineto":
            dy = args[-1]
            if (pendingOp in ["vlineto", "hlineto"]) and (
               sequenceOp == "hlineto"):
                arglist.append(dy)
                sequenceOp = "vlineto"
                if len(arglist) >= kStackLimit:
                    newT2List.append([arglist[:-1], pendingOp])
                    arglist = [dy]
                    pendingOp = "vlineto"

            else:
                if pendingOp != kNoOp:
                    newT2List.append([arglist, pendingOp])
                arglist = [dy]
                pendingOp = sequenceOp = "vlineto"

        elif op == "hlineto":
            dx = args[-1]
            if (pendingOp in ["vlineto", "hlineto"]) and (
               sequenceOp == "vlineto"):
                arglist.append(dx)
                sequenceOp = "hlineto"
                if len(arglist) >= kStackLimit:
                    newT2List.append([arglist[:-1], pendingOp])
                    arglist = [dx]
                    pendingOp = "hlineto"
            else:
                if pendingOp != kNoOp:
                    newT2List.append([arglist, pendingOp])
                arglist = [dx]
                pendingOp = sequenceOp = "hlineto"

        elif op == "rlineto":
            dx = args[-2]
            dy = args[-1]
            if dx == 0:
                if (pendingOp in ["vlineto", "hlineto"]) and (
                   sequenceOp == "hlineto"):
                    arglist.append(dy)
                    sequenceOp = "vlineto"
                    if len(arglist) >= kStackLimit:
                        newT2List.append([arglist[:-1], pendingOp])
                        arglist = [dy]
                        pendingOp = "vlineto"
                else:
                    if pendingOp != kNoOp:
                        newT2List.append([arglist, pendingOp])
                    arglist = [dy]
                    pendingOp = sequenceOp = "vlineto"

            elif dy == 0:
                if (pendingOp in ["vlineto", "hlineto"]) and (
                   sequenceOp == "vlineto"):
                    arglist.append(dx)
                    sequenceOp = "hlineto"
                    if len(arglist) >= kStackLimit:
                        newT2List.append([arglist[:-1], pendingOp])
                        arglist = [dx]
                        pendingOp = "hlineto"
                else:
                    if pendingOp != kNoOp:
                        newT2List.append([arglist, pendingOp])
                    arglist = [dx]
                    pendingOp = sequenceOp = "hlineto"

            elif pendingOp == "rrcurveto":
                arglist.extend([dx, dy])

                if len(arglist) >= kStackLimit:
                    newT2List.append([arglist[:-2], pendingOp])
                    arglist = [dx, dy]
                    pendingOp = sequenceOp = "rlineto"
                else:
                    newT2List.append([arglist, "rcurveline"])
                    arglist = []
                    pendingOp = sequenceOp = kNoOp

            elif (pendingOp == op) and (sequenceOp == op):
                arglist.extend([dx, dy])
                if len(arglist) >= kStackLimit:
                    newT2List.append([arglist[:-2], pendingOp])
                    arglist = [dx, dy]

            else:
                if pendingOp != kNoOp:
                    newT2List.append([arglist, pendingOp])
                arglist = [dx, dy]
                pendingOp = sequenceOp = op

        elif op == "vhcurveto":
            if (pendingOp in ["vhcurveto", "hvcurveto"]) and (
               sequenceOp == "hvcurveto"):
                sequenceOp = "vhcurveto"
                arglist.extend(args)
                if len(arglist) >= kStackLimit:
                    newT2List.append([arglist[:-len(args)], pendingOp])
                    arglist = args
                    pendingOp = sequenceOp = op
            else:
                if pendingOp != kNoOp:
                    newT2List.append([arglist, pendingOp])
                arglist = args
                pendingOp = sequenceOp = "vhcurveto"
            if len(args) == 5:
                newT2List.append([arglist, pendingOp])
                arglist = []
                pendingOp = sequenceOp = kNoOp

        elif op == "hvcurveto":
            if (pendingOp in ["vhcurveto", "hvcurveto"]) and (
               sequenceOp == "vhcurveto"):
                sequenceOp = "hvcurveto"
                arglist.extend(args)
                if len(arglist) >= kStackLimit:
                    newT2List.append([arglist[:-len(args)], pendingOp])
                    arglist = args
                    pendingOp = sequenceOp = op
            else:
                if pendingOp != kNoOp:
                    newT2List.append([arglist, pendingOp])
                arglist = args
                pendingOp = sequenceOp = "hvcurveto"
            if len(args) == 5:
                newT2List.append([arglist, pendingOp])
                arglist = []
                pendingOp = sequenceOp = kNoOp

        elif op == "rrcurveto":
            dx1 = args[0]
            dy1 = args[1]
            dx2 = args[2]
            dy2 = args[3]
            dx3 = args[4]
            dy3 = args[5]

            if dx1 == 0:
                if dy3 == 0:  # - dy1 dx2 dy2 dx3 - vhcurveto
                    if (pendingOp in ["vhcurveto", "hvcurveto"]) and (
                       sequenceOp == "hvcurveto"):
                        arglist.extend([dy1, dx2, dy2, dx3])
                        sequenceOp = "vhcurveto"
                        if len(arglist) >= kStackLimit:
                            newT2List.append([arglist[:-4], pendingOp])
                            arglist = [dy1, dx2, dy2, dx3]
                            pendingOp = "vhcurveto"
                    else:
                        if pendingOp != kNoOp:
                            newT2List.append([arglist, pendingOp])
                        arglist = [dy1, dx2, dy2, dx3]
                        pendingOp = sequenceOp = "vhcurveto"

                elif dx3 == 0:  # - dy1 dx2 dy2 - dy3 vvcurveto
                    if pendingOp not in ["vvcurveto", kNoOp]:
                        newT2List.append([arglist, pendingOp])
                        arglist = []
                    arglist.extend([dy1, dx2, dy2, dy3])
                    sequenceOp = "vvcurveto"
                    if len(arglist) >= kStackLimit:
                        newT2List.append([arglist[:-4], pendingOp])
                        arglist = [dy1, dx2, dy2, dy3]
                        pendingOp = sequenceOp
                    else:
                        pendingOp = sequenceOp

                else:
                    # - dy1 dx2 dy2 dx3 dy3 vhcurveto
                    # (odd number of args, can't concatenate any more ops.)
                    if (pendingOp in ["vhcurveto", "hvcurveto"]) and (
                       sequenceOp == "hvcurveto"):
                        arglist.extend([dy1, dx2, dy2, dx3, dy3])
                        if len(arglist) >= kStackLimit:
                            newT2List.append([arglist[:-5], pendingOp])
                            arglist = [dy1, dx2, dy2, dx3, dy3]
                            pendingOp = "vhcurveto"
                    else:
                        if pendingOp != kNoOp:
                            newT2List.append([arglist, pendingOp])
                        arglist = [dy1, dx2, dy2, dx3, dy3]
                        pendingOp = "vhcurveto"
                    newT2List.append([arglist, pendingOp])
                    arglist = []
                    pendingOp = sequenceOp = kNoOp

            elif dy1 == 0:
                if dx3 == 0:  # dx1 - dx2 dy2 - dy3 hvcurveto
                    if (pendingOp in ["vhcurveto", "hvcurveto"]) and (
                       sequenceOp == "vhcurveto"):
                        arglist.extend([dx1, dx2, dy2, dy3])
                        sequenceOp = "hvcurveto"
                        if len(arglist) >= kStackLimit:
                            newT2List.append([arglist[:-4], pendingOp])
                            arglist = [dx1, dx2, dy2, dy3]
                            pendingOp = "hvcurveto"
                    else:
                        if pendingOp != kNoOp:
                            newT2List.append([arglist, pendingOp])
                        arglist = [dx1, dx2, dy2, dy3]
                        pendingOp = sequenceOp = "hvcurveto"

                elif dy3 == 0:  # dx1 - dx2 dy2 dx3 - hhcurveto
                    if pendingOp not in ["hhcurveto", kNoOp]:
                        newT2List.append([arglist, pendingOp])
                        arglist = []
                    arglist.extend([dx1, dx2, dy2, dx3])
                    sequenceOp = "hhcurveto"
                    if len(arglist) >= kStackLimit:
                        # XXX Problem. Was vvcurveto
                        newT2List.append([arglist[:-4], pendingOp])
                        arglist = [dx1, dx2, dy2, dx3]
                        pendingOp = sequenceOp
                    else:
                        pendingOp = sequenceOp

                else:
                    # dx1 - dx2 dy2 dy3 dx3 hvcurveto
                    # (odd number of args, can't concatenate any more ops.)
                    if (pendingOp in ["vhcurveto", "hvcurveto"]) and (
                       sequenceOp == "vhcurveto"):
                        arglist.extend([dx1, dx2, dy2, dy3, dx3])
                        if len(arglist) >= kStackLimit:
                            newT2List.append([arglist[:-5], pendingOp])
                            arglist = [dx1, dx2, dy2, dy3, dx3]
                            pendingOp = "hvcurveto"
                    else:
                        if pendingOp != kNoOp:
                            newT2List.append([arglist, pendingOp])
                        arglist = [dx1, dx2, dy2, dy3, dx3]
                        pendingOp = "hvcurveto"
                    newT2List.append([arglist, pendingOp])
                    arglist = []
                    pendingOp = sequenceOp = kNoOp

            elif dx3 == 0:  # dx1 dy1 dx2 dy2 - dy3 vvcurveto (odd args)
                    if pendingOp != kNoOp:
                        newT2List.append([arglist, pendingOp])
                        arglist = []
                    arglist = [dx1, dy1, dx2, dy2, dy3]
                    pendingOp = "vvcurveto"
                    newT2List.append([arglist, pendingOp])
                    arglist = []
                    pendingOp = sequenceOp = kNoOp

            elif dy3 == 0:  # dx1 dy1 dx2 dy2 dx3 - hhcurveto (odd args)
                    if pendingOp != kNoOp:
                        newT2List.append([arglist, pendingOp])
                        arglist = []
                    arglist = [dy1, dx1, dx2, dy2, dx3]  # note arg order swap
                    pendingOp = "hhcurveto"
                    newT2List.append([arglist, pendingOp])
                    arglist = []
                    pendingOp = sequenceOp = kNoOp

            else:
                if pendingOp == "rlineto":
                    arglist.extend(args)
                    if len(arglist) >= kStackLimit:
                        newT2List.append([arglist[:-len(args)], pendingOp])
                        arglist = args
                        pendingOp = sequenceOp = op
                    else:
                        newT2List.append([arglist, "rlinecurve"])
                        arglist = []
                        pendingOp = sequenceOp = kNoOp

                else:
                    if pendingOp not in [kNoOp, "rrcurveto"]:
                        newT2List.append([arglist, pendingOp])
                        arglist = []
                    arglist.extend(args)
                    if len(arglist) >= kStackLimit:
                        newT2List.append([arglist[:-len(args)], pendingOp])
                        arglist = args
                    pendingOp = sequenceOp = op

        elif op == "flex":
            dx1 = args[0]
            dy1 = args[1]
            dx2 = args[2]
            dy2 = args[3]
            dx3 = args[4]
            dy3 = args[5]
            dx4 = args[6]
            dy4 = args[7]
            dx5 = args[8]
            dy5 = args[9]
            dx6 = args[10]
            dy6 = args[11]
            if pendingOp != kNoOp:
                newT2List.append([arglist, pendingOp])
                arglist = []
            noFlex = True
            if (dy3 == 0 == dy4):
                if (dy1 == dy6 == 0) and (dy2 == -dy5):
                    # the device pixel threshold is always 50,
                    # when coming back from AC.
                    newT2List.append(
                        [[dx1, dx2, dy2, dx3, dx4, dx5, dx6], "hflex"])
                    noFlex = False
                else:
                    dy = dy1 + dy2 + dy3 + dy4 + dy5 + dy6
                    if dy == 0:
                        newT2List.append(
                            [[dx1, dy1, dx2, dy2, dx3, dx4, dx5, dy5, dx6],
                             "hflex1"])
                        noFlex = False

            if noFlex:
                if 0:
                    dx = dx1 + dx2 + dx3 + dx4 + dx5
                    dy = dy1 + dy2 + dy3 + dy4 + dy5

                    if ((dy + dy6) == 0) or ((dx + dx6) == 0):
                        if abs(dx) > abs(dy):
                            lastArg = dx6
                        else:
                            lastArg = dy6

                        newT2List.append([args[:10] + [lastArg], "flex1"])
                    else:
                        newT2List.append([args, "flex"])
                else:
                    newT2List.append([args, "flex"])

            arglist = []
            pendingOp = sequenceOp = kNoOp

        else:
            if pendingOp != kNoOp:
                newT2List.append([arglist, pendingOp])
            newT2List.append([args, op])
            arglist = []
            pendingOp = sequenceOp = kNoOp

    if pendingOp != kNoOp:
        newT2List.append([arglist, pendingOp])

    return newT2List


kHintArgsNoOverlap = 0
kHintArgsOverLap = 1
kHintArgsMatch = 2


def checkStem3ArgsOverlap(argList, hintList):
    # status == 0 -> no overlap
    # status == 1 -> arg are the same
    # status = 2 -> args overlap, and are not the same
    status = kHintArgsNoOverlap
    for x0, x1 in argList:
        x1 = x0 + x1
        for y0, y1 in hintList:
            y1 = y0 + y1
            if (x0 == y0):
                if (x1 == y1):
                    status = kHintArgsMatch
                else:
                    return kHintArgsOverLap
            elif (x1 == y1):
                return kHintArgsOverLap
            else:
                if (x0 > y0) and (x0 < y1):
                    return kHintArgsOverLap
                if (x1 > y0) and (x1 < y1):
                    return kHintArgsOverLap
    return status


def buildControlMaskList(hStem3List, vStem3List):
    """
    The deal is that a charstring will use either counter hints, or stem 3
    hints, but not both. We examine all the arglists. If any are not a
    multiple of 3, then we use all the arglists as is as the args to a
    counter hint. If all are a multiple of 3, then we divide them up into
    triplets, and add a separate conter mask for each unique arg set.
    """

    vControlMask = HintMask(0)
    hControlMask = vControlMask
    controlMaskList = [hControlMask]
    for argList in hStem3List:
        for mask in controlMaskList:
            overlapStatus = kHintArgsNoOverlap
            if not mask.hList:
                mask.hList.extend(argList)
                overlapStatus = kHintArgsMatch
                break
            overlapStatus = checkStem3ArgsOverlap(argList, mask.hList)
            # The args match args in this control mask.
            if overlapStatus == kHintArgsMatch:
                break
        if overlapStatus != kHintArgsMatch:
            mask = HintMask(0)
            controlMaskList.append(mask)
            mask.hList.extend(argList)

    for argList in vStem3List:
        for mask in controlMaskList:
            overlapStatus = kHintArgsNoOverlap
            if not mask.vList:
                mask.vList.extend(argList)
                overlapStatus = kHintArgsMatch
                break
            overlapStatus = checkStem3ArgsOverlap(argList, mask.vList)
            # The args match args in this control mask.
            if overlapStatus == kHintArgsMatch:
                break
        if overlapStatus != kHintArgsMatch:
            mask = HintMask(0)
            controlMaskList.append(mask)
            mask.vList.extend(argList)

    return controlMaskList


def makeRelativeCTArgs(argList, curX, curY):
    newCurX = argList[4]
    newCurY = argList[5]
    argList[5] -= argList[3]
    argList[4] -= argList[2]

    argList[3] -= argList[1]
    argList[2] -= argList[0]

    argList[0] -= curX
    argList[1] -= curY
    return argList, newCurX, newCurY


def convertBezToT2(bezString):
    # convert bez data to a T2 outline program, a list of operator tokens.
    #
    # Convert all bez ops to simplest T2 equivalent.
    # Add all hints to vertical and horizontal hint lists as encountered.
    # Insert a HintMask class whenever a new set of hints is encountered.
    # After all operators have been processed, convert HintMask items into
    # hintmask ops and hintmask bytes.
    # Add all hints as prefix
    # Review operator list to optimize T2 operators.

    bezString = re.sub(r"%.+?\n", "", bezString)  # suppress comments
    bezList = re.findall(r"(\S+)", bezString)
    if not bezList:
        return ""
    hhints = []
    vhints = []
    hintMask = HintMask(0)  # Always assume a hint mask until proven otherwise.
    hintMaskList = [hintMask]
    vStem3Args = []
    hStem3Args = []
    vStem3List = []
    hStem3List = []
    argList = []
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
                    argList.append(val2)
                else:
                    argList.append("%s 100 div" % str(int(val1 * 100)))
            except ValueError:
                argList.append(val1)
            continue
        except ValueError:
            pass

        if token == "newcolors":
            lastPathOp = token
        elif token in ["beginsubr", "endsubr"]:
            lastPathOp = token
        elif token in ["snc"]:
            lastPathOp = token
            # The index into the t2list is kept
            # so we can quickly find them later.
            hintMask = HintMask(len(t2List))
            t2List.append([hintMask])
            hintMaskList.append(hintMask)
        elif token in ["enc"]:
            lastPathOp = token
        elif token == "div":
            # i specifically do NOT set lastPathOp for this.
            value = argList[-2] / float(argList[-1])
            argList[-2:] = [value]
        elif token == "rb":
            lastPathOp = token
            try:
                i = hhints.index(argList)
            except ValueError:
                i = len(hhints)
                hhints.append(argList)
            if hintMask:
                if hhints[i] not in hintMask.hList:
                    hintMask.hList.append(hhints[i])
            argList = []
        elif token == "ry":
            lastPathOp = token
            try:
                i = vhints.index(argList)
            except ValueError:
                i = len(vhints)
                vhints.append(argList)
            if hintMask:
                if vhints[i] not in hintMask.vList:
                    hintMask.vList.append(vhints[i])
            argList = []

        elif token == "rm":  # vstem3 hints are vhints
            try:
                i = vhints.index(argList)
            except ValueError:
                i = len(vhints)
                vhints.append(argList)
            if hintMask:
                if vhints[i] not in hintMask.vList:
                    hintMask.vList.append(vhints[i])

            if (lastPathOp != token) and vStem3Args:
                # first rm, must be start of a new vstem3
                # if we already have a set of vstems in vStem3Args, save them,
                # and then clear the vStem3Args so we can add the new set.
                vStem3List.append(vStem3Args)
                vStem3Args = []

            vStem3Args.append(argList)
            argList = []
            lastPathOp = token
        elif token == "rv":  # hstem3 are hhints
            try:
                i = hhints.index(argList)
            except ValueError:
                i = len(hhints)
                hhints.append(argList)
            if hintMask:
                if hhints[i] not in hintMask.hList:
                    hintMask.hList.append(hhints[i])

            if (lastPathOp != token) and hStem3Args:
                # first rv, must be start of a new h countermask
                hStem3List.append(hStem3Args)
                hStem3Args = []

            hStem3Args.append(argList)
            argList = []
            lastPathOp = token
        elif token == "preflx1":
            # The preflx1/preflx2 sequence provides the same 'i' as the flex
            # sequence. The difference is that the preflx1/preflx2 sequence
            # provides the argument values needed for building a Type1 string
            # while the flex sequence is simply the 6 rrcurveto points.
            # Both sequences are always provided.
            lastPathOp = token
            argList = []
        elif token in ["preflx2", "preflx2a"]:
            lastPathOp = token
            del t2List[-1]
            argList = []
        elif token == "flx":
            lastPathOp = token
            argList = argList[:12]
            t2List.append([argList + [50], "flex"])
            argList = []
        elif token == "flxa":
            lastPathOp = token
            argList1, curX, curY = makeRelativeCTArgs(argList[:6], curX, curY)
            argList2, curX, curY = makeRelativeCTArgs(argList[6:], curX, curY)
            argList = argList1 + argList2
            t2List.append([argList[:12] + [50], "flex"])
            argList = []
        elif token == "sc":
            lastPathOp = token
        else:
            if token[-2:] in ["mt", "dt", "ct", "cv"]:
                lastPathOp = token
            t2Op = bezToT2.get(token, None)
            if token in ["mt", "dt"]:
                newList = [argList[0] - curX, argList[1] - curY]
                curX = argList[0]
                curY = argList[1]
                argList = newList
            elif token in ["ct", "cv"]:
                argList, curX, curY = makeRelativeCTArgs(argList, curX, curY)
            if t2Op:
                t2List.append([argList, t2Op])
            elif t2Op is None:
                print("Unhandled operation", argList, token)
                raise KeyError
            argList = []

    # Add hints, if any. Must be done at the end of op processing to make sure
    # we have seen all the hints in the bez string. Note that the hintmask are
    # identified in the t2List by an index into the list; be careful NOT to
    # change the t2List length until the hintmasks have been converted.
    numHintMasks = len(hintMaskList)
    needHintMasks = numHintMasks > 1

    if vStem3Args:
        vStem3List.append(vStem3Args)
    if hStem3Args:
        hStem3List.append(hStem3Args)

    t2Program = []
    hhints.sort()
    vhints.sort()
    numHHints = len(hhints)
    numVHints = len(vhints)
    hintLimit = int((kStackLimit - 2) / 2)
    if numHHints >= hintLimit:
        hhints = hhints[:hintLimit]
        numHHints = hintLimit
    if numVHints >= hintLimit:
        vhints = vhints[:hintLimit]
        numVHints = hintLimit
    if hhints:
        isH = 1
        t2Program = makeHintList(hhints, needHintMasks, isH)
    if vhints:
        isH = 0
        t2Program += makeHintList(vhints, needHintMasks, isH)

    if vStem3List or hStem3List:
        controlMaskList = buildControlMaskList(hStem3List, vStem3List)
        for cMask in controlMaskList:
            hBytes = cMask.maskByte(hhints, vhints)
            t2Program.extend(["cntrmask", hBytes])

    if needHintMasks:
        # If there is not a hintsub before any drawing operators, then
        # add an initial first hint mask to the t2Program.
        if hintMaskList[1].listPos != 0:
            hBytes = hintMaskList[0].maskByte(hhints, vhints)
            t2Program.extend(["hintmask", hBytes])

        # Convert the rest of the hint masks
        # to a hintmask op and hintmask bytes.
        for hintMask in hintMaskList[1:]:
            pos = hintMask.listPos
            t2List[pos] = [["hintmask"], hintMask.maskByte(hhints, vhints)]

    t2List = optimizeT2Program(t2List)

    for entry in t2List:
        try:
            t2Program.extend(entry[0])
            t2Program.append(entry[1])
        except Exception:
            print("Failed to extend t2Program with entry", entry)
            raise KeyError

    return t2Program


class CFFFontData:
    def __init__(self, ttFont, inputPath, outFilePath, allow_decimal_coords,
                 font_format, logMsgCB):
        self.ttFont = ttFont
        self.inputPath = inputPath
        if (outFilePath is None):
            outFilePath = inputPath
        self.outFilePath = outFilePath
        self.font_format = font_format
        self.logMsg = logMsgCB
        try:
            self.cffTable = ttFont["CFF "]
            topDict = self.cffTable.cff.topDictIndex[0]
        except KeyError:
            raise focusFontError(
                "Error: font is not a CFF font <%s>." % fontFileName)

        # for identifier in glyph-list:
        # Get charstring.
        self.topDict = topDict
        self.charStrings = topDict.CharStrings
        self.charStringIndex = self.charStrings.charStringsIndex
        self.allowDecimalCoords = allow_decimal_coords

    def getGlyphList(self):
        fontGlyphList = self.ttFont.getGlyphOrder()
        return fontGlyphList

    def getUnitsPerEm(self):
        unitsPerEm = "1000"
        if hasattr(self.topDict, "FontMatrix"):
            matrix = self.topDict.FontMatrix
            unitsPerEm = "%s" % (int(round(1.0 / matrix[0])))
        return unitsPerEm

    def getPSName(self):
        psName = self.cffTable.cff.fontNames[0]
        return psName

    def convertToBez(self, glyphName, beVerbose, doAll=False):
        hasHints = False
        t2Wdth = None
        gid = self.charStrings.charStrings[glyphName]
        t2CharString = self.charStringIndex[gid]
        try:
            bezString, hasHints, t2Wdth = convertT2GlyphToBez(
                t2CharString, self.allowDecimalCoords)
            # Note: the glyph name is important, as it is used by autohintexe
            # for various heuristics, including [hv]stem3 derivation.
            bezString = "% " + glyphName + "\n" + bezString
        except SEACError:
            if not beVerbose:
                # end series of "."
                self.logMsg("")
                # output message when SEAC glyph is found
                self.logMsg("Checking %s -- ," % glyphName)
            self.logMsg("Skipping %s: can't process SEAC composite glyphs." %
                        glyphName)
            bezString = None
        return bezString, t2Wdth, hasHints

    def updateFromBez(self, bezData, glyphName, width, beVerbose):
        t2Program = [width] + convertBezToT2(bezData)
        if t2Program:
            gid = self.charStrings.charStrings[glyphName]
            t2CharString = self.charStringIndex[gid]
            t2CharString.program = t2Program
        else:
            if not beVerbose:
                # end series of "."
                self.logMsg("")
                self.logMsg("Checking %s -- ," % aliasName(name))
            self.logMsg("Skipping %s: error in processing fixed outline." %
                        aliasName(name))

    def saveChanges(self):
        ttFont = self.ttFont
        font_format = self.font_format
        inputPath = self.inputPath
        outFilePath = self.outFilePath

        overwriteOriginal = False
        if inputPath == outFilePath:
            overwriteOriginal = True
        tempPath = inputPath + ".temp.ac"

        if font_format == "OTF":
            if overwriteOriginal:
                ttFont.save(tempPath)
                ttFont.close()
                if os.path.exists(inputPath):
                    try:
                        os.remove(inputPath)
                        os.rename(tempPath, inputPath)
                    except (OSError, IOError):
                        self.logMsg(
                            "Error: could not overwrite original font file "
                            "path '%s'. Hinted font file path is '%s'." %
                            (inputPath, tempPath))
                        raise
            else:
                ttFont.save(outFilePath)
                ttFont.close()

        else:
            data = ttFont["CFF "].compile(ttFont)
            if font_format == "CFF":
                if overwriteOriginal:
                    with open(inputPath, "wb") as tf:
                        tf.write(data)
                else:
                    with open(outFilePath, "wb") as tf:
                        tf.write(data)

            if os.path.exists(tempPath):
                os.remove(tempPath)

    def close(self):
        self.ttFont.close()

    def getGlyphID(self, name):
        gid = self.ttFont.getGlyphID(name)
        return gid

    def isCID(self):
        isCID = hasattr(self.topDict, "FDSelect")
        return isCID

    def getFontInfo(self, fontPSName, inputPath, allow_no_blues, noFlex,
                    vCounterGlyphs, hCounterGlyphs, fdIndex=0):
        # The AC library needs the global font hint zones
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
        if hasattr(privateDict, "LanguageGroup"):
            fdDict.LanguageGroup = privateDict.LanguageGroup
        else:
            fdDict.LanguageGroup = "0"

        if hasattr(pDict, "FontMatrix"):
            fdDict.FontMatrix = pDict.FontMatrix
        else:
            fdDict.FontMatrix = pTopDict.FontMatrix
        upm = int(1 / fdDict.FontMatrix[0])
        fdDict.OrigEmSqUnits = str(upm)

        if hasattr(pTopDict, "FontName"):
            fdDict.FontName = pDict.FontName  # FontName
        else:
            fdDict.FontName = fontPSName

        low = min(-upm * 0.25, pTopDict.FontBBox[1] - 200)
        high = max(upm * 1.25, pTopDict.FontBBox[3] + 200)
        # Make a set of inactive alignment zones: zones outside of the
        # font BBox so as not to affect hinting. Used when source font has
        # no BlueValues or has invalid BlueValues. Some fonts have bad BBox
        # values, so I don't let this be smaller than -upm*0.25, upm*1.25.
        inactiveAlignmentValues = [low, low, high, high]
        if hasattr(privateDict, "BlueValues"):
            blueValues = privateDict.BlueValues[:]
            numBlueValues = len(privateDict.BlueValues)
            blueValues.sort()
            if numBlueValues < 4:
                if allow_no_blues:
                    blueValues = inactiveAlignmentValues
                    numBlueValues = len(blueValues)
                else:
                    raise ACFontError(
                        "Error: font must have at least four values in it's "
                        "BlueValues array for AC to work!")
        else:
            if allow_no_blues:
                blueValues = inactiveAlignmentValues
                numBlueValues = len(blueValues)
            else:
                raise ACFontError("Error: font has no BlueValues array!")

        # The first pair only is a bottom zone, where the first value is the
        # overshoot position. The rest are top zones, and second value of the
        # pair is the overshoot position.
        blueValues[0] = blueValues[0] - blueValues[1]
        for i in range(3, numBlueValues, 2):
            blueValues[i] = blueValues[i] - blueValues[i - 1]

        blueValues = [str(v) for v in blueValues]
        numBlueValues = min(numBlueValues, len(fdTools.kBlueValueKeys))
        for i in range(numBlueValues):
            key = fdTools.kBlueValueKeys[i]
            value = blueValues[i]
            setattr(fdDict, key, value)

        # print(numBlueValues)
        # for i in range(0, len(fontinfo),2):
        #    print(fontinfo[i], fontinfo[i+1])

        if hasattr(privateDict, "OtherBlues"):
            # For all OtherBlues, the pairs are bottom zones, and
            # the first value of each pair is the overshoot position.
            i = 0
            numBlueValues = len(privateDict.OtherBlues)
            blueValues = privateDict.OtherBlues[:]
            blueValues.sort()
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
                raise ACFontError(
                    "Error: font has neither StemSnapV nor StdVW!")
        vstems.sort()
        if (len(vstems) == 0) or ((len(vstems) == 1) and (vstems[0] < 1)):
            vstems = [upm]  # dummy value that will allow PyAC to run
            self.logMsg("Warning: There is no value or 0 value for DominantV.")
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
                raise ACFontError(
                    "Error: font has neither StemSnapH nor StdHW!")
        hstems.sort()
        if (len(hstems) == 0) or ((len(hstems) == 1) and (hstems[0] < 1)):
            hstems = [upm]  # dummy value that will allow PyAC to run
            self.logMsg("Warning: There is no value or 0 value for DominantH.")
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

        if hasattr(privateDict, "BlueFuzz"):
            fdDict.BlueFuzz = privateDict.BlueFuzz
        else:
            fdDict.BlueFuzz = 1

        return fdDict

    def getfdIndex(self, gid):
        return self.topDict.FDSelect[gid]

    def getfdInfo(self, fontPSName, inputPath, allow_no_blues, noFlex,
                  vCounterGlyphs, hCounterGlyphs, glyphList, fdIndex=0):
        topDict = self.topDict
        fontDictList = []
        fdGlyphDict = None

        # Get the default fontinfo from the font's top dict.
        fdDict = self.getFontInfo(
            fontPSName, inputPath, allow_no_blues, noFlex, vCounterGlyphs,
            hCounterGlyphs, fdIndex)
        fontDictList.append(fdDict)

        # Check the fontinfo file, and add any other font dicts
        srcFontInfo = os.path.dirname(inputPath)
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
            blueFuzz = fdDict.BlueFuzz
            fdGlyphDict, fontDictList, finalFDict = fdTools.parseFontInfoFile(
                fontDictList, fontInfoData, glyphList, maxY, minY, fontPSName,
                blueFuzz)
            if finalFDict is None:
                # If a font dict was not explicitly specified for the
                # output font, use the first user-specified font dict.
                fdTools.mergeFDDicts(fontDictList[1:], topDict.Private)
            else:
                fdTools.mergeFDDicts([finalFDict], topDict.Private)
        return fdGlyphDict, fontDictList


def test():
    # Test program.
    # Takes first argument font file path,
    # optional second argument = glyph name.
    # Use form "cid0769" for CID keys references.
    from fontTools.ttLib import TTFont
    path = sys.argv[1]
    ttFont = TTFont(path)
    if len(sys.argv) > 2:
        glyphNames = sys.argv[2:]
    else:
        glyphNames = ttFont.getGlyphOrder()
    cffTable = ttFont["CFF "]
    topDict = cffTable.cff.topDictIndex[0]
    charStrings = topDict.CharStrings

    for glyphName in glyphNames:
        print()
        print(glyphName)
        t2CharString = charStrings[glyphName]
        bezString, _, t2Width = convertT2GlyphToBez(t2CharString)
        # print(bezString)
        t2Program = convertBezToT2(bezString)
        if t2Width is not None:
            t2Program.insert(0, t2Width)

        # print(len(t2Program), ("t2Program", t2Program))


def test2():
    # Test program.
    # Takes first argument = bez path, writes T2 string.
    # Use form "cid0769" for CID keys references.
    path = sys.argv[1]
    with open(path, "rt", encoding="utf-8") as fp:
        bezString = fp.read()

    convertBezToT2(bezString)


if __name__ == '__main__':
    test2()
