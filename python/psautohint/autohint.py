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

import sys
import os
import re
import time
import plistlib
import traceback
import shutil

from psautohint import psautohint


kACIDKey = "AutoHintKey"

gLogFile = None
kFontPlistSuffix = ".plist"
kTempCFFSuffix = ".temp.ac.cff"
kProgressChar = "."


class ACOptions:
    def __init__(self):
        self.inputPaths = []
        self.outputPath = None
        self.glyphList = []
        self.nameAliases = {}
        self.excludeGlyphList = False
        self.usePlistFile = False
        self.hintAll = False
        self.rehint = False
        self.verbose = True
        self.quiet = False
        self.allowChanges = False
        self.noFlex = False
        self.noHintSub = False
        self.allow_no_blues = False
        self.hCounterGlyphs = []
        self.vCounterGlyphs = []
        self.counterHintFile = None
        self.logOnly = False
        self.logFile = None
        self.printDefaultFDDict = False
        self.printFDDictList = False
        self.debug = False
        self.allowDecimalCoords = False
        self.writeToDefaultLayer = False
        self.baseMaster = {}


class ACFontInfoParseError(Exception):
    pass


class ACFontError(Exception):
    pass


class ACHintError(Exception):
    pass


def logMsg(*args):
    for arg in args:
        msg = str(arg).strip()
        if not msg:
            sys.stdout.flush()
            if gLogFile:
                gLogFile.write("\n")
                gLogFile.flush()
            return

        if msg[-1] == ",":
            msg = msg[:-1]
            if msg == kProgressChar:
                sys.stdout.write(msg)  # avoid space, which is added by 'print'
            else:
                print(msg,)
            sys.stdout.flush()
            if gLogFile:
                gLogFile.write(msg)
                gLogFile.flush()
        else:
            print(msg)
            sys.stdout.flush()
            if gLogFile:
                gLogFile.write(msg + "\n")
                gLogFile.flush()


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
            logMsg("\tWarning: glyph ID <%s> in range %s from glyph selection "
                   "list option is not in font. <%s>." %
                   (rangeList[0], glyphTag, fontFileName))
        else:
            logMsg("\tWarning: glyph ID <%s> from glyph selection list option "
                   "is not in font. <%s>." % (rangeList[0], fontFileName))
        return None
    glyphNameList.append(fontGlyphList[prevGID])

    for glyphTag2 in rangeList[1:]:
        gid = getGlyphID(glyphTag2, fontGlyphList)
        if gid is None:
            logMsg("\tWarning: glyph ID <%s> in range %s from glyph selection "
                   "list option is not in font. <%s>." %
                   (glyphTag2, glyphTag, fontFileName))
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


def openFontPlistFile(psName, dirPath):
    # Find or create the plist file.
    # This holds a Python dictionary in repr() form,
    #    key: glyph name
    #    value: outline point list
    # This is used to determine which glyphs are manually hinted,
    # and which have changed since the last hint pass.
    fontPlist = None
    filePath = None
    isNewPlistFile = True
    pPath1 = os.path.join(dirPath, psName + kFontPlistSuffix)
    if os.path.exists(pPath1):
        filePath = pPath1
    else:
        # Crude approach to file length limitations.
        # Since Adobe keeps face info in separate directories, I don't worry
        # about name collisions.
        pPath2 = os.path.join(
            dirPath, psName[:-len(kFontPlistSuffix)] + kFontPlistSuffix)
        if os.path.exists(pPath2):
            filePath = pPath2
    if not filePath:
        filePath = pPath1
    else:
        try:
            fontPlist = plistlib.Plist.fromFile(filePath)
            isNewPlistFile = False
        except (IOError, OSError):
            raise ACFontError("\tError: font plist file exists, but could not "
                              "be read <%s>." % filePath)
        except Exception:
            raise ACFontError("\tError: font plist file exists, but could not "
                              "be parsed <%s>." % filePath)

    if fontPlist is None:
        fontPlist = plistlib.Plist()
    if kACIDKey not in fontPlist:
        fontPlist[kACIDKey] = {}
    return fontPlist, filePath, isNewPlistFile


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


flexPatthern = re.compile(
    r"preflx1[^f]+preflx2[\r\n]"
    r"(-*\d+\s+-*\d+\s+-*\d+\s+-*\d+\s+-*\d+\s+-*\d+\s+)"
    r"(-*\d+\s+-*\d+\s+-*\d+\s+-*\d+\s+-*\d+\s+-*\d+\s+).+?flx([\r\n])",
    re.DOTALL)
