# Copyright 2014 Adobe. All rights reserved.

"""
Tools for processing "fontinfo" files. See "psautohint --doc-fddict", or the
MakeOTF user guide for details on this format.

The "fontinfo" file is expected to be in the same directory as the input
font file.
"""

import logging
import re
import numbers


log = logging.getLogger(__name__)

# Tokens seen in font info file that are not
# part of a FDDict or GlyphSet definition.
kBeginToken = "begin"
kEndToken = "end"
kFDDictToken = "FDDict"
kGlyphSetToken = "GlyphSet"
kFinalDictName = "FinalFont"
kDefaultDictName = "No Alignment Zones"
kBaseStateTokens = [
    "FontName",
    "FullName",
    "IsBoldStyle",
    "IsItalicStyle",
    "ConvertToCID",
    "PreferOS/2TypoMetrics",
    "IsOS/2WidthWeigthSlopeOnly",
    "IsOS/2OBLIQUE",
    "UseOldNameID4",
    "LicenseCode"
]
kBlueValueKeys = [
    "BaselineOvershoot",  # 0
    "BaselineYCoord",  # 1
    "CapHeight",  # 2
    "CapOvershoot",  # 3
    "LcHeight",  # 4
    "LcOvershoot",  # 5
    "AscenderHeight",  # 6
    "AscenderOvershoot",  # 7
    "FigHeight",  # 8
    "FigOvershoot",  # 9
    "Height5",  # 10
    "Height5Overshoot",  # 11
    "Height6",  # 12
    "Height6Overshoot",  # 13
]

kOtherBlueValueKeys = [
    "Baseline5Overshoot",  # 0
    "Baseline5",  # 1
    "Baseline6Overshoot",  # 2
    "Baseline6",  # 3
    "SuperiorOvershoot",  # 4
    "SuperiorBaseline",  # 5
    "OrdinalOvershoot",  # 6
    "OrdinalBaseline",  # 7
    "DescenderOvershoot",  # 8
    "DescenderHeight",  # 9
]

kOtherFDDictKeys = [
    "FontName",
    "OrigEmSqUnits",
    "LanguageGroup",
    "DominantV",
    "DominantH",
    "FlexOK",
    "VCounterChars",
    "HCounterChars",
    "BlueFuzz"
]

kFontDictBluePairsName = "BlueValuesPairs"
kFontDictOtherBluePairsName = "OtherBlueValuesPairs"

kRunTimeFDDictKeys = [
    "DictName",
    kFontDictBluePairsName,
    kFontDictOtherBluePairsName,
]
kFDDictKeys = (kOtherFDDictKeys +
               kBlueValueKeys +
               kOtherBlueValueKeys +
               kRunTimeFDDictKeys)
kFontInfoKeys = (kOtherFDDictKeys +
                 kBlueValueKeys +
                 kOtherBlueValueKeys +
                 ["StemSnapH", "StemSnapV"])


class FontInfoParseError(ValueError):
    pass


