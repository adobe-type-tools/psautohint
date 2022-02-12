# Copyright 2016 Adobe. All rights reserved.

# Methods:

# Parse args. If glyphlist is from file, read in entire file as single string,
# and remove all white space, then parse out glyph-names and GID's.

import logging
import os
import sys
import time

from .otfFont import CFFFontData
from .ufoFont import UFOFontData
from .report import Report
from .hinter import glyphHinter

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
            glyphNames = getGlyphNames(glyphTag, fontGlyphList, fontFileName)
            if glyphNames is not None:
                glyphList.extend(glyphNames)
        if options.excludeGlyphList:
            glyphList = [n for n in fontGlyphList if n not in glyphList]
    return glyphList


def openFile(path, options):
    font_format = get_font_format(path)
    if font_format is None:
        raise FontParseError(f"{path} is not a supported font format")

    if font_format == "UFO":
        font = UFOFontData(path, options.logOnly, options.writeToDefaultLayer)
    else:
        font = CFFFontData(path, font_format)

    return font


def get_glyph_list(options, font, path):
    filename = os.path.basename(path)

    # filter specified list, if any, with font list.
    glyph_list = filterGlyphList(options, font.getGlyphList(), filename)
    if not glyph_list:
        raise FontParseError("Selected glyph list is empty for font <%s>." %
                             filename)

    return glyph_list


def get_glyph(options, font, name):

    # Convert to internal format
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


def get_fontinfo_list(options, font, glyph_list, is_var=False):

    # Check for missing glyphs explicitly added via fontinfo or command line
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
    if font.hasFDArray():
        return get_fontinfo_list_withFDArray(options, font, glyph_list, is_var)
    else:
        return get_fontinfo_list_withFontInfo(options, font, glyph_list)


def get_fontinfo_list_withFDArray(options, font, glyph_list, is_var=False):
    fdGlyphDict = {}
    fontDictList = []
    filen = 0
    for name in glyph_list:
        fdIndex = font.getfdIndex(name)
        if fdIndex >= l:
            fontDictList.extend([None] * (fdindex-l+1))
        if fontDictList[fdIndex] is None:
            fddict = font.getFontInfo(options.allow_no_blues,
                                      options.noFlex,
                                      options.vCounterGlyphs,
                                      options.hCounterGlyphs,
                                      fdIndex)
            fontDictList[fdIndex] = fddict
        if fdIndex != 0:
            fdGlyphDict[name] = fdIndex

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
        log.info("Using alternate FDDict global values from fontinfo "
                 "file for some glyphs.")

    return fdGlyphDict, fontDictList

#class fontState:
    #"""
    #Stores references to one or more related master font objects,
    #extracts glyphs from those objects by name, and updates the
    #stored glyphs in those objects by request
    #"""
    #def __init__(self, *args):
        #self.fontInstances = (*args)
#
    #def setGlyphList(self, gl):
        #self.gl = gl
#
    #class glyphiter:
        #def __init__(self, fs, gl):
            #self.fs = fs
            #self.it = gl.__iter__()
            #self.notFound = 0
#
        #def __next__(self):
            #name = self.it.__next__()
            #self.pos = self.gd.next(self.pos)
            #if self.pos is None:
                #raise StopIteration
            #return self.pos
#
    #def __iter__(self):
        #return self.glyphiter(self)


def hint_compatible_glyphs(hintadapt, name, glyphs, masters):
    # This function is used by both
    #   hint_with_reference_font->hint_compatible_fonts
    # and hint_vf_font.
    try:
        ref_master = masters[0]
        # *************************************************************
        # *********** DO NOT DELETE THIS COMMENTED-OUT CODE ***********
        # If you're tempted to "clean up", work on solving
        # https://github.com/adobe-type-tools/psautohint/issues/202
        # first, then you can uncomment the "hint_compatible_bez_glyphs"
        # line and remove this and other related comments, as well as
        # the workaround block following "# else:", below. Thanks.
        # *************************************************************
        #
        # if False:
        #     # This is disabled because it causes crashes on the CI servers
        #     # which are not reproducible locally. The branch below is a hack
        #     # to avoid the crash and should be dropped once the crash is
        #     # fixed, https://github.com/adobe-type-tools/psautohint/pull/131
        #     hinted = hint_compatible_bez_glyphs(
        #         fontinfo, bez_glyphs, masters)
        # *** see https://github.com/adobe-type-tools/psautohint/issues/202 ***
        # else:
        hinted = []
        hintadapt.hint(name, glyphs[0])
        for i, gl in enumerate(glyphs[1:]):
            if gl is None:
                out = [glyphs[0], None]
            else:
                # in_glyph = [glyphs[0], gl]
                in_masters = [ref_master, masters[i + 1]]
                changed = hint_compatible_glyphs_impl(fddict, gl, in_masters)
                # out is [hinted_ref_glyph, new_hinted_region_glyph]
            if i == 0:
                hinted = out
            else:
                hinted.append(out[1])
    # XXX except PsAutoHintCError:
    except:
        raise ACHintError("%s: Failure in processing outline data." %
                          options.nameAliases.get(name, name))

    return hinted


