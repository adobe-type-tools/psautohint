# Copyright 2014 Adobe. All rights reserved.

from __future__ import print_function, absolute_import

import os
import re
import sys

from psautohint import _psautohint, autohint, ufoFont

__usage__ = """
Auto-hinting program for PostScript and OpenType/CFF fonts.
psautohint.py v1.50.0 Feb 11 2018

psautohint -h
psautohint -u
psautohint -hfd
psautohint -pfd
psautohint [-g <glyph list>] [-gf <filename>] [-xg <glyph list>]
           [-xgf <filename>] [-cf path] [-a] [-logOnly] [-log <logFile path>]
           [-r] [-q] [-qq] [-c] [-nf] [-ns] [-nb] [-wd] [-o <output font path>]
           FONT

"""

__help__ = __usage__ + """

Takes a list of fonts, and an optional list of glyphs, and hints the fonts. If
the list of glyphs is supplied, the hinting is limited to the specified glyphs.

Note that the hinting is better if the font's global alignment zones are set
usefully; at the very least, you should have entered values that capture
capital height, x-height, ascender and descender heights, and ascender and
descender overshoot. The reports provided by the stemHist tool are useful for
choosing these.

By default, autothint will hint all glyphs in the font. Options allow you to
specify a subset of the glyphs for hinting.

Options:

-h ... Print help.

-u ... Print usage.

-hfd . Print a description of the format for defining a set of alternate
       alignment zones in an "fontinfo" file.

-pfd . Print the default FDDict values for the source font: the alignment
       zone, stem width, and other global values. This is useful
       as a starting point for building FDDict defintions (see -hfd).

-pfdl  Print the list of user-defined FDDict values, and which glyphs
       are included in each. This is useful for checking your FDDict
       definitions and glyph search terms. (see -hfd).

-g <glyphID1>,<glyphID2>,...,<glyphIDn>
       Hint only the specified list of glyphs. Note that all glyphs will be
       written to the output file. The list must be comma-delimited. The glyph
       references may be glyph IDs, glyph names, or glyph CIDs. If the latter,
       the CID value must be prefixed with the string "/". There must be no
       white-space in the glyph list.
       Examples:
          psautohint -g A,B,C,68 myFont
          psautohint -g /1030,/434,/1535,68 myCIDFont

       A range of glyphs may be specified by providing two names separated
       only by a hyphen:
          psautohint -g zero-nine,onehalf myFont

       Note that the range will be resolved by expanding the glyph indices,
       not by alphabetic names.

-gf <file name>
       Hint only the list of glyphs contained in the specified file. The file
       must contain a comma-delimited list of glyph identifiers. Any number of
       space, tab, and new-line characters are permitted between glyph names
       and commas.

-xg, -xgf
       Same as -g and -gf, but will exclude the specified glyphs from hinting.

-cf <path>
       AC will try and add counter hints to only a short hard-coded list of
       glyphs:
          V counters: "m", "M", "T", "ellipsis"
          H counters: "element", "equivalence", "notelement", "divide".
       Counter hints help to keep the space between stems open and equal in
       size.

       To extend this list, use the option -cf followed by a path to a text
       file. The text file must contain one record per line. A record
       references one glyph, and should consist of a single letter V or H,
       to indicate whether the counters should be vertical or horizontal,
       followed by a space or tab, followed by a glyph name. A maximum of 64
       glyph names can be added to either the vertical or horizontal list.
       Example:
          V ffi
          V ffl
          V f_f_j

       Alternatively, if there is a file named "fontinfo" in the same directory
       as the source font, this script will look in that file for lines with
       this format:
          VCounterChar (<glyph name1> <glyph name2> ...)
          HCounterChar (<glyph name1> <glyph name2> ...)
       and add the referenced glyphs to the counter hint lists.
       Example:
          VCounterChar (ffi ffl f_f_j)

-logOnly
       Do not change any outlines, but report warnings for all selected glyphs,
       including those already hinted.
       The option -q is ignored.
       The option -a is implied.

-q ... Quiet mode. Will suppress comments from the auto-hinting library about
       recommended glyph outline changes.

-qq .. Really quiet mode. Will suppress all normal messages.

-c ... Permit changes to the glyph outline. When this is turned on, the
       psautohint program will fix a few issues: if there are many hint
       substitutions, it will try and shuffle the order of paths to reduce
       this, and it will flatten nearly straight curves.
       This tool no longer blunts sharp angles. That is better done with
       checkOutlines.

-nf .. Suppress generation of flex commands.

-ns .. Suppress hint substitution. Do this only if you really want the smallest
       possible font file. This will use only one set of hints for the entire
       glyph.

-nb .. Allow the font to have no stem widths or blue values specified.
       Without this option, psautohint will complain and quit.

-o <output font path>
       If not specified, psautohint will write the hinted output to the
       original font path name.

-log . Write output to a log file.

-all . Hint all glyphs, even if the source glyphs is unchanged and the glyph
       has been hinted before.
       Applies only to UFO fonts.

-hf .. Use history file. Will create it if it does not already exist.
       Should not be used with UFO fonts, where another mechanism is employed.

-a ... Hint all glyphs that are in the history file, or are unhinted.
       Has effect only if the history file is being used.

-r ... Re-hint glyphs. Glyphs not in the history file will be hinted even
       if they already have hints. However, glyphs will not be hinted if
       they both have not changed and are in the history file.

-decimal
       Use decimal coordinates, instead of rounding to the nearest integer
       value.

-wd .. Write changed glyphs to default layer instead of '%s'.

psautohint can also apply different sets of alignment zones while hinting a
particular set of glyphs. This is useful for name-keyed fonts, which, unlike
CID fonts, only have one set of global alignment zones and stem widths.
By default, psautohint uses the font's global alignment zones and stem widths
for each glyph. However, if there is a file named "fontinfo" in the same
directory as the input font file, psautohint will check the "fontinfo" file for
definitions of alternate sets of alignment zones, and the matching lists of
glyphs to which they should be applied. To see the format for these entries,
use the option "-hfd". This allows one set of glyphs to be hinted using a
different set of zones and stem widths than other glyphs. This isn't as useful
as having real multiple hint dictionaries in the font, as the final name-keyed
font can only have one set of alignment zones, but it does allow for improved
glyph hints when different sets of glyphs need different zones.

psautohint can maintain a history file, which allows you to avoid hinting
glyphs that have already been auto-hinted or manually hinted. When this is in
use, psautohint will by default hint only those glyphs that are not already
hinted, and also those glyphs which are hinted, but whose outlines have changed
since the last time psautohint was run. psautohint knows whether an outline has
changed by storing the outline in the history file whenever the glyph is
hinted, and then consulting the history file when it is asked to hint the glyph
again. By default, psautohint does not maintain or use the history file, but
this can be turned on with an option.

When used, the history file is named "<PostScriptName>.plist", in the same
location as the parent font file. For each glyph, psautohint stores a
simplified version of the outline coordinates. If this entry is missing for a
glyph and the glyph has hints, then psautohint assumes it was manually hinted,
and will by default not hint it again. If the file is missing, psautohint will
assume that all the glyphs were manually hinted, and you will have to use the
option -a or -r to hint any glyphs.
""" % (ufoFont.kProcessedGlyphsLayerName)

