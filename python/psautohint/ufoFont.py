# Copyright 2014 Adobe. All rights reserved.

"""
This module supports using the Adobe FDK tools which operate on 'bez'
files with UFO fonts. It provides low level utilities to manipulate UFO
data without fully parsing and instantiating UFO objects, and without
requiring that the AFDKO contain the robofab libraries.

Modified in Nov 2014, when AFDKO added the robofab libraries. It can now
be used with UFO fonts only to support the hash mechanism.

Developed in order to support checkOutlines and autohint, the code
supports two main functions:
- convert between UFO GLIF and bez formats
- keep a history of processing in a hash map, so that the (lengthy)
processing by autohint and checkOutlines can be avoided if the glyph has
already been processed, and the source data has not changed.

The basic model is:
 - read GLIF file
 - transform GLIF XML element to bez file
 - call FDK tool on bez file
 - transform new bez file to GLIF XML element with new data, and save in list

After all glyphs are done save all the new GLIF XML elements to GLIF
files, and update the hash map.

A complication in the Adobe UFO workflow comes from the fact we want to
make sure that checkOutlines and autohint have been run on each glyph in
a UFO font, when building an OTF font from the UFO font. We need to run
checkOutlines, because we no longer remove overlaps from source UFO font
data, because this can make revising a glyph much easier. We need to run
autohint, because the glyphs must be hinted after checkOutlines is run,
and in any case we want all glyphs to have been autohinted. The problem
with this is that it can take a minute or two to run autohint or
checkOutlines on a 2K-glyph font. The way we avoid this is to make and
keep a hash of the source glyph drawing operators for each tool. When
the tool is run, it calculates a hash of the source glyph, and compares
it to the saved hash. If these are the same, the tool can skip the
glyph. This saves a lot of time: if checkOutlines and autohint are run
on all glyphs in a font, then a second pass is under 2 seconds.

Another issue is that since we no longer remove overlaps from the source
glyph files, checkOutlines must write any edited glyph data to a
different layer in order to not destroy the source data. The ufoFont
defines an Adobe-specific glyph layer for processed glyphs, named
"glyphs.com.adobe.type.processedGlyphs".
checkOutlines writes new glyph files to the processed glyphs layer only
when it makes a change to the glyph data.

When the autohint program is run, the ufoFont must be able to tell
whether checkOutlines has been run and has altered a glyph: if so, the
input file needs to be from the processed glyph layer, else it needs to
be from the default glyph layer.

The way the hashmap works is that we keep an entry for every glyph that
has been processed, identified by a hash of its marking path data. Each
entry contains:
- a hash of the glyph point coordinates, from the default layer.
This is set after a program has been run.
- a history list: a list of the names of each program that has been run,
  in order.
- an editStatus flag.
Altered GLIF data is always written to the Adobe processed glyph layer. The
program may or may not have altered the outline data. For example, autohint
adds private hint data, and adds names to points, but does not change any
paths.

If the stored hash for the glyph does not exist, the ufoFont lib will save the
new hash in the hash map entry and will set the history list to contain just
the current program. The program will read the glyph from the default layer.

If the stored hash matches the hash for the current glyph file in the default
layer, and the current program name is in the history list,then ufoFont
will return "skip=1", and the calling program may skip the glyph.

If the stored hash matches the hash for the current glyph file in the default
layer, and the current program name is not in the history list, then the
ufoFont will return "skip=0". If the font object field 'usedProcessedLayer' is
set True, the program will read the glyph from the from the Adobe processed
layer, if it exists, else it will always read from the default layer.

If the hash differs between the hash map entry and the current glyph in the
default layer, and usedProcessedLayer is False, then ufoFont will return
"skip=0". If usedProcessedLayer is True, then the program will consult the
list of required programs. If any of these are in the history list, then the
program will report an error and return skip =1, else it will return skip=1.
The program will then save the new hash in the hash map entry and reset the
history list to contain just the current program. If the old and new hash
match, but the program name is not in the history list, then the ufoFont will
not skip the glyph, and will add the program name to the history list.


The only tools using this are, at the moment, checkOutlines, checkOutlinesUFO
and autohint. checkOutlines and checkOutlinesUFO use the hash map to skip
processing only when being used to edit glyphs, not when reporting them.
checkOutlines necessarily flattens any components in the source glyph file to
actual outlines. autohint adds point names, and saves the hint data as a
private data in the new GLIF file.

autohint saves the hint data in the GLIF private data area, /lib/dict,
as a key and data pair. See below for the format.

autohint started with _hintFormat1_, a reasonably compact XML representation of
the data. In Sep 2105, autohhint switched to _hintFormat2_ in order to be plist
compatible. This was necessary in order to be compatible with the UFO spec, by
was driven more immediately by the fact the the UFO font file normalization
tools stripped out the _hintFormat1_ hint data as invalid elements.


"""

from __future__ import print_function, absolute_import, unicode_literals

import ast
import hashlib
import logging
import os
import re
import shutil

from collections import OrderedDict

from fontTools.misc.py23 import SimpleNamespace, open, tostr, round
from fontTools.pens.basePen import BasePen
from fontTools.pens.pointPen import AbstractPointPen
from fontTools.ufoLib import (UFOReader, UFOWriter, DEFAULT_GLYPHS_DIRNAME,
                              DEFAULT_LAYER_NAME)
