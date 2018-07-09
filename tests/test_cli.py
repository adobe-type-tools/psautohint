from __future__ import print_function, division, absolute_import

import glob
from os.path import basename
import py.path
import pytest

from psautohint.autohint import ACFontError
from psautohint.__main__ import main as psautohint

from . import DATA_DIR


UFO_FONTS = glob.glob("%s/*/*/font.ufo" % DATA_DIR)
OTF_FONTS = glob.glob("%s/*/*/font.otf" % DATA_DIR)
FONTS = (UFO_FONTS[0], OTF_FONTS[0])


@pytest.mark.parametrize("path", FONTS)
def test_basic(path, tmpdir):
    # the input font is modified in-place, make a temp copy first
    pathSrc = py.path.local(path)
    pathDst = tmpdir / pathSrc.basename
    pathSrc.copy(pathDst)

    psautohint([str(pathDst)])


@pytest.mark.parametrize("path", FONTS)
def test_outpath(path, tmpdir):
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out])


@pytest.mark.parametrize("path", glob.glob("%s/*/*/font.pfa" % DATA_DIR))
def test_pfa(path, tmpdir):
    out = str(tmpdir / basename(path)) + ".out"

    with pytest.raises(ACFontError):
        psautohint([path, '-o', out])


@pytest.mark.parametrize("path", FONTS)
def test_glyph_list(path, tmpdir):
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, '-g', 'a,b,c'])


@pytest.mark.parametrize("path", FONTS)
def test_filter_glyph_list(path, tmpdir):
    """Test that we don't fail if some glyphs in the list do not exist."""
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, '-g', 'FOO,BAR,a'])


@pytest.mark.parametrize("path", FONTS)
def test_missing_glyph_list(path, tmpdir):
    """Test that we raise if all glyph in the list do not exist."""
    out = str(tmpdir / basename(path)) + ".out"

    with pytest.raises(ACFontError):
        psautohint([path, '-o', out, '-g', 'FOO,BAR'])