class FDDict:
    def __init__(self, fdIndex, dictName=None):
        self.fdIndex = fdIndex
        for key in kFDDictKeys:
            setattr(self, key, None)
        if dictName is not None:
            self.DictName = dictName
        elif fdIndex > 0:
            self.DictName = "FDArray index %s" % fdIndex
        else:
            self.DictName = "Default FDArray"
        self.FlexOK = True
        setattr(self, kFontDictBluePairsName, [])
        setattr(self, kFontDictOtherBluePairsName, [])

    def __str__(self):
        a = ''
        for k, v in vars(self).items():
            if k not in kFontInfoKeys or v is None:
                continue
            if isinstance(v, list):
                v = '[%s]' % ' '.join((str(i) for i in v))
            elif v is False:
                v = 'false'
            elif v is True:
                v = 'true'
            elif not isinstance(v, str):
                v = str(v)
            a += "%s %s\n" % (k, v)
        return a

    def setInfo(self, key, value):
        if key == 'LanguageGroup':
            if ((isinstance(value, numbers.Number) and value == 1) or
                    value == '1'):
                value = 1
            else:
                value = 0
        elif key in ('DominantV', 'DominantH'):
            value = [int(v) for v in value]
        elif key == 'FlexOK':
            if key is None:
                value = True        # default
            elif key == 'false':
                value = False
            else:
                value = bool(value)
        elif key in ('VCounterChars', 'HCounterChars'):
            pass  # already formatted in __main__::_parse_fontinfo_file
        elif key in ('FontName', 'DictName'):
            pass  # keep the string
        elif value is not None:
            value = int(value)

        setattr(self, key, value)

    def buildBlueLists(self):
        log.info("Building BlueValue lists for FDDict %s" % self.DictName)
        if self.BaselineOvershoot is None:
            raise FontInfoParseError(
                "FDDict definition %s is missing the BaselineYCoord/"
                "BaselineOvershoot values. These are required." %
                self.DictName)
        elif int(self.BaselineOvershoot) > 0:
            raise FontInfoParseError(
                "The BaselineYCoord/BaselineOvershoot in FDDict definition %s "
                "must be a bottom zone - the BaselineOvershoot must be "
                "negative, not positive." % self.DictName)

        blueKeyList = [kBlueValueKeys, kOtherBlueValueKeys]
        bluePairListNames = [kFontDictBluePairsName,
                             kFontDictOtherBluePairsName]
        for i in [0, 1]:
            keyList = blueKeyList[i]
            pairFieldName = bluePairListNames[i]
            bluePairList = []
            for key in keyList:
                if key.endswith("Overshoot"):
                    width = getattr(self, key)
                    if width is not None:
                        baseName = key[:-len("Overshoot")]
                        zonePos = None
                        if key == "BaselineOvershoot":
                            zonePos = self.BaselineYCoord
                            tempKey = "BaselineYCoord"
                        else:
                            for posSuffix in ["", "Height", "Baseline"]:
                                tempKey = "%s%s" % (baseName, posSuffix)
                                value = getattr(self, tempKey, None)
                                if value is not None:
                                    zonePos = value
                                    break
                        if zonePos is None:
                            raise FontInfoParseError(
                                "Failed to find fontinfo FDDict %s top/bottom "
                                "zone name %s to match the zone width key '%s'"
                                "." % (self.DictName, tempKey, key))
                        if width < 0:
                            topPos = zonePos
                            bottomPos = zonePos + width
                            isBottomZone = 1
                            if (i == 0) and (key != "BaselineOvershoot"):
                                raise FontInfoParseError(
                                    "FontDict %s. Zone %s is a top zone, and "
                                    "the width (%s) must be positive." %
                                    (self.DictName, tempKey, width))
                        else:
                            bottomPos = zonePos
                            topPos = zonePos + width
                            isBottomZone = 0
                            if i == 1 and width > 0:
                                raise FontInfoParseError(
                                    "FontDict %s. Zone %s is a bottom zone, "
                                    "and so the width (%s) must be negative." %
                                    (self.DictName, tempKey, width))
                        bluePairList.append((topPos, bottomPos, tempKey,
                                            self.DictName, isBottomZone))
                        log.debug("%s BlueValue %s: (%g, %g)" %
                                  ('Bottom' if isBottomZone else 'Top',
                                   tempKey, bottomPos, topPos))
            if bluePairList:
                bluePairList = sorted(bluePairList)
                prevPair = bluePairList[0]
                zoneBuffer = 2 * self.BlueFuzz + 1
                for pair in bluePairList[1:]:
                    if prevPair[0] > pair[1]:
                        raise FontInfoParseError(
                            "In FDDict %s. The top of zone %s at %s overlaps "
                            "zone %s with the bottom at %s." %
                            (self.DictName, prevPair[2], prevPair[0], pair[2],
                             pair[1]))
                    elif abs(pair[1] - prevPair[0]) <= zoneBuffer:
                        raise FontInfoParseError(
                            "In FDDict %s. The top of zone %s at %s is within "
                            "the min separation limit (%s units) of zone %s "
                            "with the bottom at %s." %
                            (self.DictName, prevPair[2], prevPair[0],
                             zoneBuffer, pair[2], pair[1]))
                    prevPair = pair
                setattr(self, pairFieldName, bluePairList)
        return

    def __repr__(self):
        fddict = {k: v for k, v in vars(self).items() if v is not None}
        return "<%s '%s' %s>" % (
            self.__class__.__name__, fddict.get('DictName', 'no name'), fddict)


