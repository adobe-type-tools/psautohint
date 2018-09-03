# Copyright 2016 Adobe. All rights reserved.

# Methods:

# Parse args. If glyphlist is from file, read in entire file as single string,
# and remove all white space, then parse out glyph-names and GID's.

# For each font name:
#   Use fontTools library to open font and extract CFF table.
#   If error, skip font and report error.
#   Filter specified glyph list, if any, with list of glyphs in the font.
#   Open font plist file, if any. If not, create empty font plist.
#   Build alignment zone string
#   For identifier in glyph-list:
#     Get T2 charstring for glyph from parent font CFF table. If not present,
#       report and skip.
#     Get new alignment zone string if FDarray index (which font dict is used)
#       has changed.
#     Convert to bez
#     Build autohint point list string; this is used to tell if glyph has been
#       changed since the last time it was hinted.
#     If requested, check against plist dict, and skip if glyph is already
#       hinted or is manually hinted.
#     Call autohint library on bez string.
#     If change to the point list is permitted and happened, rebuild.
#     Autohint point list string.
#     Convert bez string to T2 charstring, and update parent font CFF.
#     Add glyph hint entry to plist file
#  Save font plist file.

from __future__ import print_function, absolute_import

import logging
import os
import re
import time

from .otfFont import CFFFontData
from .ufoFont import UFOFontData
from ._psautohint import error as PsAutoHintCError

from . import (get_font_format, hint_bez_glyph, hint_compatible_bez_glyphs,
               FontParseError)


log = logging.getLogger(__name__)


class ACOptions(object):
    def __init__(self):
        self.inputPaths = []
        self.outputPaths = []
        self.reference_font = None
        self.glyphList = []
        self.nameAliases = {}
        self.excludeGlyphList = False
        self.hintAll = False
        self.read_hints = False
        self.allowChanges = False
        self.noFlex = False
        self.noHintSub = False
        self.allow_no_blues = False
        self.hCounterGlyphs = []
        self.vCounterGlyphs = []
        self.logOnly = False
        self.printDefaultFDDict = False
        self.printFDDictList = False
        self.round_coords = True
        self.writeToDefaultLayer = False
        self.baseMaster = {}
        self.font_format = None


class ACHintError(Exception):
    pass


def getGlyphID(glyphTag, fontGlyphList):
    if glyphTag in fontGlyphList:
        return fontGlyphList.index(glyphTag)

    return None


def getGlyphNames(glyphTag, fontGlyphList, fontFileName):
    glyphNameList = []
    rangeList = glyphTag.split("-")
    prevGID = getGlyphID(rangeList[0], fontGlyphList)
    if prevGID is None:
        if len(rangeList) > 1:
            log.warning("glyph ID <%s> in range %s from glyph selection "
                        "list option is not in font. <%s>.",
                        rangeList[0], glyphTag, fontFileName)
        else:
            log.warning("glyph ID <%s> from glyph selection list option "
                        "is not in font. <%s>.", rangeList[0], fontFileName)
        return None
    glyphNameList.append(fontGlyphList[prevGID])

    for glyphTag2 in rangeList[1:]:
        gid = getGlyphID(glyphTag2, fontGlyphList)
        if gid is None:
            log.warning("glyph ID <%s> in range %s from glyph selection "
                        "list option is not in font. <%s>.",
                        glyphTag2, glyphTag, fontFileName)
            return None
        for i in range(prevGID + 1, gid + 1):
            glyphNameList.append(fontGlyphList[i])
        prevGID = gid

    return glyphNameList


def filterGlyphList(options, fontGlyphList, fontFileName):
    # Return the list of glyphs which are in the intersection of the argument
    # list and the glyphs in the font.
    # Complain about glyphs in the argument list which are not in the font.
    if not options.glyphList:
        glyphList = fontGlyphList
    else:
        # expand ranges:
        glyphList = []
        for glyphTag in options.glyphList:
            glyphNames = getGlyphNames(glyphTag, fontGlyphList, fontFileName)
            if glyphNames is not None:
                glyphList.extend(glyphNames)
        if options.excludeGlyphList:
            newList = filter(lambda name: name not in glyphList, fontGlyphList)
            glyphList = newList
    return glyphList