from fontTools.ufoLib.errors import UFOLibError

from . import fdTools, FontParseError


log = logging.getLogger(__name__)

_hintFormat1_ = """

Deprecated. See _hintFormat2_ below.

A <hintset> element identifies a specific point by its name, and
describes a new set of stem hints which should be applied before the
specific point.

A <flex> element identifies a specific point by its name. The point is
the first point of a curve. The presence of the <flex> element is a
processing suggestion, that the curve and its successor curve should
be converted to a flex operator.

One challenge in applying the hintset and flex elements is that in the
GLIF format, there is no explicit start and end operator: the first path
operator is both the end and the start of the path. I have chosen to
convert this to T1 by taking the first path operator, and making it a
move-to. I then also use it as the last path operator. An exception is a
line-to; in T1, this is omitted, as it is implied by the need to close
the path. Hence, if a hintset references the first operator, there is a
potential ambiguity: should it be applied before the T1 move-to, or
before the final T1 path operator? The logic here applies it before the
move-to only.

 <glyph>
...
    <lib>
        <dict>
            <key><com.adobe.type.autohint><key>
            <data>
                <hintSetList>
                    <hintset pointTag="point name">
                      (<hstem pos="<decimal value>" width="decimal value" />)*
                      (<vstem pos="<decimal value>" width="decimal value" />)*
                      <!-- where n1-5 are decimal values -->
                      <hstem3 stem3List="n0,n1,n2,n3,n4,n5" />*
                      <!-- where n1-5 are decimal values -->
                      <vstem3 stem3List="n0,n1,n2,n3,n4,n5" />*
                    </hintset>*
                    (<hintSetList>*
                        (<hintset pointIndex="positive integer">
                            (<stemindex>positive integer</stemindex>)+
                        </hintset>)+
                    </hintSetList>)*
                    <flexList>
                        <flex pointTag="point Name" />
                    </flexList>*
                </hintSetList>
            </data>
        </dict>
    </lib>
</glyph>

Example from "B" in SourceCodePro-Regular
<key><com.adobe.type.autohint><key>
<data>
<hintSetList id="64bf4987f05ced2a50195f971cd924984047eb1d79c8c43e6a0054f59cc85
dea23a49deb20946a4ea84840534363f7a13cca31a81b1e7e33c832185173369086">
    <hintset pointTag="hintSet0000">
        <hstem pos="0" width="28" />
        <hstem pos="338" width="28" />
        <hstem pos="632" width="28" />
        <vstem pos="100" width="32" />
        <vstem pos="496" width="32" />
    </hintset>
    <hintset pointTag="hintSet0005">
        <hstem pos="0" width="28" />
        <hstem pos="338" width="28" />
        <hstem pos="632" width="28" />
        <vstem pos="100" width="32" />
        <vstem pos="454" width="32" />
        <vstem pos="496" width="32" />
    </hintset>
    <hintset pointTag="hintSet0016">
        <hstem pos="0" width="28" />
        <hstem pos="338" width="28" />
        <hstem pos="632" width="28" />
        <vstem pos="100" width="32" />
        <vstem pos="496" width="32" />
    </hintset>
</hintSetList>
</data>

"""

_hintFormat2_ = """

A <dict> element in the hintSetList array identifies a specific point by its
name, and describes a new set of stem hints which should be applied before the
specific point.

A <string> element in the flexList identifies a specific point by its name.
The point is the first point of a curve. The presence of the element is a
processing suggestion, that the curve and its successor curve should be
converted to a flex operator.

One challenge in applying the hintSetList and flexList elements is that in
the GLIF format, there is no explicit start and end operator: the first path
operator is both the end and the start of the path. I have chosen to convert
this to T1 by taking the first path operator, and making it a move-to. I then
also use it as the last path operator. An exception is a line-to; in T1, this
is omitted, as it is implied by the need to close the path. Hence, if a hintset
references the first operator, there is a potential ambiguity: should it be
applied before the T1 move-to, or before the final T1 path operator? The logic
here applies it before the move-to only.
 <glyph>
...
    <lib>
        <dict>
            <key><com.adobe.type.autohint></key>
            <dict>
                <key>id</key>
                <string> <fingerprint for glyph> </string>
                <key>hintSetList</key>
                <array>
                    <dict>
                      <key>pointTag</key>
                      <string> <point name> </string>
                      <key>stems</key>
                      <array>
                        <string>hstem <position value> <width value></string>*
                        <string>vstem <position value> <width value></string>*
                        <string>hstem3 <position value 0>...<position value 5>
                        </string>*
                        <string>vstem3 <position value 0>...<position value 5>
                        </string>*
                      </array>
                    </dict>*
                </array>

                <key>flexList</key>*
                <array>
                    <string><point name></string>+
                </array>
            </dict>
        </dict>
    </lib>
</glyph>

Example from "B" in SourceCodePro-Regular
<key><com.adobe.type.autohint><key>
<dict>
    <key>id</key>
    <string>64bf4987f05ced2a50195f971cd924984047eb1d79c8c43e6a0054f59cc85dea23
    a49deb20946a4ea84840534363f7a13cca31a81b1e7e33c832185173369086</string>
    <key>hintSetList</key>
    <array>
        <dict>
            <key>pointTag</key>
            <string>hintSet0000</string>
            <key>stems</key>
            <array>
                <string>hstem 338 28</string>
                <string>hstem 632 28</string>
                <string>hstem 100 32</string>
                <string>hstem 496 32</string>
            </array>
        </dict>
        <dict>
            <key>pointTag</key>
            <string>hintSet0005</string>
            <key>stems</key>
            <array>
                <string>hstem 0 28</string>
                <string>hstem 338 28</string>
                <string>hstem 632 28</string>
                <string>hstem 100 32</string>
                <string>hstem 454 32</string>
                <string>hstem 496 32</string>
            </array>
        </dict>
        <dict>
            <key>pointTag</key>
            <string>hintSet0016</string>
            <key>stems</key>
            <array>
                <string>hstem 0 28</string>
                <string>hstem 338 28</string>
                <string>hstem 632 28</string>
                <string>hstem 100 32</string>
                <string>hstem 496 32</string>
            </array>
        </dict>
    </array>
<dict>

"""

