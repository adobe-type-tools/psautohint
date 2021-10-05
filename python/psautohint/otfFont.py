# Copyright 2014 Adobe. All rights reserved.

"""
Utilities for converting between T2 charstrings and glyphData objects.
"""

import copy
import logging
import os
import re
import subprocess
import tempfile
from fontTools.misc.psCharStrings import T2OutlineExtractor
from fontTools.ttLib import TTFont, newTable
from fontTools.misc.roundTools import noRound, otRound
from fontTools.varLib.varStore import VarStoreInstancer
from fontTools.varLib.cff import CFF2CharStringMergePen, MergeOutlineExtractor
# import subset.cff is needed to load the implementation for
# CFF.desubroutinize: the module adds this class method to the CFF and CFF2
# classes.
import fontTools.subset.cff

from . import fdTools, FontParseError
from .glyphData import glyphData

# keep linting tools quiet about unused import
assert fontTools.subset.cff is not None

log = logging.getLogger(__name__)

kStackLimit = 46
kStemLimit = 96


class SEACError(Exception):
    pass


def _add_method(*clazzes):
    """Returns a decorator function that adds a new method to one or
    more classes."""
    def wrapper(method):
        done = []
        for clazz in clazzes:
            if clazz in done:
                continue  # Support multiple names of a clazz
            done.append(clazz)
            assert clazz.__name__ != 'DefaultTable', \
                'Oops, table class not found.'
            assert not hasattr(clazz, method.__name__), \
                "Oops, class '%s' has method '%s'." % (clazz.__name__,
                                                       method.__name__)
            setattr(clazz, method.__name__, method)
        return None
    return wrapper


def hintOn(i, hintMaskBytes):
    """Used to help convert T2 hintmask bytes to a boolean array"""
    byteIndex = int(i / 8)
    byteValue = hintMaskBytes[byteIndex]
    offset = 7 - (i % 8)
    return ((2**offset) & byteValue) > 0


class T2ToGlyphDataExtractor(T2OutlineExtractor):
    """
    Inherits from the fontTools Outline Extractor and adapts some of the
    operator methods to match the "hint pen" interface of the glyphData class
    """
    def __init__(self, gd, localSubrs, globalSubrs, nominalWidthX,
                 defaultWidthX, readStems=True, readFlex=True):
        T2OutlineExtractor.__init__(self, gd, localSubrs, globalSubrs,
                                    nominalWidthX, defaultWidthX)
        self.glyph = gd
        self.readStems = readStems
        self.readFlex = readFlex
        self.hintMaskBytes = None
        self.subrLevel = 0
        self.vhintCount = 0
        self.hhintCount = 0

    def execute(self, charString):
        self.subrLevel += 1
        super().execute(charString)
        self.subrLevel -= 1
        if self.subrLevel == 0:
            self.glyph.finish()

    def op_endchar(self, index):
        self.endPath()
        args = self.popallWidth()
        if args:  # It is a 'seac' composite character. Don't process
            raise SEACError

    def op_hflex(self, index):
        if self.readFlex:
            self.glyph.nextIsFlex()
        super().op_hflex(index)

    def op_flex(self, index):
        if self.readFlex:
            self.glyph.nextIsFlex()
        super().op_flex(index)

    def op_hflex1(self, index):
        if self.readFlex:
            self.glyph.nextIsFlex()
        super().op_hflex1(index)

    def op_flex1(self, index):
        if self.readFlex:
            self.glyph.nextIsFlex()
        super().op_flex1(index)

    def op_hstem(self, index):
        args = self.popallWidth()
        self.countHints(args, True)
        if not self.readStems:
            return
        self.glyph.hStem(args, False)

    def op_vstem(self, index):
        args = self.popallWidth()
        self.countHints(args, False)
        if not self.readStems:
            return
        self.glyph.vStem(args, False)

    def op_hstemhm(self, index):
        args = self.popallWidth()
        self.countHints(args, True)
        if not self.readStems:
            return
        self.glyph.hStem(args, True)

    def op_vstemhm(self, index):
        args = self.popallWidth()
        self.countHints(args, False)
        if not self.readStems:
            return
        self.glyph.vStem(args, True)

    def countHints(self, args, horiz):
        if horiz:
            self.hhintCount = len(args) // 2
        else:
            self.vhintCount = len(args) // 2

    def doMask(self, index):
        args = []
        if not self.hintMaskBytes:
            args = self.popallWidth()
            if args:
                # hstem(hm) followed by values followed by a hint mask is
                # an implicit vstem(hm)
                self.countHints(args, False)
                self.glyph.vStem(args, None)
            self.hintMaskBytes = (self.vhintCount + self.hhintCount + 7) // 8

        hintMaskString, index = self.callingStack[-1].getBytes(
            index, self.hintMaskBytes)

        tc = self.hhintCount + self.vhintCount
        hhints = [hintOn(i, hintMaskString) for i in range(self.hhintCount)]
        vhints = [hintOn(i, hintMaskString)
                  for i in range(self.hhintCount, tc)]
        return hintMaskString, hhints, vhints, index

    def op_hintmask(self, index):
        hintString, hhints, vhints, index = self.doMask(index)
        if self.readStems:
            self.glyph.hintmask(hhints, vhints)
        return hintString, index

    def op_cntrmask(self, index):
        hintString, hhints, vhints, index = self.doMask(index)
        if self.readStems:
            self.glyph.cntrmask(hhints, vhints)
        return hintString, index