fontInfoKeywordList = [
    'FontName',  # string
    'OrigEmSqUnits',
    'LanguageGroup',
    'DominantV',  # array
    'DominantH',  # array
    'FlexOK',  # string
    'BlueFuzz',
    'VCounterChars',  # counter
    'HCounterChars',  # counter
    'BaselineYCoord',
    'BaselineOvershoot',
    'CapHeight',
    'CapOvershoot',
    'LcHeight',
    'LcOvershoot',
    'AscenderHeight',
    'AscenderOvershoot',
    'FigHeight',
    'FigOvershoot',
    'Height5',
    'Height5Overshoot',
    'Height6',
    'Height6Overshoot',
    'DescenderOvershoot',
    'DescenderHeight',
    'SuperiorOvershoot',
    'SuperiorBaseline',
    'OrdinalOvershoot',
    'OrdinalBaseline',
    'Baseline5Overshoot',
    'Baseline5',
    'Baseline6Overshoot',
    'Baseline6',
]

integerPattern = """ -?\d+"""
arrayPattern = """ \[[ ,0-9]+\]"""
stringPattern = """ \S+"""
counterPattern = """ \([\S ]+\)"""


def printFontInfo(fontInfoString):
    for item in fontInfoKeywordList:
        if item in ['FontName', 'FlexOK']:
            matchingExp = item + stringPattern
        elif item in ['VCounterChars', 'HCounterChars']:
            matchingExp = item + counterPattern
        elif item in ['DominantV', 'DominantH']:
            matchingExp = item + arrayPattern
        else:
            matchingExp = item + integerPattern

        try:
            print('\t%s' % re.search(matchingExp, fontInfoString).group())
        except Exception:
            pass


def openFile(path, options):
    font_format = get_font_format(path)
    if font_format is None:
        raise FontParseError("{} is not a supported font format".format(path))

    if font_format == "UFO":
        font = UFOFontData(path, options.logOnly, options.writeToDefaultLayer)
    else:
        font = CFFFontData(path, font_format)

    return font


def hintFiles(options):
    if options.reference_font:
        hintFile(options, options.reference_font, None, reference_master=True)
    for i, path in enumerate(options.inputPaths):
        outpath = None
        if options.outputPaths is not None and i < len(options.outputPaths):
            outpath = options.outputPaths[i]
        hintFile(options, path, outpath, reference_master=False)