commentPattern = re.compile(r"[^\r\n]*%[^\r\n]*[\r\n]")
hintGroupPattern = re.compile(r"beginsubr.+?newcolors[\r\n]", re.DOTALL)
whiteSpacePattern = re.compile(r"\s+", re.DOTALL)


def makeACIdentifier(bezText):
    # Get rid of all the hint operators and their args
    # collapse flex to just the two rct's
    bezText = commentPattern.sub("", bezText)
    bezText = hintGroupPattern.sub("", bezText)
    bezText = flexPatthern.sub("\1 rct\3\2 rct\3", bezText)
    bezText = whiteSpacePattern.sub("", bezText)
    return bezText


def openFile(path, outFilePath, useHashMap, options):
    if os.path.isfile(path):
        font = openOpenTypeFile(path, outFilePath, options)
    else:
        # maybe it is a a UFO font.
        # We always use the hash map to skip glyphs that have been previously
        # processed, unless the user has report only, not make changes.
        font = openUFOFile(path, outFilePath, useHashMap, options)
    return font


def openUFOFile(path, outFilePath, useHashMap, options):
    from psautohint import ufoFont

    # Check if has glyphs/contents.plist
    contentsPath = os.path.join(path, "glyphs", "contents.plist")
    if not os.path.exists(contentsPath):
        msg = "Font file must be a CFF, OTF, or ufo font file: %s." % (path)
        logMsg(msg)
        raise ACFontError(msg)

    # If user has specified a path other than the source font path,
    # then copy the entire UFO font, and operate on the copy.
    if (outFilePath is not None) and (
       os.path.abspath(path) != os.path.abspath(outFilePath)):
        if not options.quiet:
            msg = "Copying from source UFO font to output UFO font " + \
                  "before processing..."
            logMsg(msg)
        if os.path.exists(outFilePath):
            shutil.rmtree(outFilePath)
        shutil.copytree(path, outFilePath)
        path = outFilePath
    font = ufoFont.UFOFontData(path, useHashMap, ufoFont.kAutohintName)
    font.useProcessedLayer = True
    # Programs in this list must be run before autohint,
    # if the outlines have been edited.
    font.requiredHistory.append(ufoFont.kCheckOutlineName)
    return font


def openOpenTypeFile(path, outFilePath, options):
    from psautohint.otfFont import CFFFontData
    from fontTools.ttLib import TTFont, TTLibError, getTableModule

    # If input font is CFF, build a dummy ttFont in memory.
    # Return ttFont, and flag if is a real OTF font.
    # Return flag is 0 if OTF, and 1 if CFF.
    fontType = 0  # OTF
    try:
        with open(path, "rb") as ff:
            data = ff.read(10)
    except (IOError, OSError):
        logMsg("Failed to open and read font file %s." % path)

    if data[:4] == b"OTTO":  # it is an OTF font, can process file directly
        try:
            ttFont = TTFont(path)
        except (IOError, OSError):
            raise ACFontError("Error opening or reading from font file <%s>." %
                              path)
        except TTLibError:
            raise ACFontError("Error parsing font file <%s>." % path)

        try:
            cffTable = ttFont["CFF "]
        except KeyError:
            raise ACFontError("Error: font is not a CFF font <%s>." %
                              path)
    else:
        # It is not an OTF file.
        if (data[0] == b'\1') and (data[1] == b'\0'):  # CFF file
            fontType = 1
        else:
            logMsg("Font file must be a CFF or OTF fontfile: %s." % path)
            raise ACFontError("Font file must be CFF or OTF file: %s." % path)

        # now package the CFF font as an OTF font.
        with open(path, "rb") as ff:
            data = ff.read()
        try:
            ttFont = TTFont()
            cffModule = getTableModule('CFF ')
            cffTable = cffModule.table_C_F_F_('CFF ')
            ttFont['CFF '] = cffTable
            cffTable.decompile(data, ttFont)
        except Exception:
            logMsg("\t%s" % (traceback.format_exception_only(sys.exc_info()[0],
                             sys.exc_info()[1])[-1]))
            logMsg("Attempted to read font %s as CFF." % path)
            raise ACFontError("Error parsing font file <%s>." % path)

    fontData = CFFFontData(ttFont, path, outFilePath, fontType, logMsg)
    return fontData


def removeTempFiles(fileList):
    for filePath in fileList:
        if os.path.exists(filePath):
            os.remove(filePath)


def cmpFDDictEntries(entry1, entry2):
    # entry = [glyphName, [fdIndex, glyphListIndex] ]
    if entry1[1][1] > entry2[1][1]:
        return 1
    elif entry1[1][1] < entry2[1][1]:
        return -1
    else:
        return 0

