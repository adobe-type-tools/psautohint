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

import ast
import logging
import os
import re
import time
from collections import defaultdict

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
        self.report_zones = False
        self.report_stems = False
        self.report_all_stems = False
        self.use_autohintexe = False


class ACHintError(Exception):
    pass


class GlyphReports:
    def __init__(self):
        self.glyphs = {}

    def addGlyphReport(self, glyphName, reportString):
        hstems = {}
        vstems = {}
        hstems_pos = {}
        vstems_pos = {}
        char_zones = {}
        stem_zone_stems = {}
        self.glyphs[glyphName] = [hstems, vstems, char_zones, stem_zone_stems]

        lines = reportString.splitlines()
        for line in lines:
            tokens = line.split()
            key = tokens[0]
            x = ast.literal_eval(tokens[3])
            y = ast.literal_eval(tokens[5])
            hintpos = "%s %s" % (x, y)
            if key == "charZone":
                char_zones[hintpos] = (x, y)
            elif key == "stemZone":
                stem_zone_stems[hintpos] = (x, y)
            elif key == "HStem":
                width = x - y
                # avoid counting duplicates
                if hintpos not in hstems_pos:
                    count = hstems.get(width, 0)
                    hstems[width] = count+1
                    hstems_pos[hintpos] = width
            elif key == "VStem":
                width = x - y
                # avoid counting duplicates
                if hintpos not in vstems_pos:
                    count = vstems.get(width, 0)
                    vstems[width] = count+1
                    vstems_pos[hintpos] = width
            else:
                raise FontParseError("Found unknown keyword %s in report file "
                                     "for glyph %s." % (key, glyphName))

    @staticmethod
    def round_value(val):
        if val >= 0:
            return int(val + 0.5)
        else:
            return int(val - 0.5)

    def parse_stem_dict(self, stem_dict):
        """
        stem_dict: {45.5: 1, 47.0: 2}
        """
        # key: stem width
        # value: stem count
        width_dict = defaultdict(int)
        for width, count in stem_dict.items():
            width = self.round_value(width)
            width_dict[width] += count
        return width_dict

    def parse_zone_dicts(self, char_dict, stem_dict):
        all_zones_dict = char_dict.copy()
        all_zones_dict.update(stem_dict)
        # key: zone height
        # value: zone count
        top_dict = defaultdict(int)
        bot_dict = defaultdict(int)
        for top, bot in all_zones_dict.values():
            top = self.round_value(top)
            top_dict[top] += 1
            bot = self.round_value(bot)
            bot_dict[bot] += 1
        return top_dict, bot_dict

    @staticmethod
    def assemble_rep_list(items_dict, count_dict):
        # item 0: stem/zone count
        # item 1: stem width/zone height
        # item 2: list of glyph names
        rep_list = []
        for item in items_dict:
            rep_list.append((count_dict[item], item, sorted(items_dict[item])))
        return rep_list

    def _get_lists(self):
        """
        self.glyphs is a dictionary:
            key: glyph name
            value: list of 4 dictionaries
                   hstems
                   vstems
                   char_zones
                   stem_zone_stems
        {
         'A': [{45.5: 1, 47.0: 2}, {229.0: 1}, {}, {}],
         'B': [{46.0: 2, 46.5: 2, 47.0: 1}, {94.0: 1, 95.0: 1, 100.0: 1}, {}, {}],
         'C': [{50.0: 2}, {109.0: 1}, {}, {}],
         'D': [{46.0: 1, 46.5: 2, 47.0: 1}, {95.0: 1, 109.0: 1}, {}, {}],
         'E': [{46.5: 2, 47.0: 1, 50.0: 2, 177.0: 1, 178.0: 1},
               {46.0: 1, 75.5: 2, 95.0: 1}, {}, {}],
         'F': [{46.5: 2, 47.0: 1, 50.0: 1, 177.0: 1},
               {46.0: 1, 60.0: 1, 75.5: 1, 95.0: 1}, {}, {}],
         'G': [{43.0: 1, 44.5: 1, 50.0: 1, 51.0: 1}, {94.0: 1, 109.0: 1}, {}, {}]
        }
        """
        h_stem_items_dict = defaultdict(set)
        h_stem_count_dict = defaultdict(int)
        v_stem_items_dict = defaultdict(set)
        v_stem_count_dict = defaultdict(int)

        top_zone_items_dict = defaultdict(set)
        top_zone_count_dict = defaultdict(int)
        bot_zone_items_dict = defaultdict(set)
        bot_zone_count_dict = defaultdict(int)

        for gName, dicts in self.glyphs.items():
            hStemDict, vStemDict, charZoneDict, stemZoneStemDict = dicts

            glyph_h_stem_dict = self.parse_stem_dict(hStemDict)
            glyph_v_stem_dict = self.parse_stem_dict(vStemDict)

            for stem_width, stem_count in glyph_h_stem_dict.items():
                h_stem_items_dict[stem_width].add(gName)
                h_stem_count_dict[stem_width] += stem_count

            for stem_width, stem_count in glyph_v_stem_dict.items():
                v_stem_items_dict[stem_width].add(gName)
                v_stem_count_dict[stem_width] += stem_count

            glyph_top_zone_dict, glyph_bot_zone_dict = self.parse_zone_dicts(
                charZoneDict, stemZoneStemDict)

            for zone_height, zone_count in glyph_top_zone_dict.items():
                top_zone_items_dict[zone_height].add(gName)
                top_zone_count_dict[zone_height] += zone_count

            for zone_height, zone_count in glyph_bot_zone_dict.items():
                bot_zone_items_dict[zone_height].add(gName)
                bot_zone_count_dict[zone_height] += zone_count

        # item 0: stem count
        # item 1: stem width
        # item 2: list of glyph names
        h_stem_list = self.assemble_rep_list(
            h_stem_items_dict, h_stem_count_dict)

        v_stem_list = self.assemble_rep_list(
            v_stem_items_dict, v_stem_count_dict)

        # item 0: zone count
        # item 1: zone height
        # item 2: list of glyph names
        top_zone_list = self.assemble_rep_list(
            top_zone_items_dict, top_zone_count_dict)

        bot_zone_list = self.assemble_rep_list(
            bot_zone_items_dict, bot_zone_count_dict)

        return h_stem_list, v_stem_list, top_zone_list, bot_zone_list

    @staticmethod
    def _sort_count(t):
        """
        sort by: count (1st item), value (2nd item), list of glyph names (3rd
        item)
        """
        return (-t[0], -t[1], t[2])

    @staticmethod
    def _sort_val(t):
        """
        sort by: value (2nd item), count (1st item), list of glyph names (3rd
        item)
        """
        return (t[1], -t[0], t[2])

    @staticmethod
    def _sort_val_reversed(t):
        """
        sort by: value (2nd item), count (1st item), list of glyph names (3rd
        item)
        """
        return (-t[1], -t[0], t[2])

    def save(self, path):
        h_stems, v_stems, top_zones, bot_zones = self._get_lists()
        items = ([h_stems, self._sort_count],
                 [v_stems, self._sort_count],
                 [top_zones, self._sort_val_reversed],
                 [bot_zones, self._sort_val])
        atime = time.asctime()
        suffixes = (".hstm.txt", ".vstm.txt", ".top.txt", ".bot.txt")
        titles = ("Horizontal Stem List for %s on %s\n" % (path, atime),
                  "Vertical Stem List for %s on %s\n" % (path, atime),
                  "Top Zone List for %s on %s\n" % (path, atime),
                  "Bottom Zone List for %s on %s\n" % (path, atime),
                 )
        headers = ("Count\tWidth\tGlyph List\n",
                   "Count\tWidth\tGlyph List\n",
                   "Count\tTop Zone\tGlyph List\n",
                   "Count\tBottom Zone\tGlyph List\n",
                   )
        for i, item in enumerate(items):
            reps, sortFunc = item
            if not reps:
                continue
            fName = '{}{}'.format(path, suffixes[i])
            title = titles[i]
            header = headers[i]
            with open(fName, "w") as fp:
                fp.write(title)
                fp.write(header)
                reps.sort(key=sortFunc)
                for item in reps:
                    fp.write("%s\t%s\t%s\n" % (item[0], item[1], item[2]))
                log.info("Wrote %s" % fName)


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


