# Copyright 2016 Adobe. All rights reserved.

# Methods:

# Parse args. If glyphlist is from file, read in entire file as single string,
# and remove all white space, then parse out glyph-names and GID's.

import logging
import os
import sys
import time
from collections import namedtuple
from threading import Thread
from multiprocessing import Pool, Manager  # , set_start_method

from .otfFont import CFFFontData
from .ufoFont import UFOFontData
from .report import Report, GlyphReport
from .hinter import glyphHinter
from .logging import log_receiver

from . import (get_font_format, FontParseError)

log = logging.getLogger(__name__)


class ACOptions(object):
    def __init__(self):
        self.inputPaths = []
        self.outputPaths = []
        self.reference_font = None
        self.glyphList = []
        # True when contents of glyphList were specified directly by the user
        self.explicitGlyphs = False
        self.nameAliases = {}
        self.excludeGlyphList = False
        self.hintAll = False
        self.readHints = True
        self.allowChanges = False
        self.noFlex = False
        self.noHintSub = False
        self.allow_no_blues = False
        self.logOnly = False
        self.removeConflicts = True
        # Copy of parse_args verbose for child processes
        self.verbose = 0
        self.printDefaultFDDict = False
        self.printFDDictList = False
        self.roundCoords = True
        self.writeToDefaultLayer = False
        # If this number of segments is exceeded in a dimension, don't hint
        # (Only applies when explicitGlyphs is False)
        self.maxSegments = 100
        self.font_format = None
        self.report_zones = False
        self.report_stems = False
        self.report_all_stems = False
        self.process_count = None
        # False in these dictionaries indicates that there should be no
        # warning if the glyph is missing
        self.hCounterGlyphs = {'element': False, 'equivalence': False,
                               'notelement': False, 'divide': False}
        self.vCounterGlyphs = {'m': False, 'M': False, 'T': False,
                               'ellipsis': False}
        self.upperSpecials = {'questiondown': False, 'exclamdown': False,
                              'semicolon': False}
        self.lowerSpecials = {'question': False, 'exclam': False,
                              'colon': False}
        self.noBlues = {'at': False, 'bullet': False, 'copyright': False,
                        'currency': False, 'registered': False}
#        self.newHintsOnMoveto = {'percent': False, 'perthousand': False}

    def __str__(self):
        # used only when debugging.
        import inspect
        data = []
        methodList = inspect.getmembers(self)
        for fname, fvalue in methodList:
            if fname[0] == "_":
                continue
            data.append(str((fname, fvalue)))
        data.append("")
        return os.linesep.join(data)

    def justReporting(self):
        return self.report_zones or self.report_stems


def getGlyphNames(glyphTag, fontGlyphList, fDesc):
    glyphNameList = []
    rangeList = glyphTag.split("-")
    try:
        prevGID = fontGlyphList.index(rangeList[0])
    except ValueError:
        if len(rangeList) > 1:
            log.warning("glyph ID <%s> in range %s from glyph selection "
                        "list option is not in font. <%s>.",
                        rangeList[0], glyphTag, fDesc)
        else:
            log.warning("glyph ID <%s> from glyph selection list option "
                        "is not in font. <%s>.", rangeList[0], fDesc)
        return None
    glyphNameList.append(fontGlyphList[prevGID])

    for glyphTag2 in rangeList[1:]:
        try:
            gid = fontGlyphList.index(glyphTag2)
        except ValueError:
            log.warning("glyph ID <%s> in range %s from glyph selection "
                        "list option is not in font. <%s>.",
                        glyphTag2, glyphTag, fDesc)
            return None
        for i in range(prevGID + 1, gid + 1):
            glyphNameList.append(fontGlyphList[i])
        prevGID = gid

    return glyphNameList


def filterGlyphList(options, fontGlyphList, fDesc):
    """
    Returns the list of glyphs which are in the intersection of the argument
    list and the glyphs in the font.
    """
    if not options.glyphList:
        glyphList = fontGlyphList
    else:
        # expand ranges:
        glyphList = []
        for glyphTag in options.glyphList:
            glyphNames = getGlyphNames(glyphTag, fontGlyphList, fDesc)
            if glyphNames is not None:
                glyphList.extend(glyphNames)
        if options.excludeGlyphList:
            glyphList = [n for n in fontGlyphList if n not in glyphList]
    return glyphList


