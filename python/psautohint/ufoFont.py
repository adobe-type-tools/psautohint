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

from __future__ import print_function, absolute_import

import ast
import hashlib
import logging
import os
import re
import shutil

try:
    import xml.etree.cElementTree as ET
except ImportError:
    import xml.etree.ElementTree as ET

from fontTools.misc.py23 import SimpleNamespace, open
from fontTools.pens.basePen import BasePen
from ufoLib import (UFOReader, UFOWriter, DATA_DIRNAME, DEFAULT_GLYPHS_DIRNAME,
                    DEFAULT_LAYER_NAME)
from ufoLib.pointPen import AbstractPointPen

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

XMLElement = ET.Element
xmlToString = ET.tostring


# UFO names
PUBLIC_GLYPH_ORDER = "public.glyphOrder"

ADOBE_DOMAIN_PREFIX = "com.adobe.type"

PROCESSED_LAYER_NAME = "AFDKO ProcessedGlyphs"
PROCESSED_GLYPHS_DIRNAME = "glyphs.%s.processedGlyphs" % ADOBE_DOMAIN_PREFIX

HASH_MAP_NAME = "%s.processedHashMap" % ADOBE_DOMAIN_PREFIX
HASH_MAP_VERSION_NAME = "hashMapVersion"
HASH_MAP_VERSION = (1, 0)  # If major version differs, do not use.
AUTOHINT_NAME = "autohint"
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
VSTEM3_NAME = "vstem3"
VSTEM_NAME = "vstem"
STACK_LIMIT = 46


class BezParseError(ValueError):
    pass


