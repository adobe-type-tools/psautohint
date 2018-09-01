from __future__ import print_function, division, absolute_import

from psautohint.ufoFont import UFOFontData

from . import DATA_DIR


def test_incomplete_glyphorder():
    path = "%s/dummy/incomplete_glyphorder.ufo" % DATA_DIR
    font = UFOFontData(path, False, True)

    assert len(font.getGlyphList()) == 95
    assert "ampersand" in font.getGlyphList()