def hintFile(options, path, outpath, reference_master):
    nameAliases = options.nameAliases

    fontFileName = os.path.basename(path)
    log.info("Hinting font %s. Start time: %s.", path, time.asctime())

    fontData = openFile(path, options)

    # filter specified list, if any, with font list.
    fontGlyphList = fontData.getGlyphList()
    glyphList = filterGlyphList(options, fontGlyphList, fontFileName)
    if not glyphList:
        raise FontParseError("Selected glyph list is empty for font <%s>." %
                             fontFileName)

    fontInfo = ""

    # Check counter glyphs, if any.
    counter_glyphs = options.hCounterGlyphs + options.vCounterGlyphs
    if counter_glyphs:
        missing = [n for n in counter_glyphs if n not in fontGlyphList]
        if missing:
            log.error("H/VCounterChars glyph named in fontinfo is "
                      "not in font: %s", missing)

    # Build alignment zone string
    if options.printDefaultFDDict:
        print("Showing default FDDict Values:")
        fdDict = fontData.getFontInfo(options.allow_no_blues,
                                      options.noFlex,
                                      options.vCounterGlyphs,
                                      options.hCounterGlyphs)
        printFontInfo(str(fdDict))
        fontData.close()
        return

    fdGlyphDict, fontDictList = fontData.getfdInfo(options.allow_no_blues,
                                                   options.noFlex,
                                                   options.vCounterGlyphs,
                                                   options.hCounterGlyphs,
                                                   glyphList)

    if options.printFDDictList:
        # Print the user defined FontDicts, and exit.
        if fdGlyphDict:
            print("Showing user-defined FontDict Values:\n")
            for fi, fontDict in enumerate(fontDictList):
                print(fontDict.DictName)
                printFontInfo(str(fontDict))
                gnameList = []
                # item = [glyphName, [fdIndex, glyphListIndex]]
                itemList = sorted(fdGlyphDict.items(), key=lambda x: x[1][1])
                for gName, entry in itemList:
                    if entry[0] == fi:
                        gnameList.append(gName)
                print("%d glyphs:" % len(gnameList))
                if len(gnameList) > 0:
                    gTxt = " ".join(gnameList)
                else:
                    gTxt = "None"
                print(gTxt + "\n")
        else:
            print("There are no user-defined FontDict Values.")
        fontData.close()
        return

    if fdGlyphDict is None:
        fdDict = fontDictList[0]
        fontInfo = fdDict.getFontInfo()
    else:
        log.info("Using alternate FDDict global values from fontinfo "
                 "file for some glyphs.")

    # Get charstring for identifier in glyph-list
    isCID = fontData.isCID()
    lastFDIndex = None
    anyGlyphChanged = False
    if isCID:
        options.noFlex = True

    seenGlyphCount = 0
    processedGlyphCount = 0
    for name in glyphList:
        seenGlyphCount += 1

        # Convert to bez format
        bezString, width = fontData.convertToBez(name, options.read_hints,
                                                 options.round_coords,
                                                 options.hintAll)
        if bezString is None or "mt" not in bezString:
            # skip empty glyphs.
            continue

        processedGlyphCount += 1

        # get new fontinfo string if FDarray index has changed,
        # as each FontDict has different alignment zones.
        if isCID:
            fdIndex = fontData.getfdIndex(name)
            if not fdIndex == lastFDIndex:
                lastFDIndex = fdIndex
                fdDict = fontData.getFontInfo(options.allow_no_blues,
                                              options.noFlex,
                                              options.vCounterGlyphs,
                                              options.hCounterGlyphs,
                                              fdIndex)
                fontInfo = fdDict.getFontInfo()
        else:
            if fdGlyphDict is not None:
                fdIndex = fdGlyphDict[name][0]
                if lastFDIndex != fdIndex:
                    lastFDIndex = fdIndex
                    fdDict = fontDictList[fdIndex]
                    fontInfo = fdDict.getFontInfo()

        # Build autohint point list identifier

        if fdGlyphDict:
            log.info("%s: Begin hinting (using fdDict %s).",
                     nameAliases.get(name, name), fdDict.DictName)
        else:
            log.info("%s: Begin hinting.", nameAliases.get(name, name))

        # Call auto-hint library on bez string.
        try:
            if reference_master or not options.reference_font:
                newBezString = hint_bez_glyph(fontInfo, bezString,
                                              options.allowChanges,
                                              not options.noHintSub,
                                              options.round_coords)
                options.baseMaster[name] = newBezString
            else:
                baseFontFileName = os.path.basename(options.reference_font)
                masters = [baseFontFileName, fontFileName]
                glyphs = [options.baseMaster[name], bezString]
                newBezString = hint_compatible_bez_glyphs(fontInfo, glyphs,
                                                          masters)
                newBezString = newBezString[1]  # FIXME
        except PsAutoHintCError:
            raise ACHintError("%s: Failure in processing outline data." %
                              nameAliases.get(name, name))

        if not (("ry" in newBezString[:200]) or ("rb" in newBezString[:200]) or
           ("rm" in newBezString[:200]) or ("rv" in newBezString[:200])):
            log.info("No hints added!")

        if options.logOnly:
            continue

        # Convert bez to charstring, and update CFF.
        anyGlyphChanged = True
        fontData.updateFromBez(newBezString, name, width)

    if not options.logOnly:
        if anyGlyphChanged:
            log.info("Saving font file with new hints..." + time.asctime())
            fontData.save(outpath)
        else:
            fontData.close()
            log.info("No glyphs were hinted.")
    if processedGlyphCount != seenGlyphCount:
        log.info("Skipped %s of %s glyphs.",
                 seenGlyphCount - processedGlyphCount, seenGlyphCount)
    log.info("Done with font %s. End time: %s.", path, time.asctime())
