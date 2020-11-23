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

import ast
import logging
import os
import sys
import time
from collections import defaultdict, namedtuple

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
                    hstems[width] = count + 1
                    hstems_pos[hintpos] = width
            elif key == "VStem":
                width = x - y
                # avoid counting duplicates
                if hintpos not in vstems_pos:
                    count = vstems.get(width, 0)
                    vstems[width] = count + 1
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

    def assemble_rep_list(self, items_dict, count_dict):
        # item 0: stem/zone count
        # item 1: stem width/zone height
        # item 2: list of glyph names
        gorder = list(self.glyphs.keys())
        rep_list = []
        for item in items_dict:
            gnames = list(items_dict[item])
            # sort the names by the font's glyph order
            if len(gnames) > 1:
                gindexes = [gorder.index(gname) for gname in gnames]
                gnames = [x for _, x in sorted(zip(gindexes, gnames))]
            rep_list.append((count_dict[item], item, gnames))
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
         'B': [{46.0: 2, 46.5: 2, 47.0: 1}, {94.0: 1, 100.0: 1}, {}, {}],
         'C': [{50.0: 2}, {109.0: 1}, {}, {}],
         'D': [{46.0: 1, 46.5: 2, 47.0: 1}, {95.0: 1, 109.0: 1}, {}, {}],
         'E': [{46.5: 2, 47.0: 1, 50.0: 2, 177.0: 1, 178.0: 1},
               {46.0: 1, 75.5: 2, 95.0: 1}, {}, {}],
         'F': [{46.5: 2, 47.0: 1, 50.0: 1, 177.0: 1},
               {46.0: 1, 60.0: 1, 75.5: 1, 95.0: 1}, {}, {}],
         'G': [{43.0: 1, 44.5: 1, 50.0: 1}, {94.0: 1, 109.0: 1}, {}, {}]
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
        headers = (["count    width    glyphs\n"] * 2 +
                   ["count   height    glyphs\n"] * 2)

        for i, item in enumerate(items):
            reps, sortFunc = item
            if not reps:
                continue
            fName = f'{path}{suffixes[i]}'
            title = titles[i]
            header = headers[i]
            with open(fName, "w") as fp:
                fp.write(title)
                fp.write(header)
                reps.sort(key=sortFunc)
                for rep in reps:
                    gnames = ' '.join(rep[2])
                    fp.write(f"{rep[0]:5}    {rep[1]:5}    [{gnames}]\n")
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
            glyphList = [n for n in fontGlyphList if n not in glyphList]
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


def get_bez_glyphs(options, font, glyph_list):
    glyphs = {}

    for name in glyph_list:
        # Convert to bez format
        try:
            bez_glyph = font.convertToBez(name, options.read_hints,
                                          options.round_coords,
                                          options.hintAll)

            if bez_glyph is None or "mt" not in bez_glyph:
                # skip empty glyphs.
                continue
        except KeyError:
            # Source fonts may be sparse, e.g. be a subset of the
            # reference font.
            bez_glyph = None
        glyphs[name] = GlyphEntry(bez_glyph, font)

    total = len(glyph_list)
    processed = len(glyphs)
    if processed != total:
        log.info("Skipped %s of %s glyphs.", total - processed, total)

    return glyphs


def get_fontinfo_list(options, font, glyph_list, is_var=False):

    # Check counter glyphs, if any.
    counter_glyphs = options.hCounterGlyphs + options.vCounterGlyphs
    if counter_glyphs:
        missing = [n for n in counter_glyphs if n not in font.getGlyphList()]
        if missing:
            log.error("H/VCounterChars glyph named in fontinfo is "
                      "not in font: %s", missing)

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
    lastFDIndex = None
    fontinfo_list = {}
    for name in glyph_list:
        # get new fontinfo string if FDarray index has changed,
        # as each FontDict has different alignment zones.
        fdIndex = font.getfdIndex(name)
        if not fdIndex == lastFDIndex:
            lastFDIndex = fdIndex
            fddict = font.getFontInfo(options.allow_no_blues,
                                      options.noFlex,
                                      options.vCounterGlyphs,
                                      options.hCounterGlyphs,
                                      fdIndex)
            fontinfo = fddict.getFontInfo()
        fontinfo_list[name] = (fontinfo, None, None)

    return fontinfo_list


def get_fontinfo_list_withFontInfo(options, font, glyph_list):
    # Build alignment zone string
    if options.printDefaultFDDict:
        print("Showing default FDDict Values:")
        fddict = font.getFontInfo(options.allow_no_blues,
                                  options.noFlex,
                                  options.vCounterGlyphs,
                                  options.hCounterGlyphs)
        # Exit by printing default FDDict with all lines indented by one tab
        sys.exit("\t" + "\n\t".join(fddict.getFontInfo().split("\n")))

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
                print(fontDict.getFontInfo())
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
    fontinfo_list = {}
    for name in glyph_list:
        if fdglyphdict is not None:
            fdIndex = fdglyphdict[name][0]
            if lastFDIndex != fdIndex:
                lastFDIndex = fdIndex
                fddict = fontDictList[fdIndex]
                fontinfo = fddict.getFontInfo()

        fontinfo_list[name] = (fontinfo, fddict, fdglyphdict)

    return fontinfo_list


class MMHintInfo:
    def __init__(self, glyph_name=None):
        self.defined = False
        self.h_order = None
        self.v_order = None
        self.hint_masks = []
        self.glyph_name = glyph_name
        # bad_hint_idxs contains the hint pair indices for all the bad
        # hint pairs in any of the fonts for the current glyph.
        self.bad_hint_idxs = set()
        self.cntr_masks = []
        self.new_cntr_masks = []
        self.glyph_programs = None

    @property
    def needs_fix(self):
        return len(self.bad_hint_idxs) > 0