__FDDoc__ = """
By default, psautohint uses the font's global alignment zones and stem widths
when hinting each glyph. However, if there is a file named "fontinfo" in the
same directory as the input font file, psautohint will check the "fontinfo"
file for definitions of sets of alignment zones (a "FDDict"), and the matching
lists of glyphs to which they should be applied. This allows one set of glyphs
to be hinted using a different set of zones and stem widths than other glyphs.
This isn't as useful as having real multiple hint dictionaries in the font, as
the final name-keyed font can only have one set of alignment zones, but it does
allow for improved hinting when different sets of glyphs need different
alignment zones.

If FDDict definitions are used, then the global alignment zones and stem widths
in the source font will be ignored. For any glyphs not covered by an explicit
FDDict definition, psautohint will synthesize a dummy FDDict, where the zones
are set outside of the the font's bounding box, so they will not affect
hinting. This is desirable for glyphs that have no features that need to be
aligned.

If psautohint finds an FDDict named "FinalFont", then it will write that set of
values to the output font. Otherwise, it will merge all the alignment zones and
stem widths in the union of all the FDDict definitions. If this merge fails
because some of the alignment zones and stem widths overlap, then you have to
provide a "FinalFont" FDDict that explicitly defines which stems and zones to
use in the hinted output font.

To use a dictionary of alignment zones and stem widths, you need to define both
the dictionary of alternate values, and the set of glyphs to apply it to. The
FDDict must be defined in the file before the set of glyphs which belong to it.
Both the FDDict and the glyph set define a name; an FDDict is applied to the
glyph set with the same name.

If you run psautohint with the option "-pfd", it will print out the list of
FDDict values for the source font. You can use this text as a starting point
for your FDDict definitions.

You can also run psautohint with the option "-pfdl". This will print the
user-defined FDDict defintions, and the list of glyphs associated with each
FDDict. You can use this to check your values, and to check which glyphs are
assigned to which FDDict. In particular, check the glyph list for the first
FDDict "No Alignment Zones": this list exists because these glyphs did not
match in the search terms for any user-defined FDDict.

The definitions use the following syntax:

begin FDDict <name>
    <key-1> <value-1>
    <key-2> <value-2>
    ...
    <key-n> <value-n>
end FDDict <name>

begin GlyphSet <name>
    <glyphname-1> <glyphname-2> ...
    <glyphname-n>
end GlyphSet <name>

The glyph names may be either a real glyph name, or a regular expression
designed to match several names. An abbreviated regex primer:
^ ..... Matches at the start of the glyph name
$ ..... Matches at the end
[aABb]  Matches any one character in the set a, A, b, B
[A-Z] . Matches any one character in the set comprising the range from A-Z
[abA-Z] Matches any one character in the set comprising set set of a, b,
          and the characters in the range from A-Z
. ..... Matches any single character
+ ..... Maches whatever preceded it one or more times
* ..... Matches whatever preceded it none or more times.
\ ..... An escape character that includes the following character without
          the second one being interpreted as a regex special character

Examples:
^[A-Z]$    Matches names with one character in the range from A-Z.
^[A-Z].+   Matches any name where the first character is in the range A-Z,
             and it is followed by one or more characters.
[A-Z].+    Matches any name with a character that is in the range A-Z and
             which is followed by one or more characters.
^[A-Z].*   Matches any name with one or more characters, and
             the first character is in the range A-Z
^.+\.smallcaps   Matches any name that contains ".smallcaps"
^.+\.smallcaps$  Matches any name that ends with ".smallcaps"
^.+\.s[0-24]0$   Matches any name that ends with ".s00",".s10",".s20" or ".s04"


Example FDDict and GlyphSet definitions
***************************************

begin FDDict ST_Smallcaps
    # I like to put the non hint stuff first.
    OrigEmSqUnits 1000
    FlexOK true
    # This gets used as the hint dict name if the font
    # is eventually built as a CID font.
    FontName AachenStd-Bold

    # Alignment zones.
    # The first is a bottom zone, the rest are top zones. See below.
    BaselineOvershoot -20
    BaselineYCoord 0
    CapHeight 900
    CapOvershoot 20
    LcHeight 700
    LcOvershoot 15

    # Stem widths.
    DominantV [236 267]
    DominantH [141 152]
end FDDict ST_Smallcaps


begin FDDict LM_Smallcaps
    OrigEmSqUnits 1000
    FontName AachenStd-Bold
    BaselineOvershoot -25
    BaselineYCoord 0
    CapHeight 950
    CapOvershoot 25
    LcHeight 750
    LcOvershoot 21
    DominantV [236 267]
    DominantH [141 152]
    FlexOK true
end FDDict LM_Smallcaps


begin GlyphSet LM_Smallcaps
    [Ll]\S+\.smallcap  [Mm]\S+\.smallcap
end GlyphSet LM_Smallcaps


begin GlyphSet ST_Smallcaps
    [Tt]\S+\.smallcap  [Ss]\S+\.smallcap
end GlyphSet ST_Smallcaps

***************************************

Note that whitespace must exist between keywords and values, but is otherwise
ignored. "#" is a comment character; any occurrence of "#" and all following
text on a line is skipped. GlyphSet and FDDict definitions may be intermixed,
as long as any FDDict is defined before the GlyphSet which refers to it.

You must provide at least two BlueValue pairs (the 'BaselineYCoord' bottom zone
and any top zone), and you must provide the DominantH and DominantV keywords.
All other keywords are optional.

The full set of recognized FDDict keywords are:

BlueValue pairs:
    # BaselineOvershoot is a bottom zone, the rest are top zones.
    BaselineYCoord
    BaselineOvershoot

    CapHeight
    CapOvershoot

    LcHeight
    LcOvershoot

    AscenderHeight
    AscenderOvershoot

    FigHeight
    FigOvershoot

    Height5
    Height5Overshoot

    Height6
    Height6Overshoot

OtherBlues pairs:
    # These
    Baseline5Overshoot
    Baseline5

    Baseline6Overshoot
    Baseline6

    SuperiorOvershoot
    SuperiorBaseline

    OrdinalOvershoot
    OrdinalBaseline

    DescenderOvershoot
    DescenderHeight

For zones which capture the bottom of a feature in the glyph, (BaselineYCoord
and all the OtherBlues), the value specifies the top of the zone, and the
"Overshoot" is a negative value which specifes the offset to the bottom of
the zone, e.g.
    BaselineYCoord 0
    BaselineOvershoot -12

For zones which capture the top of a feature in the glyph, (the rest of the
BlueValue zones), the value specifies the bottom of the zone, and the
"Overshoot" is a positive value which specifes the offset to the top of the
zone, e.g.
    Height6 800
    Height6Overshoot 20


Note also that there is no implied sequential order of values. Height6 may have
a value less than or equal to CapHeight.

The values for keywords in one FontDict definiton are completely independent
of the values used in another FontDict. There is no inheritance from one
definition to the next.

All FontDicts must specify at least the BaselineYCoord and one top zone.

Miscellaneous values:
  FontName ..... PostScript font name.
                 Only used by makeotf when building a CID font.
  OrigEmSqUnits  Single value: size of em-square.
                 Only used by makeotf when building a CID font.
  LanguageGroup  0 or 1. Specifies whether counter hints for ideographic glyphs
                 should be applied.
                 Only used by makeotf when building a CID font.
  DominantV .... List of dominant vertical stems, in the form
                 [<stem-value-1> <stem-value-2> ...]
  DominantH .... List of dominant horizontal stems, in the form
                 [<stem-value-1> <stem-value-2> ...]
  FlexOK ....... true or false.
  VCounterChars  List of characters to which counter hints may be applied,
                 in the form [<glyph-name-1> <glyph-name-2> ...]
  HCounterChars  List of characters to which counter hints may be applied,
                 in the form [<glyph-name-1> <glyph-name-2> ...]

Note for cognoscenti: the psautohint program code ignores StdHW and StdVW
entries if DominantV and DominantH entries are present, so it omits writing the
Std[HV]W keywords to fontinfo file. Also, psautohint will add any non-duplicate
stem width values for StemSnap[HV] to the Dominant[HV] stem width list, but the
StemSnap[HV] entries are not necessary if the full list of stem widths are
supplied as values for the Dominant[HV] keywords, hence it also writes the full
stem list for the Dominant[HV] keywords, and does not write the StemSnap[HV]
keywords, to the fontinfo file. This is technically not right, as Dominant[HV]
array is supposed to hold only two values, but the psautohint program does not
care, and it can write fewer entries this way.
"""