class UFOFontData:
    def __init__(self, path, log_only, allow_decimal_coords,
                 write_to_default_layer):
        self._reader = UFOReader(path, validate=False)

        self.path = path
        self.glyphMap = {}
        self.processedLayerGlyphMap = {}
        self.newGlyphMap = {}
        self.glyphList = []
        self._fontInfo = None
        self._glyphsets = {}
        # If True, we are running in report mode and not doing any changes, so
        # we skip the hash map and process all glyphs.
        self.log_only = log_only
        # Used to store the hash of glyph data of already processed glyphs. If
        # the stored hash matches the calculated one, we skip the glyph.
        self._hash_map = None
        self.fontDict = None
        self.hashMapChanged = False
        self.historyList = []
        # See documentation above.
        self.requiredHistory = [CHECKOUTLINE_NAME]
        # If False, then read data only from the default layer;
        # else read glyphs from processed layer, if it exists.
        self.useProcessedLayer = True
        # If True, then write data to the default layer
        self.writeToDefaultLayer = write_to_default_layer
        # if True, do NOT round x,y values when processing.
        self.allowDecimalCoords = allow_decimal_coords

        self._load_glyphmap()

    def getUnitsPerEm(self):
        return self.fontInfo.get("unitsPerEm", 1000)

    def getPSName(self):
        return self.fontInfo.get("postscriptFontName", "PSName-Undefined")

    @staticmethod
    def isCID():
        return False

    def convertToBez(self, glyphName, read_hints, doAll=False):
        # We do not yet support reading hints, so read_hints is ignored.
        width, bez, skip = self._get_or_skip_glyph(glyphName, doAll)
        if skip:
            return None, width

        bezString = "\n".join(bez)
        bezString = "\n".join(["% " + glyphName, "sc", bezString, "ed", ""])
        return bezString, width

    def updateFromBez(self, bezData, name, width):
        # For UFO font, we don't use the width parameter:
        # it is carried over from the input glif file.
        self.newGlyphMap[name] = self._convertBezToGLIF(name, bezData)

    def _convertBezToGLIF(self, name, bezString):
        # I need to replace the contours with data from the bez string.
        layer = None
        if self.useProcessedLayer and name in self.processedLayerGlyphMap:
            layer = PROCESSED_LAYER_NAME
        glyphset = self._get_glyphset(layer)

        glifXML = ET.fromstring(glyphset.getGLIF(name))

        outlineItem = None
        libIndex = outlineIndex = -1
        outlineIndex = outlineIndex = -1
        childIndex = 0
        for childElement in glifXML:
            if childElement.tag == "outline":
                outlineItem = childElement
                outlineIndex = childIndex
            if childElement.tag == "lib":
                libIndex = childIndex
            childIndex += 1

        newOutlineElement, hintInfoDict = convertBezToOutline(bezString)

        if outlineItem is None:
            # need to add it. Add it before the lib item, if any.
            if libIndex > 0:
                glifXML.insert(libIndex, newOutlineElement)
            else:
                glifXML.append(newOutlineElement)
        else:
            # remove the old one and add the new one.
            glifXML.remove(outlineItem)
            glifXML.insert(outlineIndex, newOutlineElement)

        # convertBezToGLIF is called only if the GLIF has been edited by a
        # tool. We need to update the edit status in the hash map entry. I
        # assume that convertToBez has been run before, which will add an entry
        # for this glyph.
        self.updateHashEntry(name)
        # Add the stem hints.
        if hintInfoDict is not None:
            widthXML = glifXML.find("advance")
            if widthXML is not None:
                width = int(widthXML.get("width"))
            else:
                width = 1000

            new_hash = "w%s" % width
            for contour in newOutlineElement:
                if contour.tag == "contour":
                    for child in contour:
                        if child.tag == "point":
                            try:
                                pt_type = child.attrib["type"][0]
                            except KeyError:
                                pt_type = ""
                            new_hash += "%s%s%s" % (pt_type, child.attrib["x"],
                                                    child.attrib["y"])
            if len(new_hash) >= 128:
                new_hash = hashlib.sha512(new_hash.encode("ascii")).hexdigest()

            # We add this hash to the T1 data, as it is the hash which matches
            # the output outline data. This is not necessarily the same as the
            # hash of the source data - autohint can be used to change
            # outlines.
            if libIndex > 0:
                libItem = glifXML[libIndex]
            else:
                libItem = XMLElement("lib")
                glifXML.append(libItem)

            dictItem = libItem.find("dict")
            if dictItem is None:
                dictItem = XMLElement("dict")
                libItem.append(dictItem)

            # Remove any existing hint data.
            childList = list(dictItem)
            for i, childItem in enumerate(childList):
                if (childItem.tag == "key") and (
                    (childItem.text == HINT_DOMAIN_NAME1) or (
                        childItem.text == HINT_DOMAIN_NAME2)):
                    dictItem.remove(childItem)  # remove key
                    dictItem.remove(childList[i + 1])  # remove data item.

            glyphDictItem = dictItem
            key = XMLElement("key")
            key.text = HINT_DOMAIN_NAME2
            glyphDictItem.append(key)

            glyphDictItem.append(hintInfoDict)

            childList = list(hintInfoDict)
            idValue = childList[1]
            idValue.text = new_hash

        addWhiteSpace(glifXML, 0)
        return glifXML

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

        # Write glyphs.
        glyphset = writer.getGlyphSet(layer, defaultLayer=layer is None)
        for name, glifXML in self.newGlyphMap.items():
            filename = self.glyphMap[name]
            if not self.writeToDefaultLayer and \
                    name in self.processedLayerGlyphMap:
                filename = self.processedLayerGlyphMap[name]
            path = os.path.join(glyphset.dirName, filename)
            et = ET.ElementTree(glifXML)
            with open(path, "wb") as fp:
                et.write(fp, encoding="UTF-8", xml_declaration=True)
            glyphset.contents[name] = filename
        glyphset.writeContents()

        # Write layer contents.
        layers = {DEFAULT_LAYER_NAME: DEFAULT_GLYPHS_DIRNAME}
        if self.processedLayerGlyphMap:
            layers[PROCESSED_LAYER_NAME] = PROCESSED_GLYPHS_DIRNAME
        writer.layerContents.update(layers)
        writer.writeLayerContents([DEFAULT_LAYER_NAME, PROCESSED_LAYER_NAME])

    @property
    def hashMap(self):
        if self._hash_map is None:
            data = self._reader.readBytesFromPath(
                os.path.join(DATA_DIRNAME, HASH_MAP_NAME))
            if data:
                newMap = ast.literal_eval(data.decode("utf-8"))
            else:
                newMap = {HASH_MAP_VERSION_NAME: HASH_MAP_VERSION}

            try:
                version = newMap[HASH_MAP_VERSION_NAME]
                if version[0] > HASH_MAP_VERSION[0]:
                    raise FontParseError("Hash map version is newer than "
                                         "program. Please update the FDK")
                elif version[0] < HASH_MAP_VERSION[0]:
                    log.info("Updating hash map: was older version")
                    newMap = {HASH_MAP_VERSION_NAME: HASH_MAP_VERSION}
            except KeyError:
                log.info("Updating hash map: was older version")
                newMap = {HASH_MAP_VERSION_NAME: HASH_MAP_VERSION}
            self._hash_map = newMap
        return self._hash_map

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

        writer.writeBytesToPath(os.path.join(DATA_DIRNAME, HASH_MAP_NAME),
                                data.encode("utf-8"))

    def updateHashEntry(self, glyphName):
        # srcHarsh has already been set: we are fixing the history list.
        if self.log_only:
            return
        # Get hash entry for glyph
        srcHash, historyList = self.hashMap[glyphName]

        self.hashMapChanged = True
        # If the program always reads data from the default layer,
        # and we have just created a new glyph in the processed layer,
        # then reset the history.
        if not self.useProcessedLayer:
            self.hashMap[glyphName] = [srcHash, [AUTOHINT_NAME]]
        else:
            # If the program is not in the history list, add it.
            if AUTOHINT_NAME not in historyList:
                historyList.append(AUTOHINT_NAME)

    def checkSkipGlyph(self, glyphName, newSrcHash, doAll):
        skip = False
        if self.log_only:
            return skip

        hashEntry = srcHash = None
        historyList = []
        programHistoryIndex = -1  # not found in historyList

        # Get hash entry for glyph
        try:
            hashEntry = self.hashMap[glyphName]
            srcHash, historyList = hashEntry
            try:
                programHistoryIndex = historyList.index(AUTOHINT_NAME)
            except ValueError:
                pass
        except KeyError:
            # Glyph is as yet untouched by any program.
            pass

        if srcHash == newSrcHash:
            if programHistoryIndex >= 0:
                # The glyph has already been processed by this program,
                # and there have been no changes since.
                skip = not doAll
            if not skip:
                if not self.useProcessedLayer:  # case for Checkoutlines
                    self.hashMapChanged = True
                    self.hashMap[glyphName] = [newSrcHash, [AUTOHINT_NAME]]
                    glyphset = self._get_glyphset(PROCESSED_LAYER_NAME)
                    if glyphset and glyphName in glyphset:
                        glyphset.deleteGlyph(glyphName)
                else:
                    if programHistoryIndex < 0:
                        historyList.append(AUTOHINT_NAME)
        else:
            if self.useProcessedLayer:  # case for autohint
                # default layer glyph and stored glyph hash differ, and
                # useProcessedLayer is True. If any of the programs in
                # requiredHistory in are in the historyList, we need to
                # complain and skip.
                foundMatch = False
                if len(historyList) > 0:
                    for programName in self.requiredHistory:
                        if programName in historyList:
                            foundMatch = True
                if foundMatch:
                    log.error("Glyph '%s' has been edited. You must first "
                              "run '%s' before running '%s'. Skipping.",
                              glyphName, self.requiredHistory,
                              AUTOHINT_NAME)
                    skip = True

            # If the source hash has changed, we need to delete
            # the processed layer glyph.
            self.hashMapChanged = True
            self.hashMap[glyphName] = [newSrcHash, [AUTOHINT_NAME]]
            if glyphName in self.processedLayerGlyphMap:
                del self.processedLayerGlyphMap[glyphName]

        return skip

    def _get_glyphset(self, layer_name=None):
        if layer_name not in self._glyphsets:
            glyphset = None
            if layer_name is None or layer_name in \
                    self._reader.getLayerNames():
                glyphset = self._reader.getGlyphSet(layer_name)
            self._glyphsets[layer_name] = glyphset
        return self._glyphsets[layer_name]

    def _get_glyph(self, name, layer_name=None):
        width, bez = None, None
        glyphset = self._get_glyphset(layer_name)
        if glyphset is not None:
            pen = BezPen(glyphset, self)
            glyph = glyphset[name]
            glyph.draw(pen)
            width = getattr(glyph, "width", 1000)
            bez = pen.bez

        return width, bez

    def _build_glyph_hash(self, name, width, layer_name=None):
        # Hash is always from the default glyph layer.
        glyphset = self._get_glyphset(layer_name)
        if glyphset is None:
            return None
        glyph = glyphset[name]

        pen = HashPointPen(glyphset, width)
        glyph.drawPoints(pen)

        return pen.getHash()

    def _get_or_skip_glyph(self, name, doAll):
        # Get default glyph layer data, so we can check if the glyph
        # has been edited since this program was last run.
        # If the program name is in the history list, and the srcHash
        # matches the default glyph layer data, we can skip.
        width, bez = self._get_glyph(name)
        if bez is None:
            return None, None, True

        new_hash = self._build_glyph_hash(name, width)
        skip = self.checkSkipGlyph(name, new_hash, doAll)

        # If self.useProcessedLayer and there is a glyph
        # in the processed layer, get the outline from that.
        if self.useProcessedLayer and name in self.processedLayerGlyphMap:
            width, bez = self._get_glyph(name, PROCESSED_LAYER_NAME)
            if bez is None:
                return None, None, True

        return width, bez, skip

    def getGlyphList(self):
        return self.glyphList

    def _load_glyphmap(self):
        # Need to both get the list of glyphs in the font, and also the glyph
        # order. The latter is taken from the public.glyphOrder key in the lib,
        # if it exists, else it is taken from the contents.  Any existing
        # glyphs which are not named in the public.glyphOrder are sorted after
        # all glyphs which are named in the public.glyphOrder, in the order
        # that they occurred in contents.plist.
        glyphset = self._get_glyphset()
        self.glyphMap = glyphset.contents
        self.glyphList = glyphset.keys()

        self.orderMap = {}
        fontlib = self._reader.readLib()
        glyphOrder = fontlib.get(PUBLIC_GLYPH_ORDER, self.glyphList)
        for i, name in enumerate(glyphOrder):
            self.orderMap[name] = i

        # If there are glyphs in the font which are not named in the
        # public.glyphOrder entry, add them in the order of the
        # contents.plist file.
        for name in self.glyphList:
            if name not in self.orderMap:
                self.orderMap[name] = len(self.orderMap)
        self.glyphList = sorted(list(self.orderMap.keys()))

        # I also need to get the glyph map for the processed layer,
        # and use this when the glyph is read from the processed layer.
        # glyph file names that differ from what is in the default glyph layer.
        # Because checkOutliensUFO used the defcon library, it can write
        glyphset = self._get_glyphset(PROCESSED_LAYER_NAME)
        if glyphset is not None:
            self.processedLayerGlyphMap = glyphset.contents

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

    def getGlyphID(self, glyphName):
        try:
            gid = self.orderMap[glyphName]
        except IndexError:
            raise FontParseError(
                "Could not find glyph name '%s' in UFO font contents plist. "
                "'%s'. " % (glyphName, self.path))
        return gid

    @staticmethod
    def close():
        return