def get_glyph_list(options, font, path):
    filename = os.path.basename(path)

    # filter specified list, if any, with font list.
    glyph_list = filterGlyphList(options, font.getGlyphList(), filename)
    if not glyph_list:
        raise FontParseError("Selected glyph list is empty for font <%s>." %
                             filename)

    return glyph_list


def get_bez_glyphs(options, font, glyph_list):
    glyphs = {}

    for name in glyph_list:
        # Convert to bez format
        try:
            bez_glyph, width = font.convertToBez(name, options.read_hints,
                                                 options.round_coords,
                                                 options.hintAll)
            if bez_glyph is None or "mt" not in bez_glyph:
                # skip empty glyphs.
                continue
        except KeyError:
            # Source fonts may be sparse, e.g. be a subset of the
            # reference font.
            bez_glyph = width = None
        glyphs[name] = (bez_glyph, width)

    total = len(glyph_list)
    processed = len(glyphs)
    if processed != total:
        log.info("Skipped %s of %s glyphs.", total - processed, total)

    return glyphs


def get_fontinfo_list(options, font, path, glyph_list):
    fontinfo_list = {}

    fontinfo = ""

    # Check counter glyphs, if any.
    counter_glyphs = options.hCounterGlyphs + options.vCounterGlyphs
    if counter_glyphs:
        missing = [n for n in counter_glyphs if n not in font.getGlyphList()]
        if missing:
            log.error("H/VCounterChars glyph named in fontinfo is "
                      "not in font: %s", missing)

    # Build alignment zone string
    if options.printDefaultFDDict:
        print("Showing default FDDict Values:")
        fddict = font.getFontInfo(options.allow_no_blues,
                                  options.noFlex,
                                  options.vCounterGlyphs,
                                  options.hCounterGlyphs)
        printFontInfo(str(fddict))
        return

    fdglyphdict, fontDictList = font.getfdInfo(options.allow_no_blues,
                                               options.noFlex,
                                               options.vCounterGlyphs,
                                               options.hCounterGlyphs,
                                               glyph_list)

    if options.printFDDictList:
        # Print the user defined FontDicts, and exit.
        if fdglyphdict:
            print("Showing user-defined FontDict Values:\n")
            for fi, fontDict in enumerate(fontDictList):
                print(fontDict.DictName)
                printFontInfo(str(fontDict))
                gnameList = []
                # item = [glyphName, [fdIndex, glyphListIndex]]
                itemList = sorted(fdglyphdict.items(), key=lambda x: x[1][1])
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
        return

    if fdglyphdict is None:
        fddict = fontDictList[0]
        fontinfo = fddict.getFontInfo()
    else:
        log.info("Using alternate FDDict global values from fontinfo "
                 "file for some glyphs.")

    lastFDIndex = None
    for name in glyph_list:
        # get new fontinfo string if FDarray index has changed,
        # as each FontDict has different alignment zones.
        if font.isCID():
            fdIndex = font.getfdIndex(name)
            if not fdIndex == lastFDIndex:
                lastFDIndex = fdIndex
                fddict = font.getFontInfo(options.allow_no_blues,
                                          options.noFlex,
                                          options.vCounterGlyphs,
                                          options.hCounterGlyphs,
                                          fdIndex)
                fontinfo = fddict.getFontInfo()
        else:
            if fdglyphdict is not None:
                fdIndex = fdglyphdict[name][0]
                if lastFDIndex != fdIndex:
                    lastFDIndex = fdIndex
                    fddict = fontDictList[fdIndex]
                    fontinfo = fddict.getFontInfo()

        fontinfo_list[name] = (fontinfo, fddict, fdglyphdict)

    return fontinfo_list


