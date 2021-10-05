# Copyright 2016 Adobe. All rights reserved.

# Methods:

# Parse args. If glyphlist is from file, read in entire file as single string,
# and remove all white space, then parse out glyph-names and GID's.

import logging
import os
import sys
import time
from copy import copy, deepcopy
from collections import defaultdict, namedtuple

from .otfFont import CFFFontData
from .ufoFont import UFOFontData
from .hintstate import links
from .hinter import hhinter, vhinter

from . import (get_font_format, FontParseError)

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


class Report:
    def __init__(self):
        self.glyphs = {}

    class Glyph:
        """Subclass to store stem and zone data from a particular glyph"""
        def __init__(self, name=None, all_stems=False):
            self.name = name
            self.hstems = {}
            self.vstems = {}
            self.hstems_pos = set()
            self.vstems_pos = set()
            self.char_zones = set()
            self.stem_zone_stems = set()
            self.all_stems = all_stems

        def charZone(self, l, u):
            self.char_zones.add((l, u))

        def stemZone(self, l, u):
            self.stem_zone_stems.add((l, u))

        def stem(self, l, u, isLine, isV=False):
            if not isLine and not self.all_stems:
                return
            if isV:
                stems, stems_pos = self.vstems, self.vstems_pos
            else:
                stems, stems_pos = self.hstems, self.hstems_pos
            pair = (l, u)
            if pair not in stems_pos:
                width = pair[1] - pair[0]
                stems[width] = stems.get(width, 0) + 1
                stems_pos.add(pair)

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
        for bot, top in all_zones_dict:
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

    def _get_lists(self, options):
        """
        self.glyphs is a dictionary:
            key: glyph name
            value: Reports.Glyph object
        """
        if not (options.report_stems or options.report_zones):
            return [], [], [], []

        h_stem_items_dict = defaultdict(set)
        h_stem_count_dict = defaultdict(int)
        v_stem_items_dict = defaultdict(set)
        v_stem_count_dict = defaultdict(int)

        top_zone_items_dict = defaultdict(set)
        top_zone_count_dict = defaultdict(int)
        bot_zone_items_dict = defaultdict(set)
        bot_zone_count_dict = defaultdict(int)

        for gName, gr in self.glyphs.items():
            if options.report_stems:
                glyph_h_stem_dict = self.parse_stem_dict(gr.hstems)
                glyph_v_stem_dict = self.parse_stem_dict(gr.vstems)

                for stem_width, stem_count in glyph_h_stem_dict.items():
                    h_stem_items_dict[stem_width].add(gName)
                    h_stem_count_dict[stem_width] += stem_count

                for stem_width, stem_count in glyph_v_stem_dict.items():
                    v_stem_items_dict[stem_width].add(gName)
                    v_stem_count_dict[stem_width] += stem_count

            if options.report_zones:
                tmp = self.parse_zone_dicts(gr.char_zones, gr.stem_zone_stems)
                glyph_top_zone_dict, glyph_bot_zone_dict = tmp

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


GlyphEntry = namedtuple("GlyphEntry", "glyph,font")


def get_glyphs(options, font, glyph_list):
    glyphs = {}

    for name in glyph_list:
        # Convert to internal format
        try:
            gl = font.convertToGlyphData(name, options.readHints,  # stems
                                         options.readHints,  # flex
                                         options.roundCoords,
                                         options.hintAll)

            if gl is None or gl.isEmpty():
                # skip empty glyphs.
                continue
        except KeyError:
            # Source fonts may be sparse, e.g. be a subset of the
            # reference font.
            gl = None
        glyphs[name] = GlyphEntry(gl, font)

    total = len(glyph_list)
    processed = len(glyphs)
    if processed != total:
        log.info("Skipped %s of %s glyphs.", total - processed, total)

    return glyphs


