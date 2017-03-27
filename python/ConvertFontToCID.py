#!/bin/env python

from __future__ import print_function

__doc__ = """
ConvertFontToCID.py. v 1.9 April 16 2016

Convert a Type 1 font to CID, given multiple hint dict defs in the
"fontinfo" file. See AC.py help, with the "-hfd" option, or the MakeOTF
user guide for details on this format.

The "fontinfo" file is expected to be in the same directory as the input
font file.

Note that this file makes a lot of temporary files, using the input font
path as the base file path, so the parent directory needs to be
read/write enabled.
"""
__copyright__ = """Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
"""


import re
import types
import FDKUtils


# Tokens seen in font info file that are not part of a FDDict or GlyphSet definition.
kBeginToken = "begin"
kEndToken = "end"
kFDDictToken = "FDDict"
kGlyphSetToken = "GlyphSet"
kFinalDictName = "FinalFont"
kDefaultDictName = "No Alignment Zones"
kBaseStateTokens = [ "FontName","FullName","IsBoldStyle","IsItalicStyle", "ConvertToCID",
				"PreferOS/2TypoMetrics","IsOS/2WidthWeigthSlopeOnly","IsOS/2OBLIQUE",
				"UseOldNameID4", "LicenseCode"
				]
kBlueValueKeys = [
			"BaselineOvershoot", # 0
			"BaselineYCoord", #1
			"CapHeight", #2
			"CapOvershoot", #3
			"LcHeight", #4
			"LcOvershoot", #5
			"AscenderHeight", #6
			"AscenderOvershoot", #7
			"FigHeight", #8
			"FigOvershoot", #9
			"Height5", #10
			"Height5Overshoot", #11
			"Height6", #12
			"Height6Overshoot", #13
			]

kOtherBlueValueKeys = [
			"Baseline5Overshoot", #0
			"Baseline5", #1
			"Baseline6Overshoot", #2
			"Baseline6", #3
			"SuperiorOvershoot", #4
			"SuperiorBaseline", #5
			"OrdinalOvershoot", #6
			"OrdinalBaseline", #7
			"DescenderOvershoot", #8
			"DescenderHeight", # 9
			]

kOtherFDDictKeys = ["FontName","OrigEmSqUnits","LanguageGroup",
				"DominantV","DominantH" ,
				"FlexOK", "VCounterChars", "HCounterChars", "BlueFuzz"]

kFontDictBluePairsName = "BlueValuesPairs" # We keep this in the FDDict, as it is easier to sort and validate as a list of pairs
kFontDictOtherBluePairsName = "OtherBlueValuesPairs"
kFontDictBluesName = "BlueValues" # Holds the actual string for the Type1 font dict
kFontDictOtherBluesName = "OtherBlues" # Holds the actual string for the Type1 font dict
kRunTimeFDDictKeys = ["DictName", kFontDictBluePairsName, kFontDictOtherBluePairsName, kFontDictBluesName, kFontDictOtherBluesName]				
kFDDictKeys = kOtherFDDictKeys + kBlueValueKeys + kOtherBlueValueKeys + kRunTimeFDDictKeys

class FontInfoParseError(ValueError):
	pass

