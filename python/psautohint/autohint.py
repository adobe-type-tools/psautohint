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
import shutil
import time

from fontTools.ttLib import TTFont, getTableClass

from .psautohint import autohint, autohintmm
from .otfFont import CFFFontData
from .ufoFont import UFOFontData, kAutohintName, kCheckOutlineName

from . import get_font_format


log = logging.getLogger(__name__)


class ACOptions(object):
    def __init__(self):
        self.inputPaths = []
        self.outputPaths = []
        self.reference_font = None
        self.glyphList = []
        self.nameAliases = {}
        self.excludeGlyphList = False
        self.usePlistFile = False
        self.hintAll = False
        self.rehint = False
        self.allowChanges = False
        self.noFlex = False
        self.noHintSub = False
        self.allow_no_blues = False
        self.hCounterGlyphs = []
        self.vCounterGlyphs = []
        self.logOnly = False
        self.printDefaultFDDict = False
        self.printFDDictList = False
        self.allowDecimalCoords = False
        self.writeToDefaultLayer = False
        self.baseMaster = {}
        self.font_format = None


class ACFontError(Exception):
    pass


class ACHintError(Exception):
    pass


def getGlyphID(glyphTag, fontGlyphList):
    # FIXME: This is unnecessarily convoluted
    glyphID = None
    try:
        glyphID = int(glyphTag)
        fontGlyphList[glyphID]
    except IndexError:
        pass
    except ValueError:
        try:
            glyphID = fontGlyphList.index(glyphTag)
        except IndexError:
            pass
        except ValueError:
            pass
    return glyphID


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


def openFile(path, outFilePath, options):
    font_format = get_font_format(path)
    if font_format is None:
        raise ACFontError("{} is not a supported font format".format(path))

    if font_format == "UFO":
        font = openUFOFile(path, outFilePath, options)
    elif font_format in ("OTF", "CFF"):
        font = openOpenTypeFile(path, outFilePath, font_format, options)
    else:
        raise NotImplementedError("%s format is not supported yet, sorry." %
                                  font_format)

    return font


def openUFOFile(path, outFilePath, options):
    # If user has specified a path other than the source font path,
    # then copy the entire UFO font, and operate on the copy.
    if (outFilePath is not None) and (
       os.path.abspath(path) != os.path.abspath(outFilePath)):
        log.info("Copying from source UFO font to output UFO font "
                 "before processing...")
        if os.path.exists(outFilePath):
            shutil.rmtree(outFilePath)
        shutil.copytree(path, outFilePath)
        path = outFilePath
    # We always use the hash map to skip glyphs that have been previously
    # processed, unless the user has report only, not make changes.
    useHashMap = not options.logOnly
    font = UFOFontData(path, useHashMap, options.allowDecimalCoords,
                       kAutohintName)
    font.useProcessedLayer = True
    if options.writeToDefaultLayer:
        font.setWriteToDefault()
    # Programs in this list must be run before autohint,
    # if the outlines have been edited.
    font.requiredHistory.append(kCheckOutlineName)
    return font


def openOpenTypeFile(path, outFilePath, font_format, options):
    # If input font is CFF, build a dummy ttFont in memory.
    if font_format == "OTF":  # it is an OTF font, can process file directly
        ttFont = TTFont(path)
        if "CFF " not in ttFont:
            raise ACFontError("Error: font is not a CFF font <%s>." % path)
    elif font_format == "CFF":
        # now package the CFF font as an OTF font.
        with open(path, "rb") as ff:
            data = ff.read()

        ttFont = TTFont()
        cffClass = getTableClass('CFF ')
        ttFont['CFF '] = cffClass('CFF ')
        ttFont['CFF '].decompile(data, ttFont)
    else:
        raise ACFontError("Font file must be CFF or OTF file: %s." % path)

    fontData = CFFFontData(ttFont, path, outFilePath,
                           options.allowDecimalCoords, font_format)
    return fontData