def get_fontinfo_list(options, font, glyph_list, is_var=False):

    # Check for missing glyphs explicitly added via fontinfo or command line
    for label, charDict in (("hCounterGlyphs", options.hCounterGlyphs),
                            ("vCounterGlyphs", options.vCounterGlyphs),
                            ("upperSpecials", options.upperSpecials),
                            ("lowerSpecials", options.lowerSpecials),
#                            ("newHintsOnMoveTo", options.newHintsOnMoveTo)
                            ("noBlues", options.noBlues)):
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
    fontinfo_list = {}
    fddict_cache = {}
    for name in glyph_list:
        fdIndex = font.getfdIndex(name)
        fddict = fddict_cache.get(fdIndex)
        if not fddict:
            fddict = font.getFontInfo(options.allow_no_blues,
                                      options.noFlex,
                                      options.vCounterGlyphs,
                                      options.hCounterGlyphs,
                                      fdIndex)
            fddict_cache[fdIndex] = fddict
        fontinfo_list[name] = fddict

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
        fdIndex = 0
        fddict = fontDictList[0]
    else:
        log.info("Using alternate FDDict global values from fontinfo "
                 "file for some glyphs.")

    fontinfo_list = {}
    for name in glyph_list:
        if fdglyphdict is not None:
            fdIndex = fdglyphdict[name][0]
            fddict = fontDictList[fdIndex]
        fontinfo_list[name] = fddict

    return fontinfo_list