def hint_glyph(options, name, bez_glyph, fontinfo):
    try:
        hinted = hint_bez_glyph(fontinfo, bez_glyph, options.allowChanges,
                                not options.noHintSub, options.round_coords,
                                options.report_zones, options.report_stems,
                                options.report_all_stems)
    except PsAutoHintCError:
        raise ACHintError("%s: Failure in processing outline data." %
                          options.nameAliases.get(name, name))

    return hinted


def hint_compatible_glyphs(options, name, bez_glyphs, masters, fontinfo):
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
        hinted_ref_bez = hint_glyph(options, name, bez_glyphs[0], fontinfo)
        for i, bez in enumerate(bez_glyphs[1:]):
            if bez is None:
                out = [hinted_ref_bez, None]
            else:
                in_bez = [hinted_ref_bez, bez]
                in_masters = [ref_master, masters[i + 1]]
                out = hint_compatible_bez_glyphs(fontinfo,
                                                 in_bez,
                                                 in_masters)
                # out is [hinted_ref_bez, new_hinted_region_bez]
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


GlyphEntry = namedtuple("GlyphEntry", "bez_data,font")


def hint_font(options, font, glyph_list, fontinfo_list):
    aliases = options.nameAliases

    hinted = {}
    glyphs = get_bez_glyphs(options, font, glyph_list)
    for name in glyphs:
        g_entry = glyphs[name]
        fontinfo, fddict, fdglyphdict = fontinfo_list[name]

        if fdglyphdict:
            log.info("%s: Begin hinting (using fdDict %s).",
                     aliases.get(name, name), fddict.DictName)
        else:
            log.info("%s: Begin hinting.", aliases.get(name, name))

        # Call auto-hint library on bez string.
        new_bez_glyph = hint_glyph(options, name, g_entry.bez_data, fontinfo)
        options.baseMaster[name] = new_bez_glyph

        if not ("ry" in new_bez_glyph or "rb" in new_bez_glyph or
                "rm" in new_bez_glyph or "rv" in new_bez_glyph):
            log.info("%s: No hints added!", aliases.get(name, name))
            continue

        if options.logOnly:
            continue

        hinted[name] = GlyphEntry(new_bez_glyph, font)

    return hinted


def hint_compatible_fonts(options, paths, glyphs,
                          fontinfo_list):
    # glyphs is a list of dicts, one per font. Each dict is keyed by glyph name
    # and references a tuple of (src bez file, font)
    aliases = options.nameAliases

    hinted_glyphs = set()
    reference_font = None

    for name in glyphs[0]:
        fontinfo, _, _ = fontinfo_list[name]

        log.info("%s: Begin hinting.", aliases.get(name, name))

        masters = [os.path.basename(path) for path in paths]
        bez_glyphs = [g[name].bez_data for g in glyphs]
        new_bez_glyphs = hint_compatible_glyphs(options, name, bez_glyphs,
                                                masters, fontinfo)
        if options.logOnly:
            continue

        if reference_font is None:
            fonts = [g[name].font for g in glyphs]
            reference_font = fonts[0]
        mm_hint_info = MMHintInfo()

        for i, new_bez_glyph in enumerate(new_bez_glyphs):
            if new_bez_glyph is not None:
                g_entry = glyphs[i][name]
                g_entry.font.updateFromBez(new_bez_glyph, name, mm_hint_info)

        hinted_glyphs.add(name)
        # Now check if we need to fix any hint lists.
        if mm_hint_info.needs_fix:
            reference_font.fix_glyph_hints(name, mm_hint_info,
                                           is_reference_font=True)
            for font in fonts[1:]:
                font.fix_glyph_hints(name,
                                     mm_hint_info,
                                     is_reference_font=False)

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
        fontinfo, _, _ = fontinfo_list[name]
        log.info("%s: Begin hinting.", aliases.get(name, name))

        bez_glyphs = font.get_vf_bez_glyphs(name)
        num_masters = len(bez_glyphs)
        masters = [f"Master-{i}" for i in range(num_masters)]
        new_bez_glyphs = hint_compatible_glyphs(options, name, bez_glyphs,
                                                masters, fontinfo)
        if None in new_bez_glyphs:
            log.info(f"Error while hinting glyph {name}.")
            continue
        if options.logOnly:
            continue
        hinted_glyphs.add(name)

        # First, convert bez to fontTools T2 programs,
        # and check if any hints conflict.
        mm_hint_info = MMHintInfo()
        for i, new_bez_glyph in enumerate(new_bez_glyphs):
            if new_bez_glyph is not None:
                font.updateFromBez(new_bez_glyph, name, mm_hint_info)

        # Now check if we need to fix any hint lists.
        if mm_hint_info.needs_fix:
            font.fix_glyph_hints(name, mm_hint_info)

        # Now merge the programs into a singel CFF2 charstring program
        font.merge_hinted_glyphs(name)

    if hinted_glyphs:
        log.info(f"Saving font file {out_path} with new hints...")
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
        glyphs.append(get_bez_glyphs(options, font, glyph_names))

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

        glyph_names = get_glyph_list(options, font, path)
        fontinfo_list = get_fontinfo_list(options, font, glyph_names)

        log.info("Hinting font %s. Start time: %s.", path, time.asctime())

        if options.report_zones or options.report_stems:
            reports = get_glyph_reports(options, font, glyph_names,
                                        fontinfo_list)
            reports.save(outpath)
        else:
            hinted = hint_font(options, font, glyph_names, fontinfo_list)
            if hinted:
                log.info("Saving font file with new hints...")
                for name in hinted:
                    g_entry = hinted[name]
                    font.updateFromBez(g_entry.bez_data, name)
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