def parseFontInfoFile(fontDictList, data, glyphList, maxY, minY, fontName):
    # fontDictList may or may not already contain a
    # font dict taken from the source font top FontDict.
    # The map of glyph names to font dict: the index into fontDictList.
    fdGlyphDict = {}
    # The user-specified set of blue values to write into the output font,
    # some sort of merge of the individual font dicts. May not be supplied.
    finalFDict = None

    blueFuzz = fontDictList[0].BlueFuzz

    # Get rid of comments.
    data = re.sub(r"#[^\r\n]+[\r\n]", "", data)

    # We assume that no items contain whitespace.
    tokenList = data.split()
    numTokens = len(tokenList)
    i = 0
    baseState = 0
    inValue = 1
    inDictValue = 2
    dictState = 3
    glyphSetState = 4
    fdIndexDict = {}
    lenSrcFontDictList = len(fontDictList)
    dictValueList = []
    dictKeyWord = ''

    state = baseState

    while i < numTokens:
        token = tokenList[i]
        i += 1
        if state == baseState:
            if token == kBeginToken:
                token = tokenList[i]
                i += 1
                if token == kFDDictToken:
                    state = dictState
                    dictName = tokenList[i]
                    i += 1
                    fdIndex = len(fontDictList)
                    fdDict = FDDict(fdIndex)
                    fdDict.DictName = dictName
                    if dictName == kFinalDictName:
                        # This is dict is NOT used to hint any glyphs; it is
                        # used to supply the merged alignment zones and stem
                        # widths for the final font.
                        finalFDict = fdDict
                    else:
                        # save dict and FDIndex.
                        fdIndexDict[dictName] = fdIndex
                        fontDictList.append(fdDict)

                elif token == kGlyphSetToken:
                    state = glyphSetState
                    setName = tokenList[i]
                    i += 1
                else:
                    raise FontInfoParseError(
                        "Unrecognized token after \"begin\" keyword: %s" %
                        token)

            elif token in kBaseStateTokens:
                # Discard value for base token.
                token = tokenList[i]
                i += 1
                if (token[0] in ["[", "("]) and (not token[-1] in ["]", ")"]):
                    state = inValue
            else:
                raise FontInfoParseError(
                    "Unrecognized token in base state: %s" % token)

        elif state == inValue:
            # We are processing a list value for a base state token.
            if token[-1] in ["]", ")"]:
                state = baseState  # found the last token in the list value.

        elif state == inDictValue:
            if token[-1] in ["]", ")"]:
                dictValueList.append(token[:-1])
                fdDict.setInfo(dictKeyWord, dictValueList)
                state = dictState  # found the last token in the list value.
            else:
                dictValueList.append(token)

        elif state == glyphSetState:
            # "end GlyphSet" marks end of set,
            # else we are adding a new glyph name.
            if (token == kEndToken) and tokenList[i] == kGlyphSetToken:
                if tokenList[i + 1] != setName:
                    raise FontInfoParseError(
                        "End glyph set name \"%s\" does not match begin glyph "
                        "set name \"%s\"." % (tokenList[i + 1], setName))
                state = baseState
                i += 2
                setName = None
            else:
                # Need to add matching glyphs.
                gi = 0
                for gname in glyphList:
                    if re.search(token, gname):
                        # fdIndex value
                        fdGlyphDict[gname] = fdIndexDict[setName]
                    gi += 1

        elif state == dictState:
            # "end FDDict" marks end of set,
            # else we are adding a new glyph name.
            if (token == kEndToken) and tokenList[i] == kFDDictToken:
                if tokenList[i + 1] != dictName:
                    raise FontInfoParseError(
                        "End FDDict  name \"%s\" does not match begin FDDict "
                        "name \"%s\"." % (tokenList[i + 1], dictName))
                if fdDict.DominantH is None:
                    log.warning("The FDDict '%s' in fontinfo has no "
                                "DominantH value", dictName)
                if fdDict.DominantV is None:
                    log.warning("The FDDict '%s' in fontinfo has no "
                                "DominantV value", dictName)
                if fdDict.BlueFuzz is None:
                    fdDict.BlueFuzz = blueFuzz
                fdDict.buildBlueLists()
                if fdDict.FontName is None:
                    fdDict.FontName = fontName
                state = baseState
                i += 2
                dictName = None
                fdDict = None
            else:
                if token in kFDDictKeys:
                    value = tokenList[i]
                    i += 1
                    if value[0] in ["[", "("]:
                        if not value[-1] in ["]", ")"]:
                            state = inDictValue
                            dictValueList = [value[1:]]
                            dictKeyWord = token
                        else:
                            fdDict.setInfo(token, value[1:-1])
                    else:
                        fdDict.setInfo(token, value)
                else:
                    raise FontInfoParseError(
                        "FDDict key \"%s\" in fdDict named \"%s\" is not "
                        "recognized." % (token, dictName))

    if lenSrcFontDictList != len(fontDictList):
        # There are some FDDict definitions. This means that we need
        # to fix the default fontDict, inherited from the source font,
        # so that it has blues zones that will not affect hinting,
        # e.g outside of the Font BBox. We do this becuase if there are
        # glyphs which are not assigned toa user specified font dict,
        # it is becuase it doesn't make sense to provide alignment zones
        # for the glyph. Since psautohint does require at least one bottom zone
        # and one top zone, we add one bottom and one top zone that are
        # outside the font BBox, so that hinting won't be affected by them.
        defaultFDDict = fontDictList[0]
        for key in kBlueValueKeys + kOtherBlueValueKeys:
            defaultFDDict.setInfo(key, None)
        defaultFDDict.BaselineYCoord = minY - 100
        defaultFDDict.BaselineOvershoot = 0
        defaultFDDict.CapHeight = maxY + 100
        defaultFDDict.CapOvershoot = 0
        defaultFDDict.BlueFuzz = 0
        defaultFDDict.DictName = kDefaultDictName  # "No Alignment Zones"
        defaultFDDict.FontName = fontName
        defaultFDDict.buildBlueLists()

    return fdGlyphDict, finalFDict