class BezPen(BasePen):
    def __init__(self, glyph_set, font):
        super(BezPen, self).__init__(glyph_set)
        self.font = font
        self.bez = []

    def _point(self, point):
        if self.font.allowDecimalCoords:
            return " ".join("%3f" % pt for pt in point)
        else:
            return " ".join("%d" % round(pt) for pt in point)

    def _moveTo(self, pt):
        self.bez.append("%s mt" % self._point(pt))

    def _lineTo(self, pt):
        self.bez.append("%s dt" % self._point(pt))

    def _curveToOne(self, pt1, pt2, pt3):
        pt1 = self._point(pt1)
        pt2 = self._point(pt2)
        pt3 = self._point(pt3)
        self.bez.append("%s %s %s ct" % (pt1, pt2, pt3))

    @staticmethod
    def _qCurveToOne(pt1, pt2):
        raise FontParseError("Quadratic curves are not supported")

    def _closePath(self):
        self.bez.append("cp")


class HashPointPen(AbstractPointPen):
    DEFAULT_TRANSFORM = (1, 0, 0, 1, 0, 0)

    def __init__(self, glyphset, width):
        self.glyphset = glyphset
        self.width = width
        self.data = ["w%s" % width]

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
        self.data.append("%s%s%s" % (pt_type, pt[0], pt[1]))

    def addComponent(self, baseGlyphName, transformation, identifier=None,
                     **kwargs):
        self.data.append("base:%s" % baseGlyphName)

        for i, v in enumerate(transformation):
            if transformation[i] != self.DEFAULT_TRANSFORM[i]:
                self.data.append(str(v))

        self.data.append("w%s" % self.width)
        glyph = self.glyphset[baseGlyphName]
        glyph.drawPoints(self)


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

    def addHintSet(self, hintSetList):
        # Add the hint set to hintSetList
        newHintSetDict = XMLElement("dict")
        hintSetList.append(newHintSetDict)

        newHintSetKey = XMLElement("key")
        newHintSetKey.text = POINT_TAG
        newHintSetDict.append(newHintSetKey)

        newHintSetValue = XMLElement("string")
        newHintSetValue.text = self.pointName
        newHintSetDict.append(newHintSetValue)

        stemKey = XMLElement("key")
        stemKey.text = "stems"
        newHintSetDict.append(stemKey)

        newHintSetArray = XMLElement("array")
        newHintSetDict.append(newHintSetArray)

        if (len(self.hList) > 0) or (len(self.vstem3List)):
            isH = True
            addHintList(self.hList, self.hstem3List, newHintSetArray, isH)

        if (len(self.vList) > 0) or (len(self.vstem3List)):
            isH = False
            addHintList(self.vList, self.vstem3List, newHintSetArray, isH)