class FDDict:
	def __init__(self):
		self.DictName=None
		for key in kFDDictKeys:
			exec("self.%s = None" % (key))			
		self.FlexOK="true"
	
	def getFontInfo(self):
		keys = dir(self)
		fiList = []
		for key in keys:
			if key.startswith("_") or (key in kRunTimeFDDictKeys):
				continue
			value = eval("self.%s" % (key))
			if type(value) == types.MethodType:
				continue
				
			if value != None:
				fiList.append("%s %s" % (key, value))
		return " ".join(fiList)

	def buildBlueLists(self):
		if (self.BaselineOvershoot == None):
			print("Error: FDDict definition %s is missing the BaselineYCoord/BaselineOvershoot values. These are required." % (self.DictName))
		elif (int(self.BaselineOvershoot) > 0):
			print("Error: The BaselineYCoord/BaselineOvershoot in FDDict definition %s must be a bottom zone - the BaselineOvershoot must be negative, not positive." % (self.DictName))
	
		blueKeyList = [kBlueValueKeys, kOtherBlueValueKeys]
		bluePairListNames = [kFontDictBluePairsName, kFontDictOtherBluePairsName]
		blueFieldNames = 	[kFontDictBluesName, kFontDictOtherBluesName]
		for i in [0,1]:
			keyList = blueKeyList[i]
			fieldName = blueFieldNames[i]
			pairFieldName = bluePairListNames[i]
			bluePairList = []
			keyIndex = 0
			for key in keyList:
				if key.endswith("Overshoot"):
					width = eval("self.%s" % (key))
					if width != None:
						width = int(width)
						baseName = key[:-len("Overshoot")]
						zonePos = None
						if key == "BaselineOvershoot":
							zonePos = eval("self.BaselineYCoord")
							zonePos = int(zonePos)
							tempKey = "BaselineYCoord"
						else:	
							for posSuffix in ["", "Height", "Baseline"]:
								tempKey = "%s%s" % (baseName, posSuffix)
								try:
									zonePos = eval("self.%s" % (tempKey))
									zonePos = int(zonePos)
									break
								except AttributeError:
									continue
						if zonePos == None:
							raise FontInfoParseError("Failed to find fontinfo  FDDict %s top/bottom zone name %s to match the zone width key '%s'." % (self.DictName, tempKey, key))
						if width < 0:
							topPos = zonePos
							bottomPos = zonePos + width
							isBottomZone = 1
							if (i == 0) and (key != "BaselineOvershoot"):
								print("Error: FontDict %s. Zone %s is a top zone, and the width (%s)  must be positive." %  (self.DictName, tempKey, width))
						else:
							bottomPos = zonePos
							topPos = zonePos + width
							isBottomZone = 0
							if (i == 1):
								print("Error: FontDict %s. Zone %s is a bottom zone, and so the width (%s) must be negative.." %  (self.DictName, tempKey, width))
						bluePairList.append((topPos, bottomPos, tempKey, self.DictName, isBottomZone))
						
			if bluePairList:
				bluePairList.sort()
				prevPair = bluePairList[0]
				zoneBuffer = 2*self.BlueFuzz + 1
				for pair in bluePairList[1:]:
					if prevPair[0] > pair[1]:
						print("Error in FDDict %s. The top of zone %s at %s overlaps zone %s with the bottom at %s." % (self.DictName, prevPair[2], prevPair[0], pair[2], pair[1]))
					elif abs(pair[1] -  prevPair[0]) <= zoneBuffer:
						print("Error in FDDict %s. The top of zone %s at %s is within the min spearation limit (%s units) of zone %s with the bottom at %s." % (self.DictName, prevPair[2], prevPair[0], zoneBuffer, pair[2], pair[1]))
					prevPair = pair
				exec("self.%s = %s" % (pairFieldName, bluePairList))
				bluesList = []
				for pairEntry in bluePairList:
					bluesList.append(pairEntry[1])
					bluesList.append(pairEntry[0])
				bluesList = [str(v) for v in bluesList]
				bluesList = "[%s]"  % (" ".join(bluesList) )
				#print(self.DictName, bluePairList)
				#print("\t", bluesList)
				exec("self.%s = \"%s\"" % (fieldName, bluesList))
			
		return
		
	def __repr__(self):
		printStr = []
		keys = dir(self)
		for key in keys:
			val = eval("self.%s" % (key))
			# print(key, type(val))
			if (val == None) or (type(val) == types.MethodType) or key.startswith("_"):
				continue
			printStr.append(key)
			printStr.append("%s" % (val))
		return " ".join(printStr)