def cmpFDDictEntries(entry1, entry2):
    # entry = [glyphName, [fdIndex, glyphListIndex] ]
    if entry1[1][1] > entry2[1][1]:
        return 1
    elif entry1[1][1] < entry2[1][1]:
        return -1
    else:
        return 0


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

    fontData = openFile(path, outpath, options)

    # filter specified list, if any, with font list.
    fontGlyphList = fontData.getGlyphList()
    glyphList = filterGlyphList(options, fontGlyphList, fontFileName)
    if not glyphList:
        raise ACFontError("Error: selected glyph list is empty for font "
                          "<%s>." % fontFileName)

    fontInfo = ""

    psName = fontData.getPSName()

    # Check counter glyphs, if any.
    if options.hCounterGlyphs or options.vCounterGlyphs:
        missingList = filter(lambda name: name not in fontGlyphList,
                             options.hCounterGlyphs + options.vCounterGlyphs)
        if missingList:
            log.error("H/VCounterChars glyph named in fontinfo is "
                      "not in font: %s", missingList)

    # Build alignment zone string
    if (options.printDefaultFDDict):
        print("Showing default FDDict Values:")
        fdDict = fontData.getFontInfo(psName, path,
                                      options.allow_no_blues,
                                      options.noFlex,
                                      options.vCounterGlyphs,
                                      options.hCounterGlyphs)
        printFontInfo(str(fdDict))
        fontData.close()
        return

    fdGlyphDict, fontDictList = fontData.getfdInfo(psName, path,
                                                   options.allow_no_blues,
                                                   options.noFlex,
                                                   options.vCounterGlyphs,
                                                   options.hCounterGlyphs,
                                                   glyphList)

    if options.printFDDictList:
        # Print the user defined FontDicts, and exit.
        if fdGlyphDict:
            print("Showing user-defined FontDict Values:")
            for fi in enumerate(fontDictList):
                fontDict = fontDictList[fi]
                print(fontDict.DictName)
                printFontInfo(str(fontDict))
                gnameList = []
                itemList = fdGlyphDict.items()
                itemList.sort(cmpFDDictEntries)
                for gName, entry in itemList:
                    if entry[0] == fi:
                        gnameList.append(gName)
                print("%d glyphs:" % len(gnameList))
                if len(gnameList) > 0:
                    gTxt = " ".join(gnameList)
                else:
                    gTxt = "None"
                print(gTxt)
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
        bezString, width = fontData.convertToBez(name, options.hintAll)
        processedGlyphCount += 1
        if bezString is None:
            continue

        if "mt" not in bezString:
            # skip empty glyphs.
            continue
        # get new fontinfo string if FDarray index has changed,
        # as each FontDict has different alignment zones.
        gid = fontData.getGlyphID(name)
        if isCID:
            fdIndex = fontData.getfdIndex(gid)
            if not fdIndex == lastFDIndex:
                lastFDIndex = fdIndex
                fdDict = fontData.getFontInfo(psName, path,
                                              options.allow_no_blues,
                                              options.noFlex,
                                              options.vCounterGlyphs,
                                              options.hCounterGlyphs,
                                              fdIndex)
                fontInfo = fdDict.getFontInfo()
        else:
            if (fdGlyphDict is not None):
                try:
                    fdIndex = fdGlyphDict[name][0]
                except KeyError:
                    # use default dict.
                    fdIndex = 0
                if lastFDIndex != fdIndex:
                    lastFDIndex = fdIndex
                    fdDict = fontDictList[fdIndex]
                    fontInfo = fdDict.getFontInfo()

        # Build autohint point list identifier
        oldBezString = ""
        oldHintBezString = ""

        if fdGlyphDict:
            log.info("Hinting %s with fdDict %s.",
                     nameAliases.get(name, name), fdDict.DictName)
        else:
            log.info("Hinting %s.", nameAliases.get(name, name))

        # Call auto-hint library on bez string.
        # print("oldBezString", oldBezString)
        # print("")
        # print("bezString", bezString)

        if oldBezString != "" and oldBezString == bezString:
            newBezString = oldHintBezString
        else:
            if reference_master or not options.reference_font:
                newBezString = autohint(fontInfo, bezString,
                                        options.allowChanges,
                                        not options.noHintSub,
                                        options.allowDecimalCoords)
                options.baseMaster[name] = newBezString
            else:
                baseFontFileName = os.path.basename(options.reference_font)
                masters = [baseFontFileName, fontFileName]
                glyphs = [options.baseMaster[name], bezString]
                newBezString = autohintmm(fontInfo, glyphs, masters)
                newBezString = newBezString[1]  # FIXME

        if not newBezString:
            raise ACHintError(
                "%s Error - failure in processing outline data." %
                nameAliases.get(name, name))

        if not (("ry" in newBezString[:200]) or ("rb" in newBezString[:200]) or
           ("rm" in newBezString[:200]) or ("rv" in newBezString[:200])):
            print("No hints added!")

        if options.logOnly:
            continue

        # Convert bez to charstring, and update CFF.
        anyGlyphChanged = True
        fontData.updateFromBez(newBezString, name, width)

    if not options.logOnly:
        if anyGlyphChanged:
            log.info("Saving font file with new hints..." + time.asctime())
            fontData.saveChanges()
        else:
            fontData.close()
            log.info("No glyphs were hinted.")
    if processedGlyphCount != seenGlyphCount:
        log.info("Skipped %s of %s glyphs.",
                 seenGlyphCount - processedGlyphCount, seenGlyphCount)
    log.info("Done with font %s. End time: %s.", path, time.asctime())