# UFO names
PUBLIC_GLYPH_ORDER = "public.glyphOrder"

ADOBE_DOMAIN_PREFIX = "com.adobe.type"

PROCESSED_LAYER_NAME = "%s.processedglyphs" % ADOBE_DOMAIN_PREFIX
PROCESSED_GLYPHS_DIRNAME = "glyphs.%s" % PROCESSED_LAYER_NAME

HASHMAP_NAME = "%s.processedHashMap" % ADOBE_DOMAIN_PREFIX
HASHMAP_VERSION_NAME = "hashMapVersion"
HASHMAP_VERSION = (1, 0)  # If major version differs, do not use.
AUTOHINT_NAME = tostr("autohint")
CHECKOUTLINE_NAME = "checkOutlines"

BASE_FLEX_NAME = "flexCurve"
FLEX_INDEX_LIST_NAME = "flexList"
HINT_DOMAIN_NAME1 = "com.adobe.type.autohint"
HINT_DOMAIN_NAME2 = "com.adobe.type.autohint.v2"
HINT_SET_LIST_NAME = "hintSetList"
HSTEM3_NAME = "hstem3"
HSTEM_NAME = "hstem"
POINT_NAME = "name"
POINT_TAG = "pointTag"
STEMS_NAME = "stems"
VSTEM3_NAME = "vstem3"
VSTEM_NAME = "vstem"
STACK_LIMIT = 46


class BezParseError(ValueError):
    pass