def hint_glyph(options, name, bez_glyph, fontinfo):
    try:
        hinted = hint_bez_glyph(fontinfo, bez_glyph, options.allowChanges,
                                not options.noHintSub, options.round_coords,
                                options.report_zones, options.report_stems,
                                options.report_all_stems,
                                options.use_autohintexe)
    except PsAutoHintCError:
        raise ACHintError("%s: Failure in processing outline data." %
                          options.nameAliases.get(name, name))

    return hinted


def hint_compatible_glyphs(options, name, bez_glyphs, masters, fontinfo):
    try:
        if False:
            # This is disabled because it causes crashes on the CI servers
            # which are not reproducible locally. The below branch is a hack to
            # avoid the crash and should be dropped once the crash is fixed,
            # https://github.com/adobe-type-tools/psautohint/pull/131
            hinted = hint_compatible_bez_glyphs(fontinfo, bez_glyphs, masters)
        else:
            hinted = []
            for i, bez in enumerate(bez_glyphs[1:]):
                if bez is None:
                    out = [bez_glyphs[0], None]
                else:
                    in_bez = [bez_glyphs[0], bez]
                    in_masters = [masters[0], masters[i + 1]]
                    out = hint_compatible_bez_glyphs(fontinfo, in_bez, in_masters)
                if i == 0:
                    hinted = out
                else:
                    hinted.append(out[1])
    except PsAutoHintCError:
        raise ACHintError("%s: Failure in processing outline data." %
                          options.nameAliases.get(name, name))

    return hinted


def get_glyph_reports(options, font, glyph_list, fontinfo_list):
    reports = GlyphReports()

    glyphs = get_bez_glyphs(options, font, glyph_list)
    for name in glyphs:
        if name == ".notdef":
            continue

        bez_glyph = glyphs[name][0]
        fontinfo = fontinfo_list[name][0]

        report = hint_glyph(options, name, bez_glyph, fontinfo)
        reports.addGlyphReport(name, report.strip())

    return reports