def makeStemHintList(hintsStem3, stemList, isH):
    # In bez terms, the first coordinate in each pair is
    # absolute, second is relative, and hence is the width.
    if isH:
        op = HSTEM3_NAME
    else:
        op = VSTEM3_NAME
    newStem = XMLElement("string")
    posList = [op]
    for stem3 in hintsStem3:
        for pos, width in stem3:
            if (isinstance(pos, float)) and (int(pos) == pos):
                pos = int(pos)
            if (isinstance(width, float)) and (int(width) == width):
                width = int(width)
            posList.append("%s %s" % (pos, width))
    posString = " ".join(posList)
    newStem.text = posString
    stemList.append(newStem)


def makeHintList(hints, newHintSetArray, isH):
    # Add the list of hint operators
    # In bez terms, the first coordinate in each pair is
    # absolute, second is relative, and hence is the width.
    for hint in hints:
        if not hint:
            continue
        pos = hint[0]
        if (isinstance(pos, float)) and (int(pos) == pos):
            pos = int(pos)
        width = hint[1]
        if (isinstance(width, float)) and (int(width) == width):
            width = int(width)
        if isH:
            op = HSTEM_NAME
        else:
            op = VSTEM_NAME
        newStem = XMLElement("string")
        newStem.text = "%s %s %s" % (op, pos, width)
        newHintSetArray.append(newStem)