def convertT2ToGlyphData(t2CharString, readStems=True, readFlex=True,
                         roundCoords=True, name=None):
    """Wrapper method for T2ToGlyphDataExtractor execution"""
    gl = glyphData(roundCoords=roundCoords, name=name)
    subrs = getattr(t2CharString.private, "Subrs", [])
    extractor = T2ToGlyphDataExtractor(gl, subrs,
                                       t2CharString.globalSubrs,
                                       t2CharString.private.nominalWidthX,
                                       t2CharString.private.defaultWidthX,
                                       readStems, readFlex)
    extractor.execute(t2CharString)
    t2_width_arg = None
    if extractor.gotWidth and (extractor.width is not None):
        t2_width_arg = extractor.width - t2CharString.private.nominalWidthX
    gl.setWidth(t2_width_arg)
    return gl


def _run_tx(args):
    try:
        subprocess.check_call(["tx"] + args, stderr=subprocess.DEVNULL)
    except (subprocess.CalledProcessError, OSError) as e:
        raise FontParseError(e)


class CFFFontData:
    def __init__(self, path, font_format):
        self.inputPath = path
        self.font_format = font_format
        self.is_cff2 = False
        self.is_vf = False
        self.vs_data_models = None
        if font_format == "OTF":
            # It is an OTF font, we can process it directly.
            font = TTFont(path)
            if "CFF " in font:
                cff_format = "CFF "
            elif "CFF2" in font:
                cff_format = "CFF2"
                self.is_cff2 = True
            else:
                raise FontParseError("OTF font has no CFF table <%s>." % path)
        else:
            # Else, package it in an OTF font.
            cff_format = "CFF "
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
        self.cffTable = font[cff_format]

        # for identifier in glyph-list:
        # Get charstring.
        self.topDict = self.cffTable.cff.topDictIndex[0]
        self.charStrings = self.topDict.CharStrings
        if 'fvar' in self.ttFont:
            # have not yet collected VF global data.
            self.is_vf = True
            fvar = self.ttFont['fvar']
            CFF2 = self.cffTable
            CFF2.desubroutinize()
            topDict = CFF2.cff.topDictIndex[0]
            # We need a new charstring object into which we can save the
            # hinted CFF2 program data. Copying an existing charstring is a
            # little easier than creating a new one and making sure that all
            # properties are set correctly.
            self.temp_cs = copy.deepcopy(self.charStrings['.notdef'])
            self.vs_data_models = self.get_vs_data_models(topDict,
                                                          fvar)

    def getGlyphList(self):
        return self.ttFont.getGlyphOrder()

    def getPSName(self):
        if self.is_cff2 and 'name' in self.ttFont:
            psName = next((name_rec.string for name_rec in self.ttFont[
                'name'].names if (name_rec.nameID == 6) and (
                    name_rec.platformID == 3)))
            psName = psName.decode('utf-16be')
        else:
            psName = self.cffTable.cff.fontNames[0]
        return psName

    def get_min_max(self, pTopDict, upm):
        if self.is_cff2 and 'hhea' in self.ttFont:
            font_max = self.ttFont['hhea'].ascent
            font_min = self.ttFont['hhea'].descent
        elif hasattr(pTopDict, 'FontBBox'):
            font_max = pTopDict.FontBBox[3]
            font_min = pTopDict.FontBBox[1]
        else:
            font_max = upm * 1.25
            font_min = -upm * 0.25
        alignment_min = min(-upm * 0.25, font_min)
        alignment_max = max(upm * 1.25, font_max)
        return alignment_min, alignment_max

    def convertToGlyphData(self, glyphName, readStems, readFlex,
                           roundCoords, doAll=False):
        t2CharString = self.charStrings[glyphName]
        try:
            gl = convertT2ToGlyphData(t2CharString, readStems, readFlex,
                                      roundCoords, name=glyphName)
        except SEACError:
            log.warning("Skipping %s: can't process SEAC composite glyphs.",
                        glyphName)
            gl = None
        return gl

    def updateFromGlyph(self, gl, glyphName):
        t2Program = gl.T2()

        if not self.is_cff2:
            t2_width_arg = gl.getWidth()
            if t2_width_arg is not None:
                t2Program = [t2_width_arg] + t2Program
        if self.vs_data_models is not None:
            # It is a variable font. Accumulate the charstrings.
            self.glyph_programs.append(t2Program)
        else:
            # This is an MM source font. Update the font's charstring directly.
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

    def hasFDArray(self):
        return self.is_cff2 or hasattr(self.topDict, "FDSelect")

    def flattenBlends(self, blendList):
        if type(blendList[0]) is list:
            flatList = [blendList[i][0] for i in range(len(blendList))]
        else:
            flatList = blendList
        return flatList

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
        if fdIndex > 0:
            fdDict.setInfo('DictName', "OTF FDArray index %s" % fdIndex)
        fdDict.setInfo('LanguageGroup',
                       getattr(privateDict, "LanguageGroup", 0))

        if hasattr(pDict, "FontMatrix"):
            fontMatrix = pDict.FontMatrix
        else:
            fontMatrix = pTopDict.FontMatrix
        upm = int(1 / fontMatrix[0])
        fdDict.setInfo('OrigEmSqUnits', upm)

        fdDict.setInfo('FontName',
                       getattr(pTopDict, "FontName", self.getPSName()))

        blueValues = getattr(privateDict, "BlueValues", [])[:]
        numBlueValues = len(blueValues)
        if numBlueValues < 4:
            low, high = self.get_min_max(pTopDict, upm)
            # Make a set of inactive alignment zones: zones outside of the
            # font BBox so as not to affect hinting. Used when source font has
            # no BlueValues or has invalid BlueValues. Some fonts have bad BBox
            # values, so I don't let this be smaller than -upm*0.25, upm*1.25.
            inactiveAlignmentValues = [low, low, high, high]
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
        blueValues = self.flattenBlends(blueValues)
        blueValues[0] = blueValues[0] - blueValues[1]
        for i in range(3, numBlueValues, 2):
            blueValues[i] = blueValues[i] - blueValues[i - 1]

        numBlueValues = min(numBlueValues, len(fdTools.kBlueValueKeys))
        for i in range(numBlueValues):
            key = fdTools.kBlueValueKeys[i]
            value = blueValues[i]
            fdDict.setInfo(key, value)

        if hasattr(privateDict, "OtherBlues"):
            # For all OtherBlues, the pairs are bottom zones, and
            # the first value of each pair is the overshoot position.
            i = 0
            numBlueValues = len(privateDict.OtherBlues)
            blueValues = privateDict.OtherBlues[:]
            blueValues.sort()
            blueValues = self.flattenBlends(blueValues)
            for i in range(0, numBlueValues, 2):
                blueValues[i] = blueValues[i] - blueValues[i + 1]
            numBlueValues = min(numBlueValues,
                                len(fdTools.kOtherBlueValueKeys))
            for i in range(numBlueValues):
                key = fdTools.kOtherBlueValueKeys[i]
                value = blueValues[i]
                fdDict.setInfo(key, value)

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
        vstems = self.flattenBlends(vstems)
        if (len(vstems) == 0) or ((len(vstems) == 1) and (vstems[0] < 1)):
            vstems = [upm]  # dummy value that will allow PyAC to run
            log.warning("There is no value or 0 value for DominantV.")
        fdDict.setInfo('DominantV', vstems)

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
        hstems = self.flattenBlends(hstems)
        if (len(hstems) == 0) or ((len(hstems) == 1) and (hstems[0] < 1)):
            hstems = [upm]  # dummy value that will allow PyAC to run
            log.warning("There is no value or 0 value for DominantH.")
        fdDict.setInfo('DominantH', hstems)

        if noFlex:
            fdDict.setInfo('FlexOK', False)
        else:
            fdDict.setInfo('FlexOK', True)

        # Add candidate lists for counter hints, if any.
        if vCounterGlyphs:
            fdDict.setInfo('VCounterChars', vCounterGlyphs)
        if hCounterGlyphs:
            fdDict.setInfo('HCounterChars', hCounterGlyphs)

        fdDict.setInfo('BlueFuzz', getattr(privateDict, "BlueFuzz", 1))

        fdDict.buildBlueLists()

        return fdDict

    def getfdIndex(self, name):
        gid = self.ttFont.getGlyphID(name)
        if hasattr(self.topDict, "FDSelect"):
            fdIndex = self.topDict.FDSelect[gid]
        else:
            fdIndex = 0
        return fdIndex

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

    def get_vf_glyphs(self, glyph_name):
        charstring = self.charStrings[glyph_name]

        if 'vsindex' in charstring.program:
            op_index = charstring.program.index('vsindex')
            vsindex = charstring.program[op_index - 1]
        else:
            vsindex = 0
        self.vsindex = vsindex
        self.glyph_programs = []
        vs_data_model = self.vs_data_model = self.vs_data_models[vsindex]

        glyph_list = []
        for vsi in vs_data_model.master_vsi_list:
            t2_program = interpolate_cff2_charstring(charstring, glyph_name,
                                                     vsi.interpolateFromDeltas,
                                                     vsindex)
            self.temp_cs.program = t2_program
            glyph = convertT2ToGlyphData(self.temp_cs, True, False, True,
                                         name=glyph_name)
            glyph_list.append(glyph)

        return glyph_list

    @staticmethod
    def get_vs_data_models(topDict, fvar):
        otvs = topDict.VarStore.otVarStore
        region_list = otvs.VarRegionList.Region
        axis_tags = [axis_entry.axisTag for axis_entry in fvar.axes]
        vs_data_models = []
        for vsindex, var_data in enumerate(otvs.VarData):
            vsi = VarStoreInstancer(topDict.VarStore.otVarStore, fvar.axes, {})
            master_vsi_list = [vsi]
            for region_idx in var_data.VarRegionIndex:
                region = region_list[region_idx]
                loc = {}
                for i, axis in enumerate(region.VarRegionAxis):
                    loc[axis_tags[i]] = axis.PeakCoord
                vsi = VarStoreInstancer(topDict.VarStore.otVarStore, fvar.axes,
                                        loc)
                master_vsi_list.append(vsi)
            vdm = VarDataModel(var_data, vsindex, master_vsi_list)
            vs_data_models.append(vdm)
        return vs_data_models

    def merge_hinted_glyphs(self, name):
        new_t2cs = merge_hinted_programs(self.temp_cs, self.glyph_programs,
                                         name, self.vs_data_model)
        if self.vsindex:
            new_t2cs.program = [self.vsindex, 'vsindex'] + new_t2cs.program
        self.charStrings[name] = new_t2cs