def hint_font(options, font, glyph_list, fontinfo_list):
    aliases = options.nameAliases

    hinted = {}
    glyphs = get_bez_glyphs(options, font, glyph_list)
    for name in glyphs:
        bez_glyph, width = glyphs[name]
        fontinfo, fddict, fdglyphdict = fontinfo_list[name]

        if fdglyphdict:
            log.info("%s: Begin hinting (using fdDict %s).",
                     aliases.get(name, name), fddict.DictName)
        else:
            log.info("%s: Begin hinting.", aliases.get(name, name))

        # Call auto-hint library on bez string.
        new_bez_glyph = hint_glyph(options, name, bez_glyph, fontinfo)
        options.baseMaster[name] = new_bez_glyph

        if not ("ry" in new_bez_glyph or "rb" in new_bez_glyph or
                "rm" in new_bez_glyph or "rv" in new_bez_glyph):
            log.info("No hints added!")

        if options.logOnly:
            continue

        hinted[name] = (new_bez_glyph, width)

    return hinted


def hint_compatible_fonts(options, fonts, paths, glyph_list, glyphs,
                          fontinfo_list):
    aliases = options.nameAliases

    hinted = {}
    for name in glyphs[0]:
        fontinfo, fddict, fdglyphdict = fontinfo_list[name]

        if fdglyphdict:
            log.info("%s: Begin hinting (using fdDict %s).",
                     aliases.get(name, name), fddict.DictName)
        else:
            log.info("%s: Begin hinting.", aliases.get(name, name))

        masters = [os.path.basename(path) for path in paths]
        bez_glyphs = [g[name][0] for g in glyphs]
        widths = [g[name][1] for g in glyphs]

        new_bez_glyphs = hint_compatible_glyphs(options, name, bez_glyphs,
                                                masters, fontinfo)

        if options.logOnly:
            continue

        hinted[name] = (new_bez_glyphs, widths)

    return hinted


def hintFiles(options):
    fonts = []
    paths = []
    outpaths = []

    # If there is a reference font, prepend it to font paths.
    # It must be the first font in the list, code below assumes that.
    if options.reference_font:
        fonts.append(openFile(options.reference_font, options))
        paths.append(options.reference_font)
        outpaths.append(options.reference_font)

    # Open the rest of the fonts and handle output paths.
    for i, path in enumerate(options.inputPaths):
        fonts.append(openFile(path, options))
        paths.append(path)
        if options.outputPaths is not None and i < len(options.outputPaths):
            outpaths.append(options.outputPaths[i])
        else:
            outpaths.append(path)

    if fonts[0].isCID():
        options.noFlex = True

    if options.reference_font:
        # We are doing compatible, AKA multiple master, hinting.
        log.info("Start time: %s.", time.asctime())

        # Get the glyphs and font info of the reference font, we assume the
        # fonts have the same glyph set, glyph dict and in general are
        # compatible. If not bad things will happen.
        glyph_list = get_glyph_list(options, fonts[0], paths[0])
        fontinfo_list = get_fontinfo_list(options, fonts[0], paths[0],
                                          glyph_list)

        glyphs_list = []
        for i, font in enumerate(fonts):
            path = paths[i]
            outpath = outpaths[i]

            if i == 0:
                # This is the reference font, pre-hint it as the rest of the
                # fonts will copy its hinting.
                glyphs_list.append(
                    hint_font(options, font, glyph_list, fontinfo_list))
            else:
                glyphs_list.append(get_bez_glyphs(options, font, glyph_list))

        # Run the compatible hinting, copying the hinting of the reference font
        # to the rest of the fonts.
        hinted_glyphs_list = hint_compatible_fonts(options, fonts, paths,
                                                   glyph_list,
                                                   glyphs_list, fontinfo_list)
        if hinted_glyphs_list:
            log.info("Saving font files with new hints...")
            for name in hinted_glyphs_list:
                bez_glyphs, widths = hinted_glyphs_list[name]
                for i, font in enumerate(fonts):
                    if bez_glyphs[i]:
                        font.updateFromBez(bez_glyphs[i], name, widths[i])
            for i, font in enumerate(fonts):
                font.save(outpaths[i])
        else:
            log.info("No glyphs were hinted.")
            font.close()

        log.info("End time: %s.", time.asctime())
    else:
        # Regular hints, just iterate over the fonts and hint each one.
        for i, font in enumerate(fonts):
            path = paths[i]
            outpath = outpaths[i]

            glyph_list = get_glyph_list(options, font, path)
            fontinfo_list = get_fontinfo_list(options, font, path, glyph_list)

            log.info("Hinting font %s. Start time: %s.", path, time.asctime())

            if options.report_zones or options.report_stems:
                reports = get_glyph_reports(options, font, glyph_list,
                                            fontinfo_list)
                reports.save(outpath)
            else:
                hinted = hint_font(options, font, glyph_list, fontinfo_list)
                if hinted:
                    log.info("Saving font file with new hints...")
                    for name in hinted:
                        bez_glyph, width = hinted[name]
                        font.updateFromBez(bez_glyph, name, width)
                    font.save(outpath)
                else:
                    log.info("No glyphs were hinted.")
                    font.close()

            log.info("Done with font %s. End time: %s.", path, time.asctime())