def parseFontInfoFile(fontDictList, data, glyphList, maxY, minY, fontName, blueFuzz):
	# fontDictList may or may not already contain a font dict taken from the source font top FontDict.
	fdGlyphDict = {} # The map of glyph names to font dict: the index into fontDictList.
	finalFDict = None # The user-specified set of blue values to write into the output font, some sort of merge of the individual font dicts. May not be supplied.

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
					fdDict = FDDict()
					fdDict.DictName = dictName
					if dictName == kFinalDictName:
						# This is dict is NOT used to hint any glyphs; it is used
						# to supply the merged alignment zones and stem widths for
						# the final font.
						finalFDict = fdDict
					else:
						# save dict and FDIndex.
						fdIndexDict[dictName] = len(fontDictList)
						fontDictList.append(fdDict)
					
				elif token == kGlyphSetToken:
					state = glyphSetState
					setName = tokenList[i]
					i += 1
				else:
					raise FontInfoParseError("Unrecognized token after \"begin\" keyword: %s" % (token))
					
					
			elif token in kBaseStateTokens:
				# Discard value for base token.
				token = tokenList[i]
				i += 1
				if (token[0] in ["[",  "("]) and (not token[-1] in ["]",  ")"]):
					state = inValue
			else:
				raise FontInfoParseError("Unrecognized token in base state: %s" % (token))
				
		elif state == inValue:
			# We are processing a list value for a base state token.
			if token[-1] in  ["]",  ")"]:
				state = baseState # found the last token in the list value.

		elif state == inDictValue:
			dictValueList.append(token)
			if token[-1] in  ["]",  ")"]:
				value = " ".join(dictValueList)
				exec("fdDict.%s = \"%s\"" % (dictKeyWord, value))
				state = dictState # found the last token in the list value.

		elif state == glyphSetState:
			 # "end GlyphSet" marks end of set, else we are adding a new glyph name.
			 if (token == kEndToken) and tokenList[i] == kGlyphSetToken:
			 	if tokenList[i+1] != setName:
			 		raise FontInfoParseError("End glyph set name \"%s\" does not match begin glyph set name \"%s\"." % ( tokenList[i+1], setName))
			 	state = baseState
			 	i += 2
			 	setName = None
			 else:
			 	# Need to add matching glyphs.
			 	gi = 0
			 	for gname in glyphList:
			 		if re.search(token, gname):
			 			fdGlyphDict[gname] = [fdIndexDict[setName], gi] # fdIndex value
			 		gi += 1
			 	
		elif state == dictState:
			# "end FDDict" marks end of set, else we are adding a new glyph name.
			if (token == kEndToken) and tokenList[i] == kFDDictToken:
				if tokenList[i+1] != dictName:
					raise FontInfoParseError("End FDDict  name \"%s\" does not match begin FDDict name \"%s\"." % ( tokenList[i+1], dictName))
				if fdDict.DominantH == None:
					print("Warning: the FDDict '%s' in fontinfo has no DominantH value" % (dictName))
				if fdDict.DominantV == None:
					print("Warning: the FDDict '%s' in fontinfo has no DominantV value" % (dictName))
				if (fdDict.BlueFuzz == None):
					fdDict.BlueFuzz = blueFuzz
				fdDict.buildBlueLists()
				if fdDict.FontName == None:
					fdDict.FontName = fontName
				state = baseState
				i += 2
				dictName = None
				fdDict = None
			else:
				if token in kFDDictKeys:
					value = tokenList[i]
					i +=1
					if (value[0] in ["[",  "("]) and (not value[-1] in ["]",  ")"]):
						state = inDictValue
						dictValueList = [value]
						dictKeyWord = token
					else:
			 			exec("fdDict.%s = \"%s\"" % (token, value))
				else:
			 		raise FontInfoParseError("FDDict key \"%s\" in fdDict named \"%s\" is not recognised." % ( token, dictName))
			 	
	if lenSrcFontDictList != len(fontDictList):
		# There are some FDDict definitions. This means that we need to fix the default fontDict, inherited from the source font,
		# so that it has blues zones that will not affect hinting, e.g outside of the Font BBox. We do this becuase if there are
		# glyphs which are not assigned toa user specified font dict, it is becuase it doesn't make sense to provide alignment zones
		# for the glyph. Since AC does require at least one bottom zone and one top zone, we add one bottom and one top zone that are
		# outside the font BBox, so that hinting won't be affected by them.
		defaultFDDict = fontDictList[0]
		for key in kBlueValueKeys + kOtherBlueValueKeys:
			exec("defaultFDDict.%s = None" % (key))
		defaultFDDict.BaselineYCoord = minY - 100
		defaultFDDict.BaselineOvershoot = 0
		defaultFDDict.CapHeight = maxY + 100
		defaultFDDict.CapOvershoot = 0
		defaultFDDict.BlueFuzz = 0
		defaultFDDict.DictName=kDefaultDictName # "No Alignment Zones"
		defaultFDDict.FontName=fontName
		defaultFDDict.buildBlueLists()
		gi = 0
		for gname in glyphList:
			if gname not in fdGlyphDict:
			 	fdGlyphDict[gname] = [0, gi]
			gi += 1
					
	return fdGlyphDict, fontDictList, finalFDict

	
	