def interpolate_cff2_charstring(charstring, gname, interpolateFromDeltas,
                                vsindex):
    # Interpolate charstring
    # e.g replace blend op args with regular args,
    # and discard vsindex op.
    new_program = []
    last_i = 0
    program = charstring.program
    for i, token in enumerate(program):
        if token == 'vsindex':
            if last_i != 0:
                new_program.extend(program[last_i:i - 1])
            last_i = i + 1
        elif token == 'blend':
            num_regions = charstring.getNumRegions(vsindex)
            numMasters = 1 + num_regions
            num_args = program[i - 1]
            # The program list starting at program[i] is now:
            # ..args for following operations
            # num_args values  from the default font
            # num_args tuples, each with numMasters-1 delta values
            # num_blend_args
            # 'blend'
            argi = i - (num_args * numMasters + 1)
            if last_i != argi:
                new_program.extend(program[last_i:argi])
            end_args = tuplei = argi + num_args
            master_args = []
            while argi < end_args:
                next_ti = tuplei + num_regions
                deltas = program[tuplei:next_ti]
                val = interpolateFromDeltas(vsindex, deltas)
                master_val = program[argi]
                master_val += otRound(val)
                master_args.append(master_val)
                tuplei = next_ti
                argi += 1
            new_program.extend(master_args)
            last_i = i + 1
    if last_i != 0:
        new_program.extend(program[last_i:])
    return new_program