class UFOFontData:
    def __init__(self, path, log_only, write_to_default_layer):
        self._reader = UFOReader(path, validate=False)

        self.path = path
        self._glyphmap = None
        self._processed_layer_glyphmap = None
        self.newGlyphMap = {}
        self._fontInfo = None
        self._glyphsets = {}
        # If True, we are running in report mode and not doing any changes, so
        # we skip the hash map and process all glyphs.
        self.log_only = log_only
        # Used to store the hash of glyph data of already processed glyphs. If
        # the stored hash matches the calculated one, we skip the glyph.
        self._hashmap = None
        self.fontDict = None
        self.hashMapChanged = False
        # If True, then write data to the default layer
        self.writeToDefaultLayer = write_to_default_layer

    def getUnitsPerEm(self):
        return self.fontInfo.get("unitsPerEm", 1000)

    def getPSName(self):
        return self.fontInfo.get("postscriptFontName", "PSName-Undefined")

    @staticmethod
    def isCID():
        return False

    def convertToBez(self, name, read_hints, round_coords, doAll=False):
        # We do not yet support reading hints, so read_hints is ignored.
        width, bez, skip = self._get_or_skip_glyph(name, round_coords, doAll)
        if skip:
            return None, width

        bezString = "\n".join(bez)
        bezString = "\n".join(["% " + name, "sc", bezString, "ed", ""])
        return bezString, width

    def updateFromBez(self, bezData, name, width):
        # For UFO font, we don't use the width parameter:
        # it is carried over from the input glif file.
        layer = None
        if name in self.processedLayerGlyphMap:
            layer = PROCESSED_LAYER_NAME
        glyphset = self._get_glyphset(layer)

        glyph = BezGlyph(bezData)
        glyphset.readGlyph(name, glyph)
        self.newGlyphMap[name] = glyph

        # updateFromBez is called only if the glyph has been autohinted which
        # might also change its outline data. We need to update the edit status
        # in the hash map entry. I assume that convertToBez has been run
        # before, which will add an entry for this glyph.
        self.updateHashEntry(name)

    def save(self, path):
        if path is None:
            path = self.path

        if os.path.abspath(self.path) != os.path.abspath(path):
            # If user has specified a path other than the source font path,
            # then copy the entire UFO font, and operate on the copy.
            log.info("Copying from source UFO font to output UFO font before "
                     "processing...")
            if os.path.exists(path):
                shutil.rmtree(path)
            shutil.copytree(self.path, path)

        writer = UFOWriter(path, self._reader.formatVersion, validate=False)

        if self.hashMapChanged:
            self.writeHashMap(writer)
        self.hashMapChanged = False

        layer = PROCESSED_LAYER_NAME
        if self.writeToDefaultLayer:
            layer = None

        # Write layer contents.
        layers = {DEFAULT_LAYER_NAME: DEFAULT_GLYPHS_DIRNAME}
        if self.processedLayerGlyphMap or not self.writeToDefaultLayer:
            layers[PROCESSED_LAYER_NAME] = PROCESSED_GLYPHS_DIRNAME
        writer.layerContents.update(layers)
        writer.writeLayerContents([DEFAULT_LAYER_NAME, PROCESSED_LAYER_NAME])

        # Write glyphs.
        glyphset = writer.getGlyphSet(layer, defaultLayer=layer is None)
        for name, glyph in self.newGlyphMap.items():
            filename = self.glyphMap[name]
            if not self.writeToDefaultLayer and \
                    name in self.processedLayerGlyphMap:
                filename = self.processedLayerGlyphMap[name]
            glyphset.contents[name] = filename
            glyphset.writeGlyph(name, glyph, glyph.drawPoints)
        glyphset.writeContents()

    @property
    def hashMap(self):
        if self._hashmap is None:
            try:
                data = self._reader.readData(HASHMAP_NAME)
            except UFOLibError:
                data = None
            if data:
                hashmap = ast.literal_eval(data.decode("utf-8"))
            else:
                hashmap = {HASHMAP_VERSION_NAME: HASHMAP_VERSION}

            version = (0, 0)
            if HASHMAP_VERSION_NAME in hashmap:
                version = hashmap[HASHMAP_VERSION_NAME]

            if version[0] > HASHMAP_VERSION[0]:
                raise FontParseError("Hash map version is newer than "
                                     "psautohint. Please update.")
            elif version[0] < HASHMAP_VERSION[0]:
                log.info("Updating hash map: was older version")
                hashmap = {HASHMAP_VERSION_NAME: HASHMAP_VERSION}

            self._hashmap = hashmap
        return self._hashmap

    def writeHashMap(self, writer):
        hashMap = self.hashMap
        if not hashMap:
            return  # no glyphs were processed.

        hasMapKeys = hashMap.keys()
        hasMapKeys = sorted(hasMapKeys)
        data = ["{"]
        for gName in hasMapKeys:
            data.append("'%s': %s," % (gName, hashMap[gName]))
        data.append("}")
        data.append("")
        data = "\n".join(data)

        writer.writeData(HASHMAP_NAME, data.encode("utf-8"))

    def updateHashEntry(self, glyphName):
        # srcHash has already been set: we are fixing the history list.

        # Get hash entry for glyph
        srcHash, historyList = self.hashMap[glyphName]

        self.hashMapChanged = True
        # If the program is not in the history list, add it.
        if AUTOHINT_NAME not in historyList:
            historyList.append(AUTOHINT_NAME)

    def checkSkipGlyph(self, glyphName, newSrcHash, doAll):
        skip = False
        if self.log_only:
            return skip

        srcHash = None
        historyList = []

        # Get hash entry for glyph
        if glyphName in self.hashMap:
            srcHash, historyList = self.hashMap[glyphName]

        if srcHash == newSrcHash:
            if AUTOHINT_NAME in historyList:
                # The glyph has already been autohinted, and there have been no
                # changes since.
                skip = not doAll
            if not skip and AUTOHINT_NAME not in historyList:
                historyList.append(AUTOHINT_NAME)
        else:
            if CHECKOUTLINE_NAME in historyList:
                log.error("Glyph '%s' has been edited. You must first "
                          "run '%s' before running '%s'. Skipping.",
                          glyphName, CHECKOUTLINE_NAME, AUTOHINT_NAME)
                skip = True

            # If the source hash has changed, we need to delete the processed
            # layer glyph.
            self.hashMapChanged = True
            self.hashMap[glyphName] = [tostr(newSrcHash), [AUTOHINT_NAME]]
            if glyphName in self.processedLayerGlyphMap:
                del self.processedLayerGlyphMap[glyphName]

        return skip

    def _get_glyphset(self, layer_name=None):
        if layer_name not in self._glyphsets:
            glyphset = None
            try:
                glyphset = self._reader.getGlyphSet(layer_name)
            except UFOLibError:
                pass
            self._glyphsets[layer_name] = glyphset
        return self._glyphsets[layer_name]

    @staticmethod
    def get_glyph_bez(glyph, round_coords):
        pen = BezPen(glyph.glyphSet, round_coords)
        glyph.draw(pen)
        if not hasattr(glyph, "width"):
            glyph.width = 1000
        return pen.bez

    def _get_or_skip_glyph(self, name, round_coords, doAll):
        # Get default glyph layer data, so we can check if the glyph
        # has been edited since this program was last run.
        # If the program name is in the history list, and the srcHash
        # matches the default glyph layer data, we can skip.
        glyphset = self._get_glyphset()
        glyph = glyphset[name]
        bez = self.get_glyph_bez(glyph, round_coords)

        # Hash is always from the default glyph layer.
        hash_pen = HashPointPen(glyph)
        glyph.drawPoints(hash_pen)
        skip = self.checkSkipGlyph(name, hash_pen.getHash(), doAll)

        # If there is a glyph in the processed layer, get the outline from it.
        if name in self.processedLayerGlyphMap:
            glyphset = self._get_glyphset(PROCESSED_LAYER_NAME)
            glyph = glyphset[name]
            bez = self.get_glyph_bez(glyph, round_coords)

        return glyph.width, bez, skip

    def getGlyphList(self):
        glyphOrder = self._reader.readLib().get(PUBLIC_GLYPH_ORDER, [])
        glyphList = list(self._get_glyphset().keys())

        # Sort the returned glyph list by the glyph order as we depend in the
        # order for expanding glyph ranges.
        def key_fn(v):
            if v in glyphOrder:
                return glyphOrder.index(v)
            return len(glyphOrder)
        return sorted(glyphList, key=key_fn)

    @property
    def glyphMap(self):
        if self._glyphmap is None:
            glyphset = self._get_glyphset()
            self._glyphmap = glyphset.contents
        return self._glyphmap

    @property
    def processedLayerGlyphMap(self):
        if self._processed_layer_glyphmap is None:
            self._processed_layer_glyphmap = {}
            glyphset = self._get_glyphset(PROCESSED_LAYER_NAME)
            if glyphset is not None:
                self._processed_layer_glyphmap = glyphset.contents
        return self._processed_layer_glyphmap

    @property
    def fontInfo(self):
        if self._fontInfo is None:
            info = SimpleNamespace()
            self._reader.readInfo(info)
            self._fontInfo = vars(info)
        return self._fontInfo

    def getFontInfo(self, allow_no_blues, noFlex,
                    vCounterGlyphs, hCounterGlyphs, fdIndex=0):
        if self.fontDict is not None:
            return self.fontDict

        fdDict = fdTools.FDDict()
        # should be 1 if the glyphs are ideographic, else 0.
        fdDict.LanguageGroup = self.fontInfo.get("languagegroup", "0")
        fdDict.OrigEmSqUnits = self.getUnitsPerEm()
        fdDict.FontName = self.getPSName()
        upm = self.getUnitsPerEm()
        low = min(-upm * 0.25,
                  self.fontInfo.get("openTypeOS2WinDescent", 0) - 200)
        high = max(upm * 1.25,
                   self.fontInfo.get("openTypeOS2WinAscent", 0) + 200)
        # Make a set of inactive alignment zones: zones outside of the font
        # bbox so as not to affect hinting. Used when src font has no
        # BlueValues or has invalid BlueValues. Some fonts have bad BBox
        # values, so I don't let this be smaller than -upm*0.25, upm*1.25.
        inactiveAlignmentValues = [low, low, high, high]
        blueValues = self.fontInfo.get("postscriptBlueValues", [])
        numBlueValues = len(blueValues)
        if numBlueValues < 4:
            if allow_no_blues:
                blueValues = inactiveAlignmentValues
                numBlueValues = len(blueValues)
            else:
                raise FontParseError(
                    "Font must have at least four values in its "
                    "BlueValues array for PSAutoHint to work!")
        blueValues.sort()
        # The first pair only is a bottom zone, where the first value is the
        # overshoot position; the rest are top zones, and second value of the
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

        otherBlues = self.fontInfo.get("postscriptOtherBlues", [])
        numBlueValues = len(otherBlues)

        if len(otherBlues) > 0:
            i = 0
            numBlueValues = len(otherBlues)
            otherBlues.sort()
            for i in range(0, numBlueValues, 2):
                otherBlues[i] = otherBlues[i] - otherBlues[i + 1]
            otherBlues = [str(v) for v in otherBlues]
            numBlueValues = min(numBlueValues,
                                len(fdTools.kOtherBlueValueKeys))
            for i in range(numBlueValues):
                key = fdTools.kOtherBlueValueKeys[i]
                value = otherBlues[i]
                setattr(fdDict, key, value)

        vstems = self.fontInfo.get("postscriptStemSnapV", [])
        if not vstems:
            if allow_no_blues:
                # dummy value. Needs to be larger than any hint will likely be,
                # as the autohint program strips out any hint wider than twice
                # the largest global stem width.
                vstems = [fdDict.OrigEmSqUnits]
            else:
                raise FontParseError("Font does not have postscriptStemSnapV!")

        vstems.sort()
        if not vstems or (len(vstems) == 1 and vstems[0] < 1):
            # dummy value that will allow PyAC to run
            vstems = [fdDict.OrigEmSqUnits]
            log.warning("There is no value or 0 value for DominantV.")
        fdDict.DominantV = "[" + " ".join([str(v) for v in vstems]) + "]"

        hstems = self.fontInfo.get("postscriptStemSnapH", [])
        if not hstems:
            if allow_no_blues:
                # dummy value. Needs to be larger than any hint will likely be,
                # as the autohint program strips out any hint wider than twice
                # the largest global stem width.
                hstems = [fdDict.OrigEmSqUnits]
            else:
                raise FontParseError("Font does not have postscriptStemSnapH!")

        hstems.sort()
        if not hstems or (len(hstems) == 1 and hstems[0] < 1):
            # dummy value that will allow PyAC to run
            hstems = [fdDict.OrigEmSqUnits]
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

        fdDict.BlueFuzz = self.fontInfo.get("postscriptBlueFuzz", 1)
        # postscriptBlueShift
        # postscriptBlueScale
        self.fontDict = fdDict
        return fdDict

    def getfdInfo(self, allow_no_blues, noFlex, vCounterGlyphs, hCounterGlyphs,
                  glyphList, fdIndex=0):
        fdGlyphDict = None
        fdDict = self.getFontInfo(allow_no_blues, noFlex,
                                  vCounterGlyphs, hCounterGlyphs, fdIndex)
        fontDictList = [fdDict]

        # Check the fontinfo file, and add any other font dicts
        srcFontInfo = os.path.dirname(self.path)
        srcFontInfo = os.path.join(srcFontInfo, "fontinfo")
        maxX = self.getUnitsPerEm() * 2
        maxY = maxX
        minY = -self.getUnitsPerEm()
        if os.path.exists(srcFontInfo):
            with open(srcFontInfo, "r", encoding="utf-8") as fi:
                fontInfoData = fi.read()
            fontInfoData = re.sub(r"#[^\r\n]+", "", fontInfoData)

            if "FDDict" in fontInfoData:
                fdGlyphDict, fontDictList, finalFDict = \
                    fdTools.parseFontInfoFile(
                        fontDictList, fontInfoData, glyphList, maxY, minY,
                        self.getPSName())
                if finalFDict is None:
                    # If a font dict was not explicitly specified for the
                    # output font, use the first user-specified font dict.
                    fdTools.mergeFDDicts(fontDictList[1:], self.fontDict)
                else:
                    fdTools.mergeFDDicts([finalFDict], self.fontDict)

        return fdGlyphDict, fontDictList

    @staticmethod
    def close():
        return