class hintAdapter:
    """
    Adapter between high-level autohint.py code and the 1D hinter.
    Also contains code that uses hints from both dimensions, primarily
    for hintmask distribution
    """
    def __init__(self, options, fontinfo_list, report=None):
        self.options = options
        self.fontinfo_list = fontinfo_list
        if report:
            self.report = report
        else:
            self.report = Report()
        self.hHinter = hhinter(options)
        self.vHinter = vhinter(options)
        self.name = ""

        self.FlareValueLimit = 1000
        self.MaxHalfMargin = 20  # XXX 10 might better match original C code
        self.PromotionDistance = 50

    def getSegments(self, glyph, pe, oppo=False):
        """Returns the list of segments for pe in the requested dimension"""
        gstate = glyph.vhs if (self.doV == (not oppo)) else glyph.hhs
        pestate = gstate.getPEState(pe)
        return pestate.segments if pestate else []

    def getMasks(self, glyph, pe):
        """
        Returns the masks of hints needed by/desired for pe in each dimension
        """
        masks = []
        for i, hs in enumerate((glyph.hhs, glyph.vhs)):
            mask = None
            if hs.keepHints:
                if pe.mask:
                    mask = copy(pe.mask[i])
            else:
                pes = hs.getPEState(pe)
                if pes and pes.mask:
                    mask = copy(pes.mask)
            if mask is None:
                mask = [False] * len(hs.stems)
            masks.append(mask)
        return masks

    def hint(self, name, glyph):
        """Top-level flex and stem hinting method for a glyph"""
        self.doV = False
        gr = self.report.glyphs.get(name, None)
        if gr is None:
            gr = Report.Glyph(name, self.options.report_all_stems)
            self.report.glyphs[name] = gr
        self.name = name
        self.hHinter.setGlyph(self.fontinfo_list[name], gr, glyph, name)
        self.vHinter.setGlyph(self.fontinfo_list[name], gr, glyph, name)

        glyph.changed = False

        if not self.options.noFlex:
            self.hHinter.addFlex()
            self.vHinter.addFlex(inited=True)

        lnks = links(glyph)

        self.hHinter.calcHintValues(lnks)
        self.vHinter.calcHintValues(lnks)

        if self.hHinter.keepHints and self.vHinter.keepHints:
            return False

        if self.options.allowChanges:
            neworder = lnks.shuffle(self.hHinter)  # hHinter serves as log
            if neworder:
                glyph.reorder(neworder, self.hHinter)  # hHinter serves as log

        self.listHintInfo(glyph)

        if not self.hHinter.keepHints:
            self.remFlares(glyph)
        self.doV = True
        if not self.vHinter.keepHints:
            self.remFlares(glyph)

        self.hHinter.convertToMasks()
        self.vHinter.convertToMasks()

        self.distributeMasks(glyph)

        return True

    def distributeMasks(self, glyph):
        """
        When necessary, chose the locations and contents of hintmasks for
        the glyph
        """
        log = self.hHinter
        stems = [None, None]
        masks = [None, None]
        lnstm = [0, 0]
        # Initial horizontal data
        # If keepHints was true hhs.stems was already set to glyph.hstems in
        # converttoMasks()
        stems[0] = glyph.hstems = glyph.hhs.stems
        lnstm[0] = len(stems[0])
        if self.hHinter.keepHints:
            if glyph.startmasks and glyph.startmasks[0]:
                masks[0] = glyph.startmasks[0]
            elif not glyph.hhs.hasConflicts:
                masks[0] = [True] * lnstm[0]
            else:
                pass  # XXX error existing hints have conflicts but no start mask
        else:
            masks[0] = [False] * lnstm[0]

        # Initial vertical data
        stems[1] = glyph.vstems = glyph.vhs.stems
        lnstm[1] = len(stems[1])
        if self.vHinter.keepHints:
            if glyph.startmasks and glyph.startmasks[1]:
                masks[1] = glyph.startmasks[1]
            elif not glyph.hhs.hasConflicts:
                masks[1] = [True] * lnstm[1]
            else:
                pass  # XXX error existing hints have conflicts but no start mask
        else:
            masks[1] = [False] * lnstm[1]

        self.buildCounterMasks(glyph)

        if not glyph.hhs.hasConflicts and not glyph.vhs.hasConflicts:
            glyph.startmasks = None
            glyph.is_hm = False
            return

        usedmasks = deepcopy(masks)

        glyph.is_hm = True
        glyph.startmasks = masks
        NOTSHORT, SHORT, CONFLICT = 0, 1, 2
        mode = NOTSHORT
        ns = None
        c = glyph.nextForHints(glyph)
        while c:
            if c.isShort() or c.flex == 2:
                if mode == NOTSHORT:
                    if ns:
                        mode = SHORT
                        oldmasks = masks
                        masks = deepcopy(masks)
                        incompatmasks = self.getMasks(glyph, ns)
                    else:
                        mode = CONFLICT
            else:
                ns = c
                if mode == SHORT:
                    oldmasks[:] = masks
                    masks = oldmasks
                    incompatmasks = None
                mode = NOTSHORT
            cmasks = self.getMasks(glyph, c)
            candmasks, conflict = self.joinMasks(masks, cmasks,
                                                 mode == CONFLICT)
            maskstr = ''.join(('1' if i else '0'
                               for i in (candmasks[0] + candmasks[1])))
            log.info("mask %s at %g %g, mode %d, conflict: %r" %
                     (maskstr, c.e.x, c.e.y, mode, conflict))
            if conflict:
                if mode == NOTSHORT:
                    self.bridgeMasks(glyph, masks, cmasks, usedmasks, c)
                    masks = c.masks = cmasks
                elif mode == SHORT:
                    assert ns
                    newinc, _ = self.joinMasks(incompatmasks, cmasks, True)
                    self.bridgeMasks(glyph, oldmasks, newinc, usedmasks, ns)
                    masks = ns.masks = newinc
                    mode = CONFLICT
                else:
                    assert mode == CONFLICT
                    masks[:] = candmasks
            else:
                masks[:] = candmasks
                if mode == SHORT:
                    incompatmasks, _ = self.joinMasks(incompatmasks, cmasks,
                                                      False)
            c = glyph.nextForHints(c)
        if mode == SHORT:
            oldmasks[:] = masks
            masks = oldmasks
        self.bridgeMasks(glyph, masks, None, usedmasks, glyph.last())
        if False in usedmasks[0] or False in usedmasks[1]:
            self.delUnused(stems, usedmasks)
            self.delUnused(glyph.startmasks, usedmasks)
            foundPEMask = False
            for c in glyph:
                if c.masks:
                    foundPEMask = True
                    self.delUnused(c.masks, usedmasks)
            if not foundPEMask:
                glyph.startmasks = None
                glyph.is_hm = False

    def buildCounterMasks(self, glyph):
        """
        For glyph dimensions that are counter-hinted, make a cntrmask
        with all Trues in that dimension (because only h/vstem3 style counter
        hints are supported)
        """
        assert not glyph.hhs.keepHints or not glyph.vhs.keepHints
        if not glyph.hhs.keepHints:
            hcmsk = [glyph.hhs.counterHinted] * len(glyph.hhs.stems)
        if not glyph.vhs.keepHints:
            vcmsk = [glyph.vhs.counterHinted] * len(glyph.vhs.stems)
        if glyph.hhs.keepHints or glyph.vhs.keepHints and glyph.cntr:
            cntr = []
            for cm in glyph.cntr:
                hm = cm[0] if glyph.hhs.keepHints else hcmsk
                vm = cm[1] if glyph.vhs.keepHints else vcmsk
                cntr.append([hm, vm])
        elif glyph.hhs.counterHinted or glyph.vhs.counterHinted:
            cntr = [[hcmsk, vcmsk]]
        else:
            cntr = []
        glyph.cntr = cntr

    def joinMasks(self, m, cm, log):
        """
        Try to add the stems in cm to m, or start a new mask if there are
        conflicts.
        """
        conflict = False
        nm = [None, None]
        for hv in range(2):
            hs = self.vHinter.hs if hv == 1 else self.hHinter.hs
            l = len(m[hv])
            if hs.counterHinted:
                nm[hv] = [True] * l
                continue
            c = cm[hv]
            n = nm[hv] = copy(m[hv])
            if hs.keepHints:
                conflict = True in c
                continue
            assert len(c) == l
            for i in range(l):
                iconflict = ireplaced = False
                if not c[i] or n[i]:
                    continue
                # look for conflicts
                for j in range(l):
                    if not hs.hasConflicts:
                        break
                    if j == i:
                        continue
                    if n[j] and hs.stemConflicts[i][j]:
                        # See if we can do a ghost stem swap
                        if hs.ghostCompat[i]:
                            for k in range(l):
                                if not n[k] or not hs.ghostCompat[i][k]:
                                    continue
                                else:
                                    ireplaced = True
                                    break
                        if not ireplaced:
                            iconflict = True
                    if ireplaced:
                        break
                if not iconflict and not ireplaced:
                    n[i] = True
                elif iconflict:
                    conflict = True
                    # XXX log conflict here if log is true
        return nm, conflict

    def bridgeMasks(self, glyph, o, n, used, pe):
        """
        For switching hintmasks: Clean up o by adding compatible stems from
        mainMask and add stems from o to n when they are close to pe

        used contains a running map of which stems have ever been included
        in a hintmask
        """
        stems = [glyph.hstems, glyph.vstems]
        po = pe.e if pe.isLine() else pe.cs
        carryMask = [[False] * len(o[0]), [False] * len(o[1])]
        for hv in range(2):
            # Carry a previous hint forward if it is compatible and close
            # to the current pathElement
            nloc = pe.e.x if hv == 1 else pe.e.y
            for i in range(len(o[hv])):
                if not o[hv][i]:
                    continue
                dlimit = max(self.hHinter.BandMargin/2, self.MaxHalfMargin)
                if stems[hv][i].distance(nloc) < dlimit:
                    carryMask[hv][i] = True
            # If there are no hints in o in this dimension add the closest to
            # the current path element
            if True not in o[hv]:
                oloc = po.x if hv == 1 else po.y
                try:
                    _, ms = min(((stems[hv][i].distance(oloc), i)
                                 for i in range(len(o[hv]))))
                    o[hv][ms] = True
                except ValueError:
                    pass
        if self.mergeMain(glyph):
            no, _ = self.joinMasks(o, [glyph.hhs.mainMask, glyph.vhs.mainMask],
                                   False)
            o[:] = no
        for hv in range(2):
            used[hv] = [ov or uv for ov, uv in zip(o[hv], used[hv])]
        if n is not None:
            nm, _ = self.joinMasks(n, carryMask, False)
            n[:] = nm

    def mergeMain(self, glyph):
        return len(glyph.subpaths) <= 5

    def delUnused(self, l, ml):
        """If ml[d][i] is False delete that entry from ml[d]"""
        for hv in range(2):
            l[hv][:] = [l[hv][i] for i in range(len(l[hv])) if ml[hv][i]]

    def listHintInfo(self, glyph):
        """
        Output debug messages about what stems are associated with what segments
        """
        for pe in glyph:
            hList = self.getSegments(glyph, pe, False)
            vList = self.getSegments(glyph, pe, True)
            if hList or vList:
                self.hHinter.debug("hintlist x %g y %g" % (pe.e.x, pe.e.y))
                for seg in hList:
                    seg.hintval.show(False, "listhint", self.hHinter)
                for seg in vList:
                    seg.hintval.show(True, "listhint", self.vHinter)

    def remFlares(self, glyph):
        """
        When two paths are witin MaxFlare and connected by a path that
        also stays within MaxFlare, and both desire different stems,
        (sometimes) remove the lower-valued stem of the pair
        """
        for c in glyph:
            csl = self.getSegments(glyph, c)
            if not csl:
                continue
            n = glyph.nextInSubpath(c)
            cDone = False
            while c != n and not cDone:
                nsl = self.getSegments(glyph, n)
                if not nsl:
                    if not self.getSegments(glyph, n, True):
                        break
                    else:
                        n = glyph.nextInSubpath(n)
                        continue
                csi = 0
                while csi < len(csl):
                    cseg = csl[csi]
                    nsi = 0
                    while nsi < len(nsl):
                        nseg = nsl[nsi]
                        if cseg is not None and nseg is not None:
                            diff = abs(cseg.loc - nseg.loc)
                            if diff > self.hHinter.MaxFlare:
                                cDone = True
                                nsi += 1
                                continue
                            if not self.isFlare(cseg.loc, glyph, c, n):
                                cDone = True
                                nsi += 1
                                continue
                            chv, nhv = cseg.hintval, nseg.hintval
                            if (diff != 0 and
                                self.isUSeg(cseg.loc, chv.uloc, chv.lloc) ==
                                    self.isUSeg(nseg.loc, nhv.uloc, nhv.lloc)):
                                if (chv.compVal(self.hHinter.SpcBonus) >
                                        nhv.compVal(self.hHinter.SpcBonus)):
                                    if (nhv.spc == 0 and
                                            nhv.val < self.FlareValueLimit):
                                        self.reportRemFlare(n, c, "n")
                                        del nsl[nsi]
                                        nsi -= 1
                                else:
                                    if (chv.spc == 0 and
                                            chv.val < self.FlareValueLimit):
                                        self.reportRemFlare(c, n, "c")
                                        del csl[csi]
                                        csi -= 1
                                        break
                        nsi += 1
                    csi += 1
                n = glyph.nextInSubpath(n)

    def isFlare(self, loc, glyph, c, n):
        """Utility function for remFlares"""
        while c is not n:
            v = c.e.x if self.doV else c.e.y
            if abs(v - loc) > self.hHinter.MaxFlare:
                return False
            c = glyph.nextInSubpath(c)
        return True

    def isUSeg(self, loc, uloc, lloc):
        return abs(uloc - loc) <= abs(lloc - loc)

    def reportRemFlare(self, pe, pe2, desc):
        self.hHinter.info("Removed %s flare at %g %g by %g %g : %s" %
                          ("vertical" if self.doV else "horizontal",
                           pe.e.x, pe.e.y, pe2.e.x, pe2.e.y, desc))


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