def hintFiles(options):
    hintFile(options)
    if len(options.inputPaths) > 1:
        for path in options.inputPaths[1:]:
            hintFile(options, path, baseMaster=False)

def hintFile(options, path=None, baseMaster=True):
    global gLogFile
    gLogFile = options.logFile
    nameAliases = options.nameAliases

    if path is None:
        path = options.inputPaths[0]
    fontFileName = os.path.basename(path)
    if not options.quiet:
        logMsg("Hinting font %s. Start time: %s." % (path, time.asctime()))

    try:
        # For UFO fonts only.
        # We always use the hash map, unless the user
        # requested only report issues.
        useHashMap = not options.logOnly
        fontData = openFile(path, options.outputPath, useHashMap, options)
        fontData.allowDecimalCoords = options.allowDecimalCoords
        if options.writeToDefaultLayer and (
           hasattr(fontData, "setWriteToDefault")):  # UFO fonts only
            fontData.setWriteToDefault()
    except (IOError, OSError):
        logMsg(traceback.format_exception_only(sys.exc_info()[0],
                                               sys.exc_info()[1])[-1])
        raise ACFontError("Error opening or reading from font file <%s>." %
                          fontFileName)
    except Exception:
        logMsg(traceback.format_exception_only(sys.exc_info()[0],
                                               sys.exc_info()[1])[-1])
        raise ACFontError("Error parsing font file <%s>." % fontFileName)

    # filter specified list, if any, with font list.
    fontGlyphList = fontData.getGlyphList()
    glyphList = filterGlyphList(options, fontGlyphList, fontFileName)
    if not glyphList:
        raise ACFontError("Error: selected glyph list is empty for font "
                          "<%s>." % fontFileName)

    fontInfo = ""

    psName = fontData.getPSName()

    if (not options.logOnly) and options.usePlistFile:
        fontPlist, fontPlistFilePath, isNewPlistFile = openFontPlistFile(
            psName, os.path.dirname(path))
        if isNewPlistFile and not (options.hintAll or options.rehint):
            logMsg("No hint info plist file was found, so all glyphs are "
                   "unknown to autohint. To hint all glyphs, run autohint "
                   "again with option -a to hint all glyphs unconditionally.")
            logMsg("Done with font %s. End time: %s." % (path, time.asctime()))
            fontData.close()
            return

    # Check counter glyphs, if any.
    if options.hCounterGlyphs or options.vCounterGlyphs:
        missingList = filter(lambda name: name not in fontGlyphList,
                             options.hCounterGlyphs + options.vCounterGlyphs)
        if missingList:
            logMsg("\tError: glyph named in counter hint list file '%s' are "
                   "not in font: %s" % (options.counterHintFile, missingList))

    # Build alignment zone string
    if (options.printDefaultFDDict):
        logMsg("Showing default FDDict Values:")
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
            logMsg("Showing user-defined FontDict Values:")
            for fi in enumerate(fontDictList):
                fontDict = fontDictList[fi]
                logMsg("")
                logMsg(fontDict.DictName)
                printFontInfo(str(fontDict))
                gnameList = []
                itemList = fdGlyphDict.items()
                itemList.sort(cmpFDDictEntries)
                for gName, entry in itemList:
                    if entry[0] == fi:
                        gnameList.append(gName)
                logMsg("%d glyphs:" % len(gnameList))
                if len(gnameList) > 0:
                    gTxt = " ".join(gnameList)
                else:
                    gTxt = "None"
                logMsg(gTxt)
        else:
            logMsg("There are no user-defined FontDict Values.")
        fontData.close()
        return

    if fdGlyphDict is None:
        fdDict = fontDictList[0]
        fontInfo = fdDict.getFontInfo()
    else:
        if not options.verbose and not options.quiet:
            logMsg("Note: Using alternate FDDict global values from fontinfo "
                   "file for some glyphs. Remove option '-q' to see which "
                   "dict is used for which glyphs.")

    # Get charstring for identifier in glyph-list
    isCID = fontData.isCID()
    lastFDIndex = None
    anyGlyphChanged = False
    pListChanged = False
    if isCID:
        options.noFlex = True

    if not options.verbose:
        dotCount = 0

    dotCount = 0
    seenGlyphCount = 0
    processedGlyphCount = 0
    for name in glyphList:
        prevACIdentifier = None
        seenGlyphCount += 1

        # Convert to bez format
        bezString, width, hasHints = fontData.convertToBez(
            name, options.verbose, options.hintAll)
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
        if (not options.logOnly) and options.usePlistFile:
            # If the glyph is not in the plist file, then we skip it unless
            # kReHintUnknown is set.
            # If the glyph is in the plist file and the outline has changed,
            # we hint it.
            ACidentifier = makeACIdentifier(bezString)
            try:
                (prevACIdentifier, _, oldBezString,
                    oldHintBezString) = fontPlist[kACIDKey][name]
            except ValueError:
                prevACIdentifier, _ = fontPlist[kACIDKey][name]
                oldBezString = oldHintBezString = ""
            except KeyError:
                # there wasn't an entry in tempList file, so we will add one.
                pListChanged = True
                if hasHints and not options.rehint:
                    # Glyphs is hinted, but not referenced in the plist file.
                    # Skip it unless options.rehint is seen
                    if not isNewPlistFile:
                        # Comment only if there is a plist file; otherwise,
                        # we'd be complaining for almost every glyph.
                        logMsg("%s Skipping glyph - it has hints, but it is "
                               "not in the hint info plist file." %
                               nameAliases.get(name, name))
                        dotCount = 0
                    continue
            # there's an entry in the plist file
            # and it matches what's in the font
            if prevACIdentifier and (prevACIdentifier == ACidentifier):
                if hasHints and not (options.hintAll or options.rehint):
                    continue
            else:
                pListChanged = True

        if options.verbose:
            if fdGlyphDict:
                logMsg("Hinting %s with fdDict %s." %
                       (nameAliases.get(name, name), fdDict.DictName))
            else:
                logMsg("Hinting %s." % nameAliases.get(name, name))
        elif not options.quiet:
            logMsg(".,")
            dotCount += 1
            # I do this to never have more than 40 dots on a line.
            # This in turn give reasonable performance when calling autohint
            # in a subprocess and getting output with std.readline()
            if dotCount > 40:
                dotCount = 0
                logMsg("")

        # Call auto-hint library on bez string.
        # print("oldBezString", oldBezString)
        # print("")
        # print("bezString", bezString)

        if oldBezString != "" and oldBezString == bezString:
            newBezString = oldHintBezString
        else:
            if baseMaster:
                newBezString = psautohint.autohint(
                    fontInfo, bezString, options.verbose,
                    options.allowChanges, not options.noHintSub,
                    options.allowDecimalCoords)
                options.baseMaster[name] = newBezString
            else:
                masters = [b"A", b"B"]
                glyphs = [options.baseMaster[name], bezString]
                newBezString = psautohint.autohintmm(
                    fontInfo, glyphs, masters, options.verbose)

        if not newBezString:
            if not options.verbose and not options.quiet:
                logMsg("")
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
        fontData.updateFromBez(newBezString, name, width, options.verbose)

        if options.usePlistFile:
            bezString = "%% %s\n%s" % (name, newBezString)
            ACidentifier = makeACIdentifier(bezString)
            # add glyph hint entry to plist file
            if options.allowChanges:
                if prevACIdentifier and (prevACIdentifier != ACidentifier):
                    logMsg("\t%s Glyph outline changed" %
                           nameAliases.get(name, name))
                    dotCount = 0

            fontPlist[kACIDKey][name] = (ACidentifier, time.asctime(),
                                         bezString, newBezString)

    if not options.verbose and not options.quiet:
        print("")  # print final new line after progress dots.

    if not options.logOnly:
        if anyGlyphChanged:
            if not options.quiet:
                logMsg("Saving font file with new hints..." + time.asctime())
            fontData.saveChanges()
        else:
            fontData.close()
            if options.usePlistFile:
                if options.rehint:
                    logMsg("No new hints. All glyphs had hints that matched "
                           "the hint record file %s." % (fontPlistFilePath))
                if options.hintAll:
                    logMsg("No new hints. All glyphs had hints that matched "
                           "the hint history file %s, or were not in the "
                           "history file and already had hints." %
                           fontPlistFilePath)
                else:
                    logMsg("No new hints. All glyphs were already hinted.")
            else:
                logMsg("No glyphs were hinted.")
    if options.usePlistFile and (anyGlyphChanged or pListChanged):
        # save font plist file.
        fontPlist.write(fontPlistFilePath)
    if processedGlyphCount != seenGlyphCount:
        logMsg("Skipped %s of %s glyphs." %
               (seenGlyphCount - processedGlyphCount, seenGlyphCount))
    if not options.quiet:
        logMsg("Done with font %s. End time: %s." % (path, time.asctime()))