def get_glyph(options, font, name):
    try:
        gl = font.convertToGlyphData(name, options.readHints,  # stems
                                     options.readHints,  # flex
                                     options.roundCoords,
                                     options.hintAll)
        if gl is None or gl.isEmpty():
            # skip empty glyphs.
            return None
        return gl
    except KeyError:
        # Source fonts may be sparse, e.g. be a subset of the
        # reference font.
        return None

def get_fontinfo_list_withFDArray(options, font, glyph_list, isVF=False):
    fdGlyphDict = {}
    fontDictList = []
    l = 0
    for name in glyph_list:
        fdIndex = font.getfdIndex(name)
        if fdIndex >= l:
            fontDictList.extend([None] * (fdIndex - l + 1))
            l = fdIndex + 1
        if fontDictList[fdIndex] is None:
            fddict = font.getFontInfo(options.allow_no_blues,
                                      options.noFlex,
                                      options.vCounterGlyphs,
                                      options.hCounterGlyphs,
                                      fdIndex, isVF)
            fontDictList[fdIndex] = fddict
        if fdIndex != 0:
            fdGlyphDict[name] = fdIndex

    if isVF and fontDictList:
        # If the font was variable then each "fddict" in the list is a
        # list of fddicts by master. Now we just need to swap the axes
        # (could use a numpy one-liner but don't want the dependency)
        fdls = []
        for i in range(len(fontDictList[fdIndex])):
            fdls.append([x[i] if x is not None else None
                        for x in fontDictList])

    return fdGlyphDict, fontDictList


def get_fontinfo_list_withFontInfo(options, font, glyph_list):
    # Build alignment zone string
    if options.printDefaultFDDict:
        print("Showing default FDDict Values:")
        fddict = font.getFontInfo(options.allow_no_blues,
                                  options.noFlex,
                                  options.vCounterGlyphs,
                                  options.hCounterGlyphs)
        # Exit by printing default FDDict with all lines indented by one tab
        sys.exit("\t" + "\n\t".join(str(fddict).split("\n")))

    fdGlyphDict, fontDictList = font.getfdInfo(options.allow_no_blues,
                                               options.noFlex,
                                               options.vCounterGlyphs,
                                               options.hCounterGlyphs,
                                               glyph_list)

    if options.printFDDictList:
        # Print the user defined FontDicts, and exit.
        if fdGlyphDict:
            print("Showing user-defined FontDict Values:\n")
            for fi, fontDict in enumerate(fontDictList):
                if fontDict is None:
                    continue
                print(fontDict.DictName)
                print(str(fontDict))
                if fi == 0:
                    continue
                gnameList = [gn for gn, fdIndex in fdGlyphDict.items()
                             if fdIndex == fi]
                print("%d glyphs:" % len(gnameList))
                if len(gnameList) > 0:
                    gTxt = " ".join(gnameList)
                else:
                    gTxt = "None"
                print(gTxt + "\n")
        else:
            print("There are no user-defined FontDict Values.")
        return

    if fdGlyphDict:
        log.info("Using alternate FDDicts for some glyphs.")

    return fdGlyphDict, fontDictList


FontInstance = namedtuple("FontInstance", "font desc outpath")