class BezPen(BasePen):
    def __init__(self, glyph_set, round_coords):
        super(BezPen, self).__init__(glyph_set)
        self.round_coords = round_coords
        self.bez = []

    def _point(self, point):
        if self.round_coords:
            return " ".join("%d" % round(pt) for pt in point)
        return " ".join("%3f" % pt for pt in point)

    def _moveTo(self, pt):
        self.bez.append("%s mt" % self._point(pt))

    def _lineTo(self, pt):
        self.bez.append("%s dt" % self._point(pt))

    def _curveToOne(self, pt1, pt2, pt3):
        self.bez.append("%s ct" % self._point(pt1 + pt2 + pt3))

    @staticmethod
    def _qCurveToOne(pt1, pt2):
        raise FontParseError("Quadratic curves are not supported")

    def _closePath(self):
        self.bez.append("cp")


class HashPointPen(AbstractPointPen):
    DEFAULT_TRANSFORM = (1, 0, 0, 1, 0, 0)

    def __init__(self, glyph):
        self.glyphset = getattr(glyph, "glyphSet", None)
        self.width = round(getattr(glyph, "width", 1000), 9)
        self.data = ["w%s" % self.width]

    def getHash(self):
        data = "".join(self.data)
        if len(data) >= 128:
            data = hashlib.sha512(data.encode("ascii")).hexdigest()
        return data

    def beginPath(self, identifier=None, **kwargs):
        pass

    def endPath(self):
        pass

    def addPoint(self, pt, segmentType=None, smooth=False, name=None,
                 identifier=None, **kwargs):
        if segmentType is None:
            pt_type = ""
        else:
            pt_type = segmentType[0]
        self.data.append("%s%s%s" % (pt_type, repr(pt[0]), repr(pt[1])))

    def addComponent(self, baseGlyphName, transformation, identifier=None,
                     **kwargs):
        self.data.append("base:%s" % baseGlyphName)

        for i, v in enumerate(transformation):
            if transformation[i] != self.DEFAULT_TRANSFORM[i]:
                self.data.append(str(round(v, 9)))

        self.data.append("w%s" % self.width)
        glyph = self.glyphset[baseGlyphName]
        glyph.drawPoints(self)


