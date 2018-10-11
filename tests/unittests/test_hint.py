import glob
import os
import pytest

from psautohint.autohint import ACOptions, openFile
from psautohint import hint_bez_glyph

from . import DATA_DIR


class BezFontData:
    def __init__(self, path):
        self._path = path
        self._info = None
        self._glyphs = {}

    class FontInfo:
        def __init__(self, info):
            self._info = info

        def getFontInfo(self):
            return self._info

    def getFontInfo(self, allow_no_blues, noFlex, vCounterGlyphs,
                    hCounterGlyphs):
        if self._info is None:
            with open(os.path.join(self._path, "fontinfo")) as fp:
                self._info = self.FontInfo(fp.read())
        return self._info

    def convertToBez(self, glyphName, read_hints, doAll=False):
        if glyphName not in self._glyphs:
            with open(os.path.join(self._path, glyphName + ".bez")) as fp:
                self._glyphs[glyphName] = fp.read()
        return (self._glyphs[glyphName], 0)

    def getGlyphList(self):
        files = glob.glob(self._path + "/*.bez")
        files += glob.glob(self._path + "/.*.bez")
        return [os.path.splitext(os.path.basename(f))[0] for f in files]


def open_font(path):
    if not path.endswith(".bez"):
        font = openFile(path, ACOptions())
    else:
        font = BezFontData(path)

    return font


def get_font_info(font, path):
    info = font.getFontInfo(False, False, [], [])
    # Sort to normalize the order.
    info = sorted(info.getFontInfo().split("\n"))
    return "\n".join(info)


def normalize_glyph(glyph, name):
    """Dump Bez format normalizer."""

    # strip comments
    hhints = []
    vhints = []
    path = []
    for line in glyph.split("\n"):
        line = line.split("%")[0].strip()
        if not line:
            pass

        elif line.endswith("rb"):
            hhints.append(line)
        elif line.endswith("ry"):
            vhints.append(line)
        else:
            path.append(line)
    hhints.sort()
    vhints.sort()
    return "\n".join(["% " + name] + hhints + vhints + path)


def get_glyph(font, name):
    glyph = font.convertToBez(name, True, True)[0]
    return normalize_glyph(glyph, name)


@pytest.mark.parametrize("unhinted,hinted", zip(
    glob.glob("%s/unhinted/*.[uo][ft][of]" % DATA_DIR),
    glob.glob("%s/hinted/*.[uo][ft][of]" % DATA_DIR),
))
@pytest.mark.parametrize("use_autohintexe", [False, True])
def test_bez(unhinted, hinted, use_autohintexe):
    unhinted_base = os.path.splitext(unhinted)[0]
    hinted_base = os.path.splitext(hinted)[0]

    bez_font = open_font(unhinted_base + ".bez")
    otf_font = open_font(unhinted_base + ".otf")
    ufo_font = open_font(unhinted_base + ".ufo")
    hinted_bez_font = open_font(hinted_base + ".bez")
    hinted_otf_font = open_font(hinted_base + ".otf")

    bez_info = get_font_info(bez_font, unhinted_base + ".bez")
    otf_info = get_font_info(otf_font, unhinted_base + ".otf")
    ufo_info = get_font_info(ufo_font, unhinted_base + ".ufo")
    assert otf_info == bez_info == ufo_info

    names = sorted(otf_font.getGlyphList())
    assert all(sorted(f.getGlyphList()) == names for f in [bez_font,
                                                           otf_font,
                                                           ufo_font,
                                                           hinted_bez_font,
                                                           hinted_otf_font])
    for name in names:
        if name == ".notdef":
            continue
        bez_glyph = get_glyph(bez_font, name)
        otf_glyph = get_glyph(otf_font, name)
        ufo_glyph = get_glyph(ufo_font, name)
        assert otf_glyph == bez_glyph == ufo_glyph

        hinted_bez_glyph = get_glyph(hinted_bez_font, name)
        hinted_otf_glyph = get_glyph(hinted_otf_font, name)
        assert hinted_otf_glyph == hinted_bez_glyph

        result = hint_bez_glyph(bez_info, bez_glyph,
                                use_autohintexe=use_autohintexe)
        assert normalize_glyph(result, name) == hinted_bez_glyph