def log_dict(fddict, an, name, task="hinting"):
    if an != name:
        log.info("%s (%s): Begin %s (using fdDict %s).",
                 an, name, task, fddict.DictName)
    else:
        log.info("%s: Begin glyph %s (using fdDict %s).",
                 name, task, fddict.DictName)

def hint_font(options, font, glyph_list, returnReport=False):
    aliases = options.nameAliases

    fdGlyphDict, fontDictList = get_fontinfo_list(options, font,
                                                  glyph_list)

    hintadapt = glyphHinter(options, fontDictList)
    hinted = {}
    notFound = 0
    if returnReport:
        report = Report()
        task = 'analysis'
    else:
        report = None
        task = "hinting"

    #glyphs = get_glyphs(options, font, glyph_list)
    for name in glyph_list:
        if returnReport and name == '.notdef':
            continue

        g = get_glyph(options, font, name)

        if g is None:
            notFound += 1
            continue
        
        fdIndex = fdGlyphDict.get(name, 0)
        an = aliases.get(name, name)

        log_dict(fontDictList[fdIndex], an, name, task)

        # Call auto-hint library on glyph
        r, changed = hintadapt.hint(name, g, fdIndex)

        if returnReport:
            report.glyphs[name] = r
        elif changed:
            if not g.hasHints(both=True):
                log.info("%s: No hints added!", an)
            if not options.logOnly:
                hinted[name] = r

    if notFound:
        log.info("Skipped %s of %s glyphs.", notFound, len(glyph_list))

    if returnReport:
        return report
    else:
        return hinted


def hint_compatible_fonts(options, paths, glyphs, fontinfo_list):
    # glyphs is a list of dicts, one per font. Each dict is keyed by glyph name
    # and references a tuple of (src bez file, font)
    aliases = options.nameAliases

    hinted_glyphs = set()
    reference_font = None

    for name in glyphs[0]:
        fddict = fontinfo_list[name]

        log_dict(fddict, aliases.get(name, name))

        masters = [os.path.basename(path) for path in paths]
        glyph_list = [g[name].data for g in glyphs]
        new_glyphs = hint_compatible_glyphs(options, name, glyph_list,
                                            masters, fddict)
        if options.logOnly:
            continue

        if reference_font is None:
            fonts = [g[name].font for g in glyphs]
            reference_font = fonts[0]

        for i, new_glyph in enumerate(new_glyphs):
            if new_glyph is not None:
                ge = glyphs[i][name]
                ge.font.updateFromGlyph(new_glyph, name)

        hinted_glyphs.add(name)

    return len(hinted_glyphs) > 0


def hint_vf_font(options, font_path, out_path):
    font = openFile(font_path, options)
    options.noFlex = True  # work around for incompatibel flex args.
    aliases = options.nameAliases
    glyph_names = get_glyph_list(options, font, font_path)
    log.info("Hinting font %s. Start time: %s.", font_path, time.asctime())
    fontinfo_list = get_fontinfo_list(options, font, glyph_names, True)
    hinted_glyphs = set()

    for name in glyph_names:
        fddict = fontinfo_list[name]

        log_dict(fddict, aliases.get(name, name))

        glyph_list = font.get_vf_glyphs(name)
        num_masters = len(glyph_list)
        masters = [f"Master-{i}" for i in range(num_masters)]
        new_glyphs = hint_compatible_glyphs(options, name, glyph_list,
                                            masters, fddict)
        if None in new_glyphs:
            log.info(f"Error while hinting glyph {name}.")
            continue
        if options.logOnly:
            continue
        hinted_glyphs.add(name)

        # First, convert bez to fontTools T2 programs,
        # and check if any hints conflict.
        for i, new_glyph in enumerate(new_glyphs):
            if new_glyph is not None:
                font.updateFromGlyph(new_glyph, name)

        # Now merge the programs into a singel CFF2 charstring program
        font.merge_hinted_glyphs(name)

    if hinted_glyphs:
        log.info("Saving font file {out_path} with new hints...")
        font.save(out_path)
    else:
        log.info("No glyphs were hinted.")
        font.close()

    log.info("Done with font %s. End time: %s.", font_path, time.asctime())