class BezGlyph(object):
    def __init__(self, bez):
        self._bez = bez
        self.lib = {}

    @staticmethod
    def _draw(contours, pen):
        for contour in contours:
            pen.beginPath()
            for point in contour:
                x = point.get("x")
                y = point.get("y")
                segmentType = point.get("type", None)
                name = point.get("name", None)
                pen.addPoint((x, y), segmentType=segmentType, name=name)
            pen.endPath()

    def drawPoints(self, pen):
        contours, hints = convertBezToOutline(self._bez)
        self._draw(contours, pen)

        # Add the stem hints.
        if hints is not None:
            # Add this hash to the glyph data, as it is the hash which matches
            # the output outline data. This is not necessarily the same as the
            # hash of the source data; autohint can be used to change outlines.
            hash_pen = HashPointPen(self)
            self._draw(contours, hash_pen)
            hints["id"] = hash_pen.getHash()

            # Remove any existing hint data.
            for key in (HINT_DOMAIN_NAME1, HINT_DOMAIN_NAME2):
                if key in self.lib:
                    del self.lib[key]

            self.lib[HINT_DOMAIN_NAME2] = hints


class HintMask:
    # class used to collect hints for the current
    # hint mask when converting bez to T2.
    def __init__(self, listPos):
        # The index into the pointList is kept
        # so we can quickly find them later.
        self.listPos = listPos
        self.hList = []  # These contain the actual hint values.
        self.vList = []
        self.hstem3List = []
        self.vstem3List = []
        # The name attribute of the point which follows the new hint set.
        self.pointName = "hintSet" + str(listPos).zfill(4)

    def getHintSet(self):
        hintset = OrderedDict()
        hintset[POINT_TAG] = self.pointName
        hintset[STEMS_NAME] = []

        if len(self.hList) > 0 or len(self.hstem3List):
            hintset[STEMS_NAME].extend(
                    makeHintSet(self.hList, self.hstem3List, isH=True))

        if len(self.vList) > 0 or len(self.vstem3List):
            hintset[STEMS_NAME].extend(
                    makeHintSet(self.vList, self.vstem3List, isH=False))

        return hintset