def mergeFDDicts(prevDictList, privateDict):
    # Extract the union of the stem widths and zones from the list
    # of FDDicts, and replace the current values in the topDict.
    blueZoneDict = {}
    otherBlueZoneDict = {}
    dominantHDict = {}
    dominantVDict = {}
    bluePairListNames = [kFontDictBluePairsName, kFontDictOtherBluePairsName]
    zoneDictList = [blueZoneDict, otherBlueZoneDict]
    for prefDDict in prevDictList:
        if prefDDict is None:
            continue
        for ki in [0, 1]:
            zoneDict = zoneDictList[ki]
            bluePairName = bluePairListNames[ki]
            bluePairList = getattr(prefDDict, bluePairName)
            if not bluePairList:
                continue
            for topPos, bottomPos, zoneName, _, isBottomZone in bluePairList:
                zoneDict[(topPos, bottomPos)] = (isBottomZone, zoneName,
                                                 prefDDict.DictName)
        # Now for the stem widths.
        stemNameList = ["DominantH", "DominantV"]
        stemDictList = [dominantHDict, dominantVDict]
        for wi in (0, 1):
            stemFieldName = stemNameList[wi]
            dList = getattr(prefDDict, stemFieldName)
            stemDict = stemDictList[wi]
            if dList is not None:
                for width in dList:
                    stemDict[width] = prefDDict.DictName

    # Now we have collected all the stem widths and zones
    # from all the dicts. See if we can merge them.
    goodBlueZoneList = []
    goodOtherBlueZoneList = []
    goodHStemList = []
    goodVStemList = []

    zoneDictList = [blueZoneDict, otherBlueZoneDict]
    goodZoneLists = [goodBlueZoneList, goodOtherBlueZoneList]
    stemDictList = [dominantHDict, dominantVDict]
    goodStemLists = [goodHStemList, goodVStemList]

    for ki in [0, 1]:
        zoneDict = zoneDictList[ki]
        goodZoneList = goodZoneLists[ki]
        stemDict = stemDictList[ki]
        goodStemList = goodStemLists[ki]

        # Zones first.
        zoneList = zoneDict.keys()
        if not zoneList:
            continue
        zoneList = sorted(zoneList)
        # Now check for conflicts.
        prevZone = zoneList[0]
        goodZoneList.append(prevZone[1])
        goodZoneList.append(prevZone[0])
        zoneBuffer = 2 * prefDDict.BlueFuzz + 1
        for zone in zoneList[1:]:
            curEntry = blueZoneDict[zone]
            prevEntry = blueZoneDict[prevZone]
            zoneName = curEntry[1]
            fdDictName = curEntry[2]
            prevZoneName = prevEntry[1]
            prevFDictName = prevEntry[2]

            if zone[1] < prevZone[0]:
                log.warning("For final FontDict, skipping zone %s in FDDict %s"
                            " because it overlaps with zone %s in FDDict %s.",
                            zoneName, fdDictName, prevZoneName, prevFDictName)
            elif abs(zone[1] - prevZone[0]) <= zoneBuffer:
                log.warning("For final FontDict, skipping zone %s in FDDict %s"
                            " because it is within the minimum separation "
                            "allowed (%s units) of %s in FDDict %s.",
                            zoneName, fdDictName, zoneBuffer, prevZoneName,
                            prevFDictName)
            else:
                goodZoneList.append(zone[1])
                goodZoneList.append(zone[0])

            prevZone = zone

        stemList = stemDict.keys()
        if not stemList:
            continue
        stemList = sorted(stemList)
        # Now check for conflicts.
        prevStem = stemList[0]
        goodStemList.append(prevStem)
        for stem in stemList[1:]:
            if abs(stem - prevStem) < 2:
                log.warning("For final FontDict, skipping stem width %s in "
                            "FDDict %s because it overlaps in coverage with "
                            "stem width %s in FDDict %s.",
                            stem, stemDict[stem], prevStem, stemDict[prevStem])
            else:
                goodStemList.append(stem)
            prevStem = stem
    if goodBlueZoneList:
        privateDict.BlueValues = goodBlueZoneList
    if goodOtherBlueZoneList:
        privateDict.OtherBlues = goodOtherBlueZoneList
    else:
        privateDict.OtherBlues = None
    if goodHStemList:
        privateDict.StemSnapH = goodHStemList
    else:
        privateDict.StemSnapH = None
    if goodVStemList:
        privateDict.StemSnapV = goodVStemList
    else:
        privateDict.StemSnapV = None
    return