def hint_with_reference_font(options, fonts, paths, outpaths):
    # We are doing compatible, AKA multiple master, hinting.
    log.info("Start time: %s.", time.asctime())
    options.noFlex = True  # work-around for mm-hinting

    # Get the glyphs and font info of the reference font. We assume the
    # fonts have the same glyph set, glyph dict and in general are
    # compatible. If not bad things will happen.
    glyph_names = get_glyph_list(options, fonts[0], paths[0])
    fontinfo_list = get_fontinfo_list(options, fonts[0], glyph_names)

    glyphs = []
    for i, font in enumerate(fonts):
        glyphs.append(get_glyphs(options, font, glyph_names))

    have_hinted_glyphs = hint_compatible_fonts(options, paths,
                                               glyphs, fontinfo_list)
    if have_hinted_glyphs:
        log.info("Saving font files with new hints...")

        for i, font in enumerate(fonts):
            font.save(outpaths[i])
    else:
        log.info("No glyphs were hinted.")
        font.close()

    log.info("End time: %s.", time.asctime())


def hint_regular_fonts(options, fonts, paths, outpaths):
    # Regular fonts, just iterate over the list and hint each one.
    for i, font in enumerate(fonts):
        path = paths[i]
        outpath = outpaths[i]

        glyph_list = get_glyph_list(options, font, path)

        log.info("Hinting font %s. Start time: %s.", path, time.asctime())

        returnReport = options.report_zones or options.report_stems

        r = hint_font(options, font, glyph_list, returnReport)

        if returnReport:
            report = r
            report.save(outpath, options)
        else:
            hinted = r
            if hinted:
                log.info("Saving font file with new hints...")
                for name, glyph in hinted.items():
                    font.updateFromGlyph(glyph, name)
                font.save(outpath)
            else:
                log.info("No glyphs were hinted.")
                font.close()

        log.info("Done with font %s. End time: %s.", path, time.asctime())


def get_outpath(options, font_path, i):
    if options.outputPaths is not None and i < len(options.outputPaths):
        outpath = options.outputPaths[i]
    else:
        outpath = font_path
    return outpath


def hintFiles(options):
    fonts = []
    paths = []
    outpaths = []
    # If there is a reference font, prepend it to font paths.
    # It must be the first font in the list, code below assumes that.
    if options.reference_font:
        font = openFile(options.reference_font, options)
        fonts.append(font)
        paths.append(options.reference_font)
        outpaths.append(options.reference_font)
        if hasattr(font, 'ttFont'):
            assert 'fvar' not in font.ttFont, ("Can't use a CFF2 VF font as a "
                                               "default font in a set of MM "
                                               "fonts.")

    # Open the rest of the fonts and handle output paths.
    for i, path in enumerate(options.inputPaths):
        font = openFile(path, options)
        out_path = get_outpath(options, path, i)
        if hasattr(font, 'ttFont') and 'fvar' in font.ttFont:
            assert not options.report_zones or options.report_stems
            # Certainly not supported now, also I think it only makes sense
            # to ask for zone reports for the source fonts for the VF font.
            # You can't easily change blue values in a VF font.
            hint_vf_font(options, path, out_path)
        else:
            fonts.append(font)
            paths.append(path)
            outpaths.append(out_path)

    if fonts:
        if fonts[0].isCID():
            options.noFlex = True  # Flex hinting in CJK fonts doed bad things.
            # For CFF fonts, being a CID font is a good indicator of being CJK.

        if options.reference_font:
            hint_with_reference_font(options, fonts, paths, outpaths)
        else:
            hint_regular_fonts(options, fonts, paths, outpaths)
