# Copyright 2014 Adobe. All rights reserved.

"""
Utilities for converting between T2 charstrings and the bez data format.
"""

from __future__ import print_function, absolute_import

import logging
import os
import re
import subprocess
import tempfile

from fontTools.misc.psCharStrings import T2OutlineExtractor, SimpleT2Decompiler
from fontTools.misc.py23 import bytechr, byteord, open
from fontTools.ttLib import TTFont, newTable

from . import fdTools, FontParseError


log = logging.getLogger(__name__)

kStackLimit = 46
kStemLimit = 96


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

    def updateHints(self, args, hintList, bezCommand):
        self.countHints(args)

        # first hint value is absolute hint coordinate, second is hint width
        if not self.read_hints:
            return

        lastval = args[0]
        arg = str(lastval)
        hintList.append(arg)
        self.bezProgram.append(arg + " ")

        for i in range(len(args))[1:]:
            val = args[i]
            lastval += val

            if i % 2:
                arg = str(val)
                hintList.append(arg)
                self.bezProgram.append("%s %s\n" % (arg, bezCommand))
            else:
                arg = str(lastval)
                hintList.append(arg)
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
            mask = [strout + hex(byteord(ch)) for ch in self.hintMaskString]
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
    width = None
    if extractor.gotWidth:
        width = extractor.width - t2CharString.private.nominalWidthX
    return "".join(extractor.bezProgram), width


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
        if pos % 1 == 0:
            pos = int(pos)
        hintList.append(pos)
        pos2 = hint[1]
        if pos2 % 1 == 0:
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
    "dt": 'rlineto',
    "ct": 'rrcurveto',
    "cp": '',
    "ed": 'endchar'
}


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
                    argList.append("%s 100 div" % int(val1 * 100))
            except ValueError:
                argList.append(val1)
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
            hintMask = HintMask(len(t2List))
            t2List.append([hintMask])
            hintMaskList.append(hintMask)
        elif token == "enc":
            lastPathOp = token
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
            # The preflx1/preflx2a sequence provides the same 'i' as the flex
            # sequence. The difference is that the preflx1/preflx2a sequence
            # provides the argument values needed for building a Type1 string
            # while the flex sequence is simply the 6 rrcurveto points.
            # Both sequences are always provided.
            lastPathOp = token
            argList = []
        elif token == "preflx2a":
            lastPathOp = token
            del t2List[-1]
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
            if token in ["rmt", "mt", "dt", "ct"]:
                lastPathOp = token
            t2Op = bezToT2.get(token, None)
            if token in ["mt", "dt"]:
                newList = [argList[0] - curX, argList[1] - curY]
                curX = argList[0]
                curY = argList[1]
                argList = newList
            elif token == "ct":
                argList, curX, curY = makeRelativeCTArgs(argList, curX, curY)
            if t2Op:
                t2List.append([argList, t2Op])
            elif t2Op is None:
                raise KeyError("Unhandled operation %s %s" % (argList, token))
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
        t2Program = makeHintList(hhints, needHintMasks, isH=True)
    if vhints:
        t2Program += makeHintList(vhints, needHintMasks, isH=False)

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

    for entry in t2List:
        try:
            t2Program.extend(entry[0])
            t2Program.append(entry[1])
        except Exception:
            raise KeyError("Failed to extend t2Program with entry %s" % entry)

    return t2Program


def _run_tx(args):
    try:
        subprocess.check_call(["tx"] + args)
    except (subprocess.CalledProcessError, OSError) as e:
        raise FontParseError(e)


class CFFFontData:
    def __init__(self, path, font_format):
        self.inputPath = path
        self.font_format = font_format

        if font_format == "OTF":
            # It is an OTF font, we can process it directly.
            font = TTFont(path)
            if "CFF " not in font:
                raise FontParseError("OTF font has no CFF table <%s>." % path)
        else:
            # Else, package it in an OTF font.
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
        self.cffTable = font["CFF "]

        # for identifier in glyph-list:
        # Get charstring.
        self.topDict = self.cffTable.cff.topDictIndex[0]
        self.charStrings = self.topDict.CharStrings

    def getGlyphList(self):
        return self.ttFont.getGlyphOrder()

    def getPSName(self):
        return self.cffTable.cff.fontNames[0]

    def convertToBez(self, glyphName, read_hints, round_coords, doAll=False):
        t2Wdth = None
        t2CharString = self.charStrings[glyphName]
        try:
            bezString, t2Wdth = convertT2GlyphToBez(t2CharString, read_hints,
                                                    round_coords)
            # Note: the glyph name is important, as it is used by autohintexe
            # for various heuristics, including [hv]stem3 derivation.
            bezString = "% " + glyphName + "\n" + bezString
        except SEACError:
            log.warning("Skipping %s: can't process SEAC composite glyphs.",
                        glyphName)
            bezString = None
        return bezString, t2Wdth

    def updateFromBez(self, bezData, glyphName, width):
        t2Program = [width] + convertBezToT2(bezData)
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

        low = min(-upm * 0.25, pTopDict.FontBBox[1] - 200)
        high = max(upm * 1.25, pTopDict.FontBBox[3] + 200)
        # Make a set of inactive alignment zones: zones outside of the
        # font BBox so as not to affect hinting. Used when source font has
        # no BlueValues or has invalid BlueValues. Some fonts have bad BBox
        # values, so I don't let this be smaller than -upm*0.25, upm*1.25.
        inactiveAlignmentValues = [low, low, high, high]
        blueValues = getattr(privateDict, "BlueValues", [])[:]
        numBlueValues = len(blueValues)
        if numBlueValues < 4:
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
        return self.topDict.FDSelect[gid]

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