def merge_hinted_programs(charstring, t2_programs, gname, vs_data_model):
    num_masters = vs_data_model.num_masters
    var_pen = CFF2CharStringMergePen([], gname, num_masters, 0)
    charstring.outlineExtractor = MergeOutlineExtractor

    for i, t2_program in enumerate(t2_programs):
        var_pen.restart(i)
        charstring.program = t2_program
        charstring.draw(var_pen)

    new_charstring = var_pen.getCharString(
        private=charstring.private,
        globalSubrs=charstring.globalSubrs,
        var_model=vs_data_model, optimize=True)
    return new_charstring


@_add_method(VarStoreInstancer)
def get_scalars(self, vsindex, region_idx):
    varData = self._varData
    # The index key needs to be the master value index, which includes
    # the default font value. VarRegionIndex provides the region indices.
    scalars = {0: 1.0}  # The default font always has a weight of 1.0
    region_index = varData[vsindex].VarRegionIndex
    for idx in range(region_idx):  # omit the scalar for the region.
        scalar = self._getScalar(region_index[idx])
        if scalar:
            scalars[idx + 1] = scalar
    return scalars


class VarDataModel(object):

    def __init__(self, var_data, vsindex, master_vsi_list):
        self.master_vsi_list = master_vsi_list
        self.var_data = var_data
        self._num_masters = len(master_vsi_list)
        self.delta_weights = [{}]  # for default font value
        for region_idx, vsi in enumerate(master_vsi_list[1:]):
            scalars = vsi.get_scalars(vsindex, region_idx)
            self.delta_weights.append(scalars)

    @property
    def num_masters(self):
        return self._num_masters

    def getDeltas(self, master_values, *, round=noRound):
        assert len(master_values) == len(self.delta_weights)
        out = []
        for i, scalars in enumerate(self.delta_weights):
            delta = master_values[i]
            for j, scalar in scalars.items():
                if scalar:
                    delta -= out[j] * scalar
            out.append(round(delta))
        return out