class OptionParseError(Exception):
    pass


def expandNames(glyphName, nameAliases):
    glyphRange = glyphName.split("-")
    if len(glyphRange) > 1:
        g1 = expandNames(glyphRange[0], nameAliases)
        g2 = expandNames(glyphRange[1], nameAliases)
        glyphName = "%s-%s" % (g1, g2)

    elif glyphName[0] == "/":
        glyphName = "cid" + glyphName[1:].zfill(5)
        if glyphName == "cid00000":
            glyphName = ".notdef"
            nameAliases[glyphName] = "cid00000"

    elif glyphName.startswith("cid") and (len(glyphName) < 8):
        glyphName = "cid" + glyphName[3:].zfill(5)
        if glyphName == "cid00000":
            glyphName = ".notdef"
            nameAliases[glyphName] = "cid00000"

    return glyphName


def parseGlyphListArg(glyphString, nameAliases):
    glyphString = re.sub(r"[ \t\r\n,]+", ",", glyphString)
    glyphList = glyphString.split(",")
    glyphList = [expandNames(n, nameAliases) for n in glyphList]
    glyphList = filter(None, glyphList)
    return glyphList


def parseCounterHintData(path):
    hCounterGlyphList = []
    vCounterGlyphList = []
    with open(path, "rt") as gf:
        data = gf.read()
    lines = re.findall(r"([^\r\n]+)", data)
    # strip blank and comment lines
    lines = filter(lambda line: re.sub(r"#.+", "", line), lines)
    lines = filter(lambda line: line.strip(), lines)
    for line in lines:
        fields = line.split()
        if (len(fields) != 2) or (fields[0] not in ["V", "v", "H", "h"]):
            print("\tError: could not process counter hint line '%s' in file "
                  "%s. Doesn't look like V or H followed by a tab or space, "
                  "and then a glyph name." % (line, path))
        elif fields[0] in ["V", "v"]:
            vCounterGlyphList.append(fields[1])
        else:
            hCounterGlyphList.append(fields[1])
    return hCounterGlyphList, vCounterGlyphList