def addFlexHint(flexList, flexArray):
    for pointTag in flexList:
        newFlexTag = XMLElement("string")
        newFlexTag.text = pointTag
        flexArray.append(newFlexTag)


def fixStartPoint(outlineItem, opList):
    # For the GLIF format, the idea of first/last point is funky, because
    # the format avoids identifying a start point. This means there is no
    # implied close-path line-to. If the last implied or explicit path-close
    # operator is a line-to, then replace the "mt" with linto, and remove
    # the last explicit path-closing line-to, if any. If the last op is a
    # curve, then leave the first two point args on the stack at the end of
    # the point list, and move the last curveto to the first op, replacing
    # the move-to.

    _, firstX, firstY = opList[0]
    lastOp, lastX, lastY = opList[-1]
    firstPointElement = outlineItem[0]
    if (firstX == lastX) and (firstY == lastY):
        del outlineItem[-1]
        firstPointElement.set("type", lastOp)
    else:
        # we have an implied final line to. All we need to do
        # is convert the inital moveto to a lineto.
        firstPointElement.set("type", "line")


bezToUFOPoint = {
    "mt": 'move',
    "rmt": 'move',
    "dt": 'line',
    "ct": 'curve',
}


def convertCoords(curX, curY):
    showX = int(curX)
    if showX != curX:
        showX = curX
    showY = int(curY)
    if showY != curY:
        showY = curY
    return showX, showY


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
    bezList = re.findall(r"(\S+)", bezString)
    if not bezList:
        return "", None, None
    flexList = []
    # Create an initial hint mask. We use this if
    # there is no explicit initial hint sub.
    hintMask = HintMask(0)
    hintMaskList = [hintMask]
    vStem3Args = []
    hStem3Args = []
    argList = []
    opList = []
    newHintMaskName = None
    inPreFlex = False
    hintInfoDict = None
    opIndex = 0
    curX = 0
    curY = 0
    newOutline = XMLElement("outline")
    outlineItem = None
    seenHints = False

    for token in bezList:
        try:
            val = float(token)
            argList.append(val)
            continue
        except ValueError:
            pass
        if token == "newcolors":
            pass
        elif token in ["beginsubr", "endsubr"]:
            pass
        elif token == "snc":
            hintMask = HintMask(opIndex)
            # If the new hints precedes any marking operator,
            # then we want throw away the initial hint mask we
            # made, and use the new one as the first hint mask.
            if opIndex == 0:
                hintMaskList = [hintMask]
            else:
                hintMaskList.append(hintMask)
            newHintMaskName = hintMask.pointName
        elif token == "enc":
            pass
        elif token == "rb":
            if newHintMaskName is None:
                newHintMaskName = hintMask.pointName
            hintMask.hList.append(argList)
            argList = []
            seenHints = True
        elif token == "ry":
            if newHintMaskName is None:
                newHintMaskName = hintMask.pointName
            hintMask.vList.append(argList)
            argList = []
            seenHints = True
        elif token == "rm":  # vstem3's are vhints
            if newHintMaskName is None:
                newHintMaskName = hintMask.pointName
            seenHints = True
            vStem3Args.append(argList)
            argList = []
            if len(vStem3Args) == 3:
                hintMask.vstem3List.append(vStem3Args)
                vStem3Args = []

        elif token == "rv":  # hstem3's are hhints
            seenHints = True
            hStem3Args.append(argList)
            argList = []
            if len(hStem3Args) == 3:
                hintMask.hstem3List.append(hStem3Args)
                hStem3Args = []

        elif token == "preflx1":
            # the preflx1/preflx2a sequence provides the same i as the flex
            # sequence; the difference is that the preflx1/preflx2a sequence
            # provides the argument values needed for building a Type1 string
            # while the flex sequence is simply the 6 rcurveto points. Both
            # sequences are always provided.
            argList = []
            # need to skip all move-tos until we see the "flex" operator.
            inPreFlex = True
        elif token == "preflx2a":
            argList = []
        elif token == "flxa":  # flex with absolute coords.
            inPreFlex = False
            flexPointName = BASE_FLEX_NAME + str(opIndex).zfill(4)
            flexList.append(flexPointName)
            curveCnt = 2
            i = 0
            # The first 12 args are the 6 args for each of
            # the two curves that make up the flex feature.
            while i < curveCnt:
                curX = argList[0]
                curY = argList[1]
                showX, showY = convertCoords(curX, curY)
                newPoint = XMLElement(
                    "point", {"x": "%s" % showX, "y": "%s" % showY})
                outlineItem.append(newPoint)
                curX = argList[2]
                curY = argList[3]
                showX, showY = convertCoords(curX, curY)
                newPoint = XMLElement(
                    "point", {"x": "%s" % showX, "y": "%s" % showY})
                outlineItem.append(newPoint)
                curX = argList[4]
                curY = argList[5]
                showX, showY = convertCoords(curX, curY)
                opName = 'curve'
                newPoint = XMLElement(
                    "point", {"x": "%s" % showX, "y": "%s" % showY,
                              "type": opName})
                outlineItem.append(newPoint)
                opList.append([opName, curX, curY])
                opIndex += 1
                if i == 0:
                    argList = argList[6:12]
                i += 1
            # attach the point name to the first point of the first curve.
            outlineItem[-6].set(POINT_NAME, flexPointName)
            if newHintMaskName is not None:
                # We have a hint mask that we want to attach to the first
                # point of the flex op. However, there is already a flex
                # name in that attribute. What we do is set the flex point
                # name into the hint mask.
                hintMask.pointName = flexPointName
                newHintMaskName = None
            argList = []
        elif token == "sc":
            pass
        elif token == "cp":
            pass
        elif token == "ed":
            pass
        else:
            if inPreFlex and token in ["rmt", "mt"]:
                continue

            if token in ["rmt", "mt", "dt", "ct"]:
                opIndex += 1
            else:
                raise BezParseError(
                    "Unhandled operation: '%s' '%s'." % (argList, token))
            opName = bezToUFOPoint[token]
            if token in ["rmt", "mt", "dt"]:
                if token in ["mt", "dt"]:
                    curX = argList[0]
                    curY = argList[1]
                else:
                    curX += argList[0]
                    curY += argList[1]
                showX, showY = convertCoords(curX, curY)
                newPoint = XMLElement(
                    "point", {"x": "%s" % showX, "y": "%s" % showY,
                              "type": "%s" % opName})

                if opName == "move":
                    if outlineItem is not None:
                        if len(outlineItem) == 1:
                            # Just in case we see two moves in a row,
                            # delete the previous outlineItem if it has
                            # only the move-to
                            log.info("Deleting moveto: %s adding %s",
                                     xmlToString(newOutline[-1]),
                                     xmlToString(outlineItem))
                            del newOutline[-1]
                        else:
                            # Fix the start/implied end path
                            # of the previous path.
                            fixStartPoint(outlineItem, opList)
                    opList = []
                    outlineItem = XMLElement('contour')
                    newOutline.append(outlineItem)

                if newHintMaskName is not None:
                    newPoint.set(POINT_NAME, newHintMaskName)
                    newHintMaskName = None
                outlineItem.append(newPoint)
                opList.append([opName, curX, curY])
            else:  # "ct"
                curX = argList[0]
                curY = argList[1]
                showX, showY = convertCoords(curX, curY)
                newPoint = XMLElement(
                    "point", {"x": "%s" % showX, "y": "%s" % showY})
                outlineItem.append(newPoint)
                curX = argList[2]
                curY = argList[3]
                showX, showY = convertCoords(curX, curY)
                newPoint = XMLElement(
                    "point", {"x": "%s" % showX, "y": "%s" % showY})
                outlineItem.append(newPoint)
                curX = argList[4]
                curY = argList[5]
                showX, showY = convertCoords(curX, curY)
                newPoint = XMLElement(
                    "point", {"x": "%s" % showX, "y": "%s" % showY,
                              "type": "%s" % opName})
                outlineItem.append(newPoint)
                if newHintMaskName is not None:
                    # attach the pointName to the first point of the curve.
                    outlineItem[-3].set(POINT_NAME, newHintMaskName)
                    newHintMaskName = None
                opList.append([opName, curX, curY])
            argList = []

    if outlineItem is not None:
        if len(outlineItem) == 1:
            # Just in case we see two moves in a row, delete
            # the previous outlineItem if it has zero length.
            del newOutline[-1]
        else:
            fixStartPoint(outlineItem, opList)

    # add hints, if any
    # Must be done at the end of op processing to make sure we have seen
    # all the hints in the bez string.
    # Note that the hintmasks are identified in the opList by the point name.
    # We will follow the T1 spec: a glyph may have stem3 counter hints or
    # regular hints, but not both.

    if (seenHints) or (len(flexList) > 0):
        hintInfoDict = XMLElement("dict")

        idItem = XMLElement("key")
        idItem.text = "id"
        hintInfoDict.append(idItem)

        idString = XMLElement("string")
        idString.text = "id"
        hintInfoDict.append(idString)

        hintSetListItem = XMLElement("key")
        hintSetListItem.text = HINT_SET_LIST_NAME
        hintInfoDict.append(hintSetListItem)

        hintSetListArray = XMLElement("array")
        hintInfoDict.append(hintSetListArray)
        # Convert the rest of the hint masks
        # to a hintmask op and hintmask bytes.
        for hintMask in hintMaskList:
            hintMask.addHintSet(hintSetListArray)

        if len(flexList) > 0:
            hintSetListItem = XMLElement("key")
            hintSetListItem.text = FLEX_INDEX_LIST_NAME
            hintInfoDict.append(hintSetListItem)

            flexArray = XMLElement("array")
            hintInfoDict.append(flexArray)
            addFlexHint(flexList, flexArray)

    return newOutline, hintInfoDict


def addHintList(hints, hintsStem3, newHintSetArray, isH):
    # A charstring may have regular v stem hints or vstem3 hints, but not both.
    # Same for h stem hints and hstem3 hints.
    if len(hintsStem3) > 0:
        hintsStem3.sort()
        numHints = len(hintsStem3)
        hintLimit = int((STACK_LIMIT - 2) / 2)
        if numHints >= hintLimit:
            hintsStem3 = hintsStem3[:hintLimit]
            numHints = hintLimit
        makeStemHintList(hintsStem3, newHintSetArray, isH)

    else:
        hints.sort()
        numHints = len(hints)
        hintLimit = int((STACK_LIMIT - 2) / 2)
        if numHints >= hintLimit:
            hints = hints[:hintLimit]
            numHints = hintLimit
        makeHintList(hints, newHintSetArray, isH)


def addWhiteSpace(parent, level):
    child = None
    childIndent = "\n" + ("  " * (level + 1))
    prentIndent = "\n" + ("  " * (level))
    for child in parent:
        child.tail = childIndent
        addWhiteSpace(child, level + 1)
    if child is not None:
        if parent.text is None:
            parent.text = childIndent
        child.tail = prentIndent