def makeStemHintList(hintsStem3, isH):
    # In bez terms, the first coordinate in each pair is
    # absolute, second is relative, and hence is the width.
    if isH:
        op = HSTEM3_NAME
    else:
        op = VSTEM3_NAME
    posList = [op]
    for stem3 in hintsStem3:
        for pos, width in stem3:
            if pos % 1 == 0:
                pos = int(pos)
            if width % 1 == 0:
                width = int(width)
            posList.append("%s %s" % (pos, width))
    return " ".join(posList)


def makeHintList(hints, isH):
    # Add the list of hint operators
    # In bez terms, the first coordinate in each pair is
    # absolute, second is relative, and hence is the width.
    hintset = []
    for hint in hints:
        if not hint:
            continue
        pos = hint[0]
        if pos % 1 == 0:
            pos = int(pos)
        width = hint[1]
        if width % 1 == 0:
            width = int(width)
        if isH:
            op = HSTEM_NAME
        else:
            op = VSTEM_NAME
        hintset.append("%s %s %s" % (op, pos, width))
    return hintset


def fixStartPoint(contour, operators):
    # For the GLIF format, the idea of first/last point is funky, because
    # the format avoids identifying a start point. This means there is no
    # implied close-path line-to. If the last implied or explicit path-close
    # operator is a line-to, then replace the "mt" with linto, and remove
    # the last explicit path-closing line-to, if any. If the last op is a
    # curve, then leave the first two point args on the stack at the end of
    # the point list, and move the last curveto to the first op, replacing
    # the move-to.

    _, firstX, firstY = operators[0]
    lastOp, lastX, lastY = operators[-1]
    point = contour[0]
    if (firstX == lastX) and (firstY == lastY):
        del contour[-1]
        point["type"] = lastOp
    else:
        # we have an implied final line to. All we need to do
        # is convert the inital moveto to a lineto.
        point["type"] = "line"


bezToUFOPoint = {
    "mt": 'move',
    "rmt": 'move',
    "dt": 'line',
    "ct": 'curve',
}


def convertCoords(current_x, current_y):
    x, y = current_x, current_y
    if x % 1 == 0:
        x = int(x)
    if y % 1 == 0:
        y = int(y)
    return x, y