class fontWrapper:
    """
    Stores references to one or more related master font objects.
    Extracts glyphs from those objects by name, hints them, and
    stores the result back those objects. Optionally saves the
    modified glyphs in corresponding output font files.
    """
    def __init__(self, options, fil):
        self.options = options
        self.fontInstances = fil
        if len(fil) > 1:
            self.isVF = False
        else:
            self.isVF = (hasattr(fil[0].font, 'ttFont') and
                         'fvar' in fil[0].font.ttFont)
        self.reportOnly = options.justReporting()
        assert not self.reportOnly or (not self.isVF and len(fil) == 1)
        self.notFound = 0
        self.glyphNameList = filterGlyphList(options,
                                             fil[0].font.getGlyphList(),
                                             fil[0].desc)
        if not self.glyphNameList:
            raise FontParseError("Selected glyph list is empty for " +
                                 "font <%s>." % fil[0].desc)
        self.getFontinfoLists()

    def numGlyphs(self):
        return len(self.glyphNameList)

    def getFontinfoLists(self):
        options = self.options
        font = self.fontInstances[0].font
        # Check for missing glyphs explicitly added via fontinfo or cmd line
        for label, charDict in [("hCounterGlyphs", options.hCounterGlyphs),
                                ("vCounterGlyphs", options.vCounterGlyphs),
                                ("upperSpecials", options.upperSpecials),
                                ("lowerSpecials", options.lowerSpecials),
                                ("noBlues", options.noBlues)]:
            for name in (n for n, w in charDict.items()
                         if w and n not in font.getGlyphList()):
                log.warning("%s glyph named in fontinfo is " % label +
                            "not in font: %s" % name)

        # For Type1 name keyed fonts, psautohint supports defining
        # different alignment zones for different glyphs by FontDict
        # entries in the fontinfo file. This is NOT supported for CID
        # or CFF2 fonts, as these have FDArrays, can can truly support
        # different Font.Dict.Private Dicts for different groups of glyphs.
        if self.isVF:
            (self.fdGlyphDict,
             self.fontDictLists) = get_fontinfo_list_withFDArray(options, font,
                self.glyphNameList, True)
        else:
            if font.hasFDArray():
                getfil = get_fontinfo_list_withFDArray
            else:
                getfil = get_fontinfo_list_withFontInfo
            fdls = self.fontDictLists = []
            self.fdGlyphDict = None
            for f in self.fontInstances:
                log.info("Getting FDDicts for font %s" % f.desc)
                gd, fdl = getfil(options, f.font, self.glyphNameList)
                if self.fdGlyphDict is None:
                    self.fdGlyphDict = gd
                elif not self.equalDicts(self.fdGlyphDict, gd):
                    log.error("fdIndexes in font %s different " % font.desc +
                              "from those in font %s" % f.desc)
                fdls.append(fdl)

    def equalDicts(self, d1, d2):
        # This allows for sparse masters, just verifying that if a glyph name
        # is in both dictionaries it maps to the same index.
        return all((d1[k] == d2[k] for k in d1 if k in d2))

    def hintStatus(self, name, hgt):
        an = self.options.nameAliases.get(name, name)
        if hgt is None:
            log.warning("%s: Could not hint!", an)
            return False
        hs = [g.hasHints(both=True) for g in hgt if g is not None]
        if False in hs:
            if len(hgt) == 1:
                log.info("%s: No hints added!", an)
                return False
            elif True in hs:
                log.info("%s: Hints only added to some masters!", an)
                return True
            else:
                log.info("%s: No hints added to any master!", an)
                return False
        return True

    class glyphiter:
        def __init__(self, parent):
            self.fw = parent
            self.gnit = parent.glyphNameList.__iter__()
            self.notFound = 0

        def __next__(self):
            # gnit's StopIteration exception stops this iteration
            stillLooking = True
            while stillLooking:
                stillLooking = False
                name = self.gnit.__next__()
                if self.fw.reportOnly and name == '.notdef':
                    stillLooking = True
                    continue
                if self.fw.isVF:
                    gt = self.fw.fontInstances[0].font.get_vf_glyphs(name)
                    for i, g in enumerate(gt):
                        if g is not None:
                            g.setMasterDesc("Master %d" % i)
                else:
                    gt = tuple((get_glyph(self.fw.options, f.font, name)
                                for f in self.fw.fontInstances))
                    for i, g in enumerate(gt):
                        if g is not None:
                            g.setMasterDesc(self.fw.fontInstances[i].desc)
                if True not in (g is not None for g in gt):
                    self.notFound += 1
                    stillLooking = True
            self.fw.notFound = self.notFound
            fdIndex = self.fw.fdGlyphDict.get(name, 0)
            return name, gt, fdIndex

    def __iter__(self):
        return self.glyphiter(self)

    def hint(self):
        hintedAny = False

        report = Report() if self.reportOnly else None

        pcount = self.options.process_count
        if pcount is None:
            pcount = os.cpu_count()
        if pcount < 0:
            pcount = os.cpu_count() - pcount
            if pcount < 0:
                pcount = 1
        if pcount > self.numGlyphs():
            pcount = self.numGlyphs()

        pool = None
        lt = None
        try:
            if pcount == 1:
                glyphHinter.initialize(self.options, self.fontDictLists)
                gmap = map(glyphHinter.hint, self)
            else:
                # set_start_method('spawn')
                manager = Manager()
                logQueue = manager.Queue(-1)
                lt = Thread(target=log_receiver, args=(logQueue,))
                lt.start()
                pool = Pool(pcount, initializer=glyphHinter.initialize,
                            initargs=(self.options, self.fontDictLists,
                                      logQueue))
                if report is not None:
                    # Retain glyph ordering for reporting purposes
                    gmap = pool.imap(glyphHinter.hint, self)
                else:
                    gmap = pool.imap_unordered(glyphHinter.hint, self)

            for name, r in gmap:
                if isinstance(r, GlyphReport):
                    if report is not None:
                        report.glyphs[name] = r
                else:
                    hasHints = self.hintStatus(name, r)
                    if r is None:
                        r = [None]*len(self.fontInstances)
                    if not self.options.logOnly:
                        if hasHints:
                            hintedAny = True
                        font = self.fontInstances[0].font
                        for i, new_glyph in enumerate(r):
                            if i > 0 and not self.isVF:
                                font = self.fontInstances[i].font
                            font.updateFromGlyph(new_glyph, name)
                        if self.isVF:
                            font.merge_hinted_glyphs(name)

            if self.notFound:
                log.info("Skipped %s of %s glyphs.", self.notFound,
                         self.numGlyphs())

            if report is not None:
                report.save(self.fontInstances[0].outpath, self.options)
            elif not hintedAny:
                log.info("No glyphs were hinted.")

            if pool is not None:
                pool.close()
                pool.join()
                logQueue.put(None)
                lt.join()
        finally:
            if pool is not None:
                pool.terminate()
                pool.join()
            if lt is not None:
                logQueue.put(None)
                lt.join()

        return hintedAny

    def save(self):
        for f in self.fontInstances:
            log.info("Saving font file %s with new hints..." % f.outpath)
            f.font.save(f.outpath)

    def close(self):
        for f in self.fontInstances:
            log.info("Closing font file %s without saving." % f.outpath)
            f.font.close()