def mergeFDDicts(prevDictList, privateDict):
	# Extract the union of the stem widths and zones from the list of FDDicts, and
	# replace the current values in the topDict.
	blueZoneDict = {}
	otherBlueZoneDict = {}
	dominantHDict = {}
	dominantVDict = {}
	blueKeyList = [kBlueValueKeys, kOtherBlueValueKeys]
	bluePairListNames = [kFontDictBluePairsName, kFontDictOtherBluePairsName]
	zoneDictList = [blueZoneDict, otherBlueZoneDict]
	for prefDDict in prevDictList:
		for ki in [0,1]:
			zoneDict = zoneDictList[ki]
			bluePairName = bluePairListNames[ki]
			bluePairList = eval("prefDDict.%s" % (bluePairName))
			if not bluePairList:
				continue
			for topPos, bottomPos, zoneName, dictName, isBottomZone in bluePairList:
				zoneDict[ (topPos, bottomPos) ] = (isBottomZone, zoneName, prefDDict.DictName)
				
		# Now for the stem widths.
		stemNameList = ["DominantH", "DominantV"]
		stemDictList = [dominantHDict, dominantVDict]
		for wi in  (0,1):
			stemFieldName = stemNameList[wi]
			dList = eval("prefDDict.%s" % (stemFieldName))
			stemDict = stemDictList[wi]
			if dList != None:
				dList = dList[1:-1] # remove the braces
				dList = dList.split()
				dList = [int(d) for d in dList]
				for width in dList:
					stemDict[width] = prefDDict.DictName
		
			
	
	# Now we have collected all the stem widths and zones from all the dicts. See if we can merge them.
	goodBlueZoneList = []
	goodOtherBlueZoneList = []
	goodHStemList = []
	goodVStemList = []

	zoneDictList = [blueZoneDict, otherBlueZoneDict]
	goodZoneLists = [goodBlueZoneList, goodOtherBlueZoneList]
	stemDictList = [dominantHDict, dominantVDict]
	goodStemLists = [goodHStemList, goodVStemList]

	for ki in [0,1]:
		zoneDict = zoneDictList[ki]
		goodZoneList = goodZoneLists[ki]
		stemDict = stemDictList[ki]
		goodStemList = goodStemLists[ki]

		# Zones first.
		zoneList = zoneDict.keys()
		if not zoneList:
			continue
		zoneList.sort()
		# Now check for conflicts.
		prevZone = zoneList[0]
		goodZoneList.append(prevZone[1])
		goodZoneList.append(prevZone[0])
		seenProblem = 0
		prev = zoneList[0]
		zoneBuffer = 2*prefDDict.BlueFuzz + 1
		for zone in zoneList[1:]:
			if (ki == 0) and (len(zoneList) >= 14):
				print("Warning. For final FontDict, skipping BlueValues alignment zone %s from FDDict %s because there are already 7 zones." % (zoneName, fdDictName))
			elif (ki == 1) and (len(zoneList) >= 5):
				print("Warning. For final FontDict, skipping OtherBlues alignment zone %s from FDDict %s because there are already 5 zones." % (zoneName, fdDictName))
			if zone[1] <  prevZone[0]:
				curEntry = blueZoneDict[zone]
				prevEntry = blueZoneDict[prevZone]
				zoneName = curEntry[1]
				fdDictName = curEntry[2]
				prevZoneName = prevEntry[1]
				prevFDictName = prevEntry[2]
				print("Warning. For final FontDict, skipping zone %s in FDDict %s because it overlaps with zone %s in FDDict %s." % (zoneName, fdDictName, prevZoneName, prevFDictName))
			elif abs(zone[1] -  prevZone[0]) <= zoneBuffer:
				curEntry = blueZoneDict[zone]
				prevEntry = blueZoneDict[prevZone]
				zoneName = curEntry[1]
				fdDictName = curEntry[2]
				prevZoneName = prevEntry[1]
				prevFDictName = prevEntry[2]
				print("Warning. For final FontDict, skipping zone %s in FDDict %s because it is within the minimum separation allowed (%s units) of %s in FDDict %s." % (zoneName, fdDictName, zoneBuffer, prevZoneName, prevFDictName))
			else:
				goodZoneList.append(zone[1])
				goodZoneList.append(zone[0])
				
			prevZone = zone
		
		stemList = stemDict.keys()
		if not stemList:
			continue
		stemList.sort()
		# Now check for conflicts.
		prevStem = stemList[0]
		goodStemList.append(prevStem)
		for stem in stemList[1:]:
			if abs(stem  - prevStem) < 2:
				fdDictName = stemDict[stem]
				prevFDictName = stemDict[prevStem]
				print("Warning. For final FontDict, skipping stem width %s in FDDict %s because it overlaps in coverage with stem width %s in FDDict %s." % (stem, fdDictName, prevStem, prevFDictName))
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
	

def getBlueFuzz(fPath):
	blueFuzz = 1.0
	command = "tx -dump -0 \"%s\"" % (fPath)
	data = FDKUtils.runShellCmd(command)
	if not data:
		raise FontInfoParseError("Error: Failed getting log from tx from %s, when trying to get BlueFuzz." % (fPath))
		
	m = re.search(r"BlueFuzz\s+(\d+(?:\.\d+)*)", data) 
	if m:
		blueFuzz = int(m.group(1))
	return blueFuzz