def convertBezToOutline(bezString):
    """
    Since the UFO outline element as no attributes to preserve,
    I can just make a new one.
    """
    # convert bez data to a UFO glif XML representation
    #
    # Convert all bez ops to simplest UFO equivalent
    # Add all hints to vertical and horizontal hint lists as encountered;
    # insert a HintMask class whenever a new set of hints is encountered
    # after all operators have been processed, convert HintMask items into
    # hintmask ops and hintmask bytes add all hints as prefix review operator
    # list to optimize T2 operators.
    # if useStem3 == 1, then any counter hints must be processed as stem3
    # hints, else the opposite.
    # Counter hints are used only in LanguageGroup 1 glyphs, aka ideographs

    bezString = re.sub(r"%.+?\n", "", bezString)  # supress comments
    bez = re.findall(r"(\S+)", bezString)
    flexes = []
    # Create an initial hint mask. We use this if
    # there is no explicit initial hint sub.
    hintmask = HintMask(0)
    hintmasks = [hintmask]
    vstem3_args = []
    hstem3_args = []
    args = []
    operators = []
    hintmask_name = None
    in_preflex = False
    hints = None
    op_index = 0
    current_x = 0
    current_y = 0
    contours = []
    contour = None
    has_hints = False

    for token in bez:
        try:
            val = float(token)
            args.append(val)
            continue
        except ValueError:
            pass
        if token == "newcolors":
            pass
        elif token in ["beginsubr", "endsubr"]:
            pass
        elif token == "snc":
            hintmask = HintMask(op_index)
            # If the new hints precedes any marking operator,
            # then we want throw away the initial hint mask we
            # made, and use the new one as the first hint mask.
            if op_index == 0:
                hintmasks = [hintmask]
            else:
                hintmasks.append(hintmask)
            hintmask_name = hintmask.pointName
        elif token == "enc":
            pass
        elif token == "rb":
            if hintmask_name is None:
                hintmask_name = hintmask.pointName
            hintmask.hList.append(args)
            args = []
            has_hints = True
        elif token == "ry":
            if hintmask_name is None:
                hintmask_name = hintmask.pointName
            hintmask.vList.append(args)
            args = []
            has_hints = True
        elif token == "rm":  # vstem3's are vhints
            if hintmask_name is None:
                hintmask_name = hintmask.pointName
            has_hints = True
            vstem3_args.append(args)
            args = []
            if len(vstem3_args) == 3:
                hintmask.vstem3List.append(vstem3_args)
                vstem3_args = []

        elif token == "rv":  # hstem3's are hhints
            has_hints = True
            hstem3_args.append(args)
            args = []
            if len(hstem3_args) == 3:
                hintmask.hstem3List.append(hstem3_args)
                hstem3_args = []

        elif token == "preflx1":
            # the preflx1/preflx2a sequence provides the same i as the flex
            # sequence; the difference is that the preflx1/preflx2a sequence
            # provides the argument values needed for building a Type1 string
            # while the flex sequence is simply the 6 rcurveto points. Both
            # sequences are always provided.
            args = []
            # need to skip all move-tos until we see the "flex" operator.
            in_preflex = True
        elif token == "preflx2a":
            args = []
        elif token == "flxa":  # flex with absolute coords.
            in_preflex = False
            flex_point_name = BASE_FLEX_NAME + str(op_index).zfill(4)
            flexes.append(flex_point_name)
            # The first 12 args are the 6 args for each of
            # the two curves that make up the flex feature.
            i = 0
            while i < 2:
                current_x = args[0]
                current_y = args[1]
                x, y = convertCoords(current_x, current_y)
                point = {"x": x, "y": y}
                contour.append(point)
                current_x = args[2]
                current_y = args[3]
                x, y = convertCoords(current_x, current_y)
                point = {"x": x, "y": y}
                contour.append(point)
                current_x = args[4]
                current_y = args[5]
                x, y = convertCoords(current_x, current_y)
                point_type = 'curve'
                point = {"x": x, "y": y, "type": point_type}
                contour.append(point)
                operators.append([point_type, current_x, current_y])
                op_index += 1
                if i == 0:
                    args = args[6:12]
                i += 1
            # attach the point name to the first point of the first curve.
            contour[-6][POINT_NAME] = flex_point_name
            if hintmask_name is not None:
                # We have a hint mask that we want to attach to the first
                # point of the flex op. However, there is already a flex
                # name in that attribute. What we do is set the flex point
                # name into the hint mask.
                hintmask.pointName = flex_point_name
                hintmask_name = None
            args = []
        elif token == "sc":
            pass
        elif token == "cp":
            pass
        elif token == "ed":
            pass
        else:
            if in_preflex and token in ["rmt", "mt"]:
                continue

            if token in ["rmt", "mt", "dt", "ct"]:
                op_index += 1
            else:
                raise BezParseError(
                    "Unhandled operation: '%s' '%s'." % (args, token))
            point_type = bezToUFOPoint[token]
            if token in ["rmt", "mt", "dt"]:
                if token in ["mt", "dt"]:
                    current_x = args[0]
                    current_y = args[1]
                else:
                    current_x += args[0]
                    current_y += args[1]
                x, y = convertCoords(current_x, current_y)
                point = {"x": x, "y": y, "type": point_type}

                if point_type == "move":
                    if contour is not None:
                        if len(contour) == 1:
                            # Just in case we see two moves in a row,
                            # delete the previous contour if it has
                            # only the move-to
                            log.info("Deleting moveto: %s adding %s",
                                     contours[-1], contour)
                            del contours[-1]
                        else:
                            # Fix the start/implied end path
                            # of the previous path.
                            fixStartPoint(contour, operators)
                    operators = []
                    contour = []
                    contours.append(contour)

                if hintmask_name is not None:
                    point[POINT_NAME] = hintmask_name
                    hintmask_name = None
                contour.append(point)
                operators.append([point_type, current_x, current_y])
            else:  # "ct"
                current_x = args[0]
                current_y = args[1]
                x, y = convertCoords(current_x, current_y)
                point = {"x": x, "y": y}
                contour.append(point)
                current_x = args[2]
                current_y = args[3]
                x, y = convertCoords(current_x, current_y)
                point = {"x": x, "y": y}
                contour.append(point)
                current_x = args[4]
                current_y = args[5]
                x, y = convertCoords(current_x, current_y)
                point = {"x": x, "y": y, "type": point_type}
                contour.append(point)
                if hintmask_name is not None:
                    # attach the pointName to the first point of the curve.
                    contour[-3][POINT_NAME] = hintmask_name
                    hintmask_name = None
                operators.append([point_type, current_x, current_y])
            args = []

    if contour is not None:
        if len(contour) == 1:
            # Just in case we see two moves in a row, delete
            # the previous contour if it has zero length.
            del contours[-1]
        else:
            fixStartPoint(contour, operators)

    # Add hints, if any.
    # Must be done at the end of op processing to make sure we have seen all
    # the hints in the bez string.
    # Note that the hintmasks are identified in the operators by the point
    # name. We will follow the T1 spec: a glyph may have stem3 counter hints
    # or regular hints, but not both.

    if has_hints or len(flexes) > 0:
        hints = OrderedDict()
        hints["id"] = ""

        # Convert the rest of the hint masks to a hintmask op and hintmask
        # bytes.
        hints[HINT_SET_LIST_NAME] = []
        for hintmask in hintmasks:
            hints[HINT_SET_LIST_NAME].append(hintmask.getHintSet())

        if len(flexes) > 0:
            hints[FLEX_INDEX_LIST_NAME] = []
            for pointTag in flexes:
                hints[FLEX_INDEX_LIST_NAME].append(pointTag)

    return contours, hints


def makeHintSet(hints, hintsStem3, isH):
    # A charstring may have regular v stem hints or vstem3 hints, but not both.
    # Same for h stem hints and hstem3 hints.
    hintset = []
    if len(hintsStem3) > 0:
        hintsStem3.sort()
        numHints = len(hintsStem3)
        hintLimit = int((STACK_LIMIT - 2) / 2)
        if numHints >= hintLimit:
            hintsStem3 = hintsStem3[:hintLimit]
            numHints = hintLimit
        hintset.append(makeStemHintList(hintsStem3, isH))
    else:
        hints.sort()
        numHints = len(hints)
        hintLimit = int((STACK_LIMIT - 2) / 2)
        if numHints >= hintLimit:
            hints = hints[:hintLimit]
            numHints = hintLimit
        hintset.extend(makeHintList(hints, isH))

    return hintset