def openFile(path, options):
    font_format = get_font_format(path)
    if font_format is None:
        raise FontParseError(f"{path} is not a supported font format")

    if font_format == "UFO":
        font = UFOFontData(path, options.logOnly, options.writeToDefaultLayer)
    else:
        font = CFFFontData(path, font_format)

    return font


def get_outpath(options, font_path, i):
    if options.outputPaths is not None and i < len(options.outputPaths):
        outpath = options.outputPaths[i]
    else:
        outpath = font_path
    return outpath


def hintFiles(options):
    fontInstances = []
    # If there is a reference font, prepend it to font paths.
    # It must be the first font in the list, code below assumes that.
    if options.reference_font:
        font = openFile(options.reference_font, options)
        if hasattr(font, 'ttFont'):
            assert 'fvar' not in font.ttFont, ("Can't use a CFF2 VF font as a "
                                               "default font in a set of MM "
                                               "fonts.")
        fontInstances.append(FontInstance(font,
                             os.path.basename(options.reference_font),
                             options.reference_font))

    # Open the rest of the fonts and handle output paths.
    for i, path in enumerate(options.inputPaths):
        fontInstances.append(FontInstance(openFile(path, options),
                             os.path.basename(path),
                             get_outpath(options, path, i)))

    noFlex = options.noFlex
    if fontInstances and options.reference_font:
        log.info("Hinting fonts with reference %s. Start time: %s.",
                 fontInstances[0].desc, time.asctime())
        if fontInstances[0].font.isCID():
            options.noFlex = True
        fw = fontWrapper(options, fontInstances)
        if fw.hint():
            fw.save()
        else:
            fw.close()
        log.info("Done hinting fonts with reference %s. End time: %s.",
                 fontInstances[0].desc, time.asctime())
    else:
        for fi in fontInstances:
            log.info("Hinting font %s. Start time: %s.", fi.desc,
                     time.asctime())
            if fi[0].isCID():
                options.noFlex = True  # Flex hinting in CJK fonts does
                # bad things. For CFF fonts, being a CID font is a good
                # indicator of being CJK.
            else:
                options.noFlex = noFlex
            fw = fontWrapper(options, [fi])
            if fw.hint():
                fw.save()
            else:
                fw.close()
            log.info("Done hinting font %s. End time: %s.", fi.desc,
                     time.asctime())