def checkFontinfoFile(options):
    """
    Check if there is a makeotf fontinfo file in the input font directory.
    If so, get any Vcounter or HCouunter glyphs from it.
    """
    srcFontInfo = os.path.dirname(options.inputPaths[0])
    srcFontInfo = os.path.join(srcFontInfo, "fontinfo")
    if os.path.exists(srcFontInfo):
        with open(srcFontInfo, "rU") as fi:
            data = fi.read()
        data = re.sub(r"#[^\r\n]+", "", data)
        counterGlyphLists = re.findall(
            r"([VH])CounterChars\s+\(\s*([^\)\r\n]+)\)", data)
        for entry in counterGlyphLists:
            glyphList = entry[1].split()
            if glyphList:
                if entry[0] == "V":
                    options.vCounterGlyphs.extend(glyphList)
                else:
                    options.hCounterGlyphs.extend(glyphList)

        if options.vCounterGlyphs or options.hCounterGlyphs:
            options.counterHintFile = srcFontInfo


def getOptions(args):
    options = autohint.ACOptions()
    i = 0
    numOptions = len(args)
    while i < numOptions:
        arg = args[i]
        if options.inputPaths and arg[0] == "-":
            raise OptionParseError(
                "Option Error: All options must preceed the input font path "
                "<%s>." % arg)

        if arg == "-h":
            print(__help__)
            print("Lib version:", _psautohint.version)
            return
        elif arg == "-u":
            print(__usage__)
            print("Lib version:", _psautohint.version)
            return
        elif arg == "-hfd":
            print(__FDDoc__)
            return
        elif arg == "-pfd":
            options.printDefaultFDDict = True
        elif arg == "-pfdl":
            options.printFDDictList = True
        elif arg == "-hf":
            options.usePlistFile = True
        elif arg == "-a":
            options.hintAll = True
        elif arg == "-all":
            options.hintAll = True
        elif arg == "-r":
            options.rehint = True
        elif arg == "-q":
            options.verbose = False
        elif arg == "-qq":
            options.quiet = True
            options.verbose = False
        elif arg == "-c":
            options.allowChanges = True
        elif arg == "-nf":
            options.noFlex = True
        elif arg == "-ns":
            options.noHintSub = True
        elif arg == "-nb":
            options.allow_no_blues = True
        elif arg in ["-xg", "-g"]:
            if arg == "-xg":
                options.excludeGlyphList = True
            i += 1
            glyphString = args[i]
            if glyphString[0] == "-":
                raise OptionParseError(
                    "Option Error: it looks like the first item in the glyph "
                    "list following '-g' is another option.")
            options.glyphList += parseGlyphListArg(glyphString,
                                                   options.nameAliases)
        elif arg in ["-xgf", "-gf"]:
            if arg == "-xgf":
                options.excludeGlyphList = True
            i += 1
            filePath = args[i]
            if filePath[0] == "-":
                raise OptionParseError(
                    "Option Error: it looks like the the glyph list file "
                    "following '-gf' is another option.")
            try:
                with open(filePath, "rt") as gf:
                    glyphString = gf.read()
            except (IOError, OSError):
                raise OptionParseError(
                    "Option Error: could not open glyph list file <%s>." %
                    filePath)
            options.glyphList += parseGlyphListArg(glyphString,
                                                   options.nameAliases)
        elif arg == "-cf":
            i += 1
            filePath = args[i]
            if filePath[0] == "-":
                raise OptionParseError(
                    "Option Error: it looks like the the counter hint glyph "
                    "list file following '-cf' is another option.")
            try:
                options.counterHintFile = filePath
                (options.hCounterGlyphs,
                 options.vCounterGlyphs) = parseCounterHintData(filePath)
            except (IOError, OSError):
                raise OptionParseError(
                    "Option Error: could not open counter hint glyph list "
                    "file <%s>." % filePath)
        elif arg == "-logOnly":
            options.logOnly = True
        elif arg == "-log":
            i += 1
            options.logFile = open(args[i], "wt")
        elif arg == "-o":
            i += 1
            options.outputPath = args[i]
        elif arg == "-d":
            options.debug = True
        elif arg in ["-decimal", "-dec"]:
            options.allowDecimalCoords = True
        elif arg == "-wd":
            options.writeToDefaultLayer = True
        elif arg[0] == "-":
            raise OptionParseError("Option Error: Unknown option <%s>." % arg)
        else:
            options.inputPaths.append(arg)
        i += 1

    if not options.inputPaths:
        raise OptionParseError(
            "Option Error: You must provide a font path(s).")

    if not all(os.path.exists(p) for p in options.inputPaths):
        raise OptionParseError(
            "Option Error: The input font(s) %s' do not exist." %
            " ".join(options.inputPaths))

    if len(options.inputPaths) >=2 and options.outputPath:
        raise OptionParseError(
            "Option Error: Output path is not supported with multiple "
            "input fonts.")

    # Might be a UFO font.
    # Auto completion in some shells adds a dir separator,
    # which then causes problems with os.path.dirname().
    options.inputPaths = [p.rstrip(os.sep) for p in options.inputPaths]

    checkFontinfoFile(options)

    if options.logOnly:
        options.verbose = True
        options.hintAll = True

    return options


def main(args=None):
    if args is None:
        args = sys.argv[1:]
    try:
        options = getOptions(args)
        if options is None:
            # Happens when one of the help arguments is given.
            return
    except OptionParseError as e:
        autohint.logMsg(e)
        return 1

    # verify that all files exist.
    try:
        autohint.hintFiles(options)
    except (autohint.ACFontError, autohint.ACHintError,
            ufoFont.UFOParseError) as e:
        autohint.logMsg("\t%s" % e)
        return 1

    if options.logFile:
        options.logFile.close()


if __name__ == '__main__':
    sys.exit(main())