def log_dict(fddict, name, task="hinting"):
    if getattr(fddict, 'DictName', None):
        log.info("%s: Begin %s (using fdDict %s).",
                 name, task, fddict.DictName)
    else:
        log.info("%s: Begin %s.", name, task)


def hint_font(hintadapt, font, glyph_list):
    aliases = hintadapt.options.nameAliases

    hinted = {}
    glyphs = get_glyphs(hintadapt.options, font, glyph_list)
    for name in glyphs:
        if hintadapt.options.justReporting():
            if name == '.notdef':
                continue
            task = "analysis"
        else:
            task = "hinting"

        g = glyphs[name]

        an = aliases.get(name, name)
        log_dict(hintadapt.fontinfo_list[name], name, task)

        # Call auto-hint library on glyph
        changed = hintadapt.hint(name, g.glyph)

        if not g.glyph.hasHints(both=True):
            log.info("%s: No hints added!", an)
            if not changed:
                continue

        if hintadapt.options.logOnly or hintadapt.options.justReporting():
            continue

        if changed:
            hinted[name] = g

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

        glyph_names = get_glyph_list(options, font, path)
        fontinfo_list = get_fontinfo_list(options, font, glyph_names)

        hintadapt = hintAdapter(options, fontinfo_list)

        log.info("Hinting font %s. Start time: %s.", path, time.asctime())

        hinted = hint_font(hintadapt, font, glyph_names)

        if options.report_zones or options.report_stems:
            hintadapt.report.save(outpath)
        else:
            if hinted:
                log.info("Saving font file with new hints...")
                for name, ge in hinted.items():
                    font.updateFromGlyph(ge.glyph, name)
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
