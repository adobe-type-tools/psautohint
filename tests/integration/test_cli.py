from __future__ import print_function, division, absolute_import

import glob
from os.path import basename
import pytest

from psautohint.autohint import ACFontError
from psautohint.__main__ import main as psautohint

from . import make_temp_copy, DATA_DIR


# font.otf, font.cff, font.ufo
FONTS = glob.glob("%s/dummy/font.[ocu][tf][fo]" % DATA_DIR)
FONTINFO = glob.glob("%s/*/*/fontinfo" % DATA_DIR)


@pytest.mark.parametrize("path", FONTS)
def test_basic(path, tmpdir):
    # the input font is modified in-place, make a temp copy first
    psautohint([make_temp_copy(tmpdir, path)])


@pytest.mark.parametrize("path", FONTS)
def test_outpath(path, tmpdir):
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out])


def test_outpath_multi(tmpdir):
    """Test handling multiple output paths."""
    base = glob.glob("%s/dummy/mm0" % DATA_DIR)[0]
    paths = sorted(glob.glob(base + "/*.ufo"))
    # the reference font is modified in-place, make a temp copy first
    reference = make_temp_copy(tmpdir, paths[0])
    inpaths = paths[1:]
    outpaths = [str(tmpdir / basename(p)) for p in inpaths]

    psautohint(inpaths + ['-o'] + outpaths + ['-r', reference])


def test_outpath_multi_unequal(tmpdir):
    """Test that we exit if output paths don't match number of input paths."""
    base = glob.glob("%s/dummy/mm0" % DATA_DIR)[0]
    paths = sorted(glob.glob(base + "/*.ufo"))
    # the reference font is modified in-place, make a temp copy first
    reference = make_temp_copy(tmpdir, paths[0])
    inpaths = paths[1:]
    outpaths = [str(tmpdir / basename(p)) for p in inpaths][1:]

    with pytest.raises(SystemExit):
        psautohint(inpaths + ['-o'] + outpaths + ['-r', reference])


@pytest.mark.parametrize("path", glob.glob("%s/dummy/font.pf[ab]" % DATA_DIR))
def test_type1(path, tmpdir):
    out = str(tmpdir / basename(path)) + ".out"

    with pytest.raises(SystemExit):
        psautohint([path, '-o', out])


def test_glyph_list(tmpdir):
    path = "%s/dummy/font.ufo" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, '-g', 'a,b,c'])


def test_glyph_range(tmpdir):
    path = "%s/dummy/font.ufo" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, '-g', 'a-z'])


def test_cid_glyph_list(tmpdir):
    path = "%s/source-code-pro/CID/font.otf" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, '-g', '/0,/1,/2'])


def test_cid_glyph_range(tmpdir):
    path = "%s/source-code-pro/CID/font.otf" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, '-g', '/0-/10'])


def test_cid_prefixed_glyph_list(tmpdir):
    path = "%s/source-code-pro/CID/font.otf" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, '-g', 'cid0,cid1,cid2'])


def test_cid_prefixed_glyph_range(tmpdir):
    path = "%s/source-code-pro/CID/font.otf" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, '-g', 'cid0-cid10'])


def test_exclude_glyph_list(tmpdir):
    path = "%s/dummy/font.ufo" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, '-x', 'a,b,c'])


def test_exclude_glyph_range(tmpdir):
    path = "%s/dummy/font.ufo" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, '-x', 'a-z'])


def test_filter_glyph_list(tmpdir):
    """Test that we don't fail if some glyphs in the list do not exist."""
    path = "%s/dummy/font.ufo" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, '-g', 'FOO,BAR,a'])


def test_missing_glyph_list(tmpdir):
    """Test that we raise if all glyph in the list do not exist."""
    path = "%s/dummy/font.ufo" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    with pytest.raises(ACFontError):
        psautohint([path, '-o', out, '-g', 'FOO,BAR'])


@pytest.mark.parametrize("path", [FONTINFO[0], DATA_DIR])
def test_unsupported_format(path, tmpdir):
    with pytest.raises(SystemExit):
        psautohint([path])


def test_missing_cff_table(tmpdir):
    path = "%s/dummy/nocff.otf" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    with pytest.raises(ACFontError):
        psautohint([path, '-o', out])

@pytest.mark.parametrize("option,argument", [
    ("--exclude-glyphs-file", "glyphs.txt"),
    ("--fontinfo-file", "fontinfo"),
    ("--glyphs-file", "glyphs.txt"),
])
@pytest.mark.parametrize("path", ["font.ufo", "font.otf"])
def test_option(path, option, argument, tmpdir):
    path = "%s/dummy/%s" % (DATA_DIR, path)
    out = str(tmpdir / basename(path)) + ".out"

    argument = "%s/dummy/%s" % (DATA_DIR, argument)

    psautohint([path, '-o', out, option, argument])


@pytest.mark.parametrize("option", [
    "--all",
    "--allow-changes",
    "--decimal",
    "--no-flex",
    "--no-hint-sub",
    "--no-zones-stems",
    "--print-dflt-fddict",
    "--print-list-fddict",
    "--report-only",
    "--verbose",
    "--write-to-default-layer",
    "-vv",
])
@pytest.mark.parametrize("path", ["font.ufo", "font.otf"])
def test_argumentless_option(path, option, tmpdir):
    path = "%s/dummy/%s" % (DATA_DIR, path)
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, option])


@pytest.mark.parametrize("option", [
    "--doc-fddict",
    "--help",
    "--info",
    "--version",
])
def test_doc_option(option, tmpdir):
    with pytest.raises(SystemExit) as e:
        psautohint([option])
    assert e.type == SystemExit
    assert e.value.code == 0


@pytest.mark.parametrize("path", [
    "%s/dummy/font.ufo" % DATA_DIR,
    "%s/dummy/font.otf" % DATA_DIR,
    "%s/dummy/font.cff" % DATA_DIR,
])
def test_overwrite_font(path, tmpdir):
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, '-g', 'a,b,c'])
    psautohint([path, '-o', out, '-g', 'a,b,c'])


def test_invalid_input_path(tmpdir):
    path = str(tmpdir / "foo") + ".otf"
    with pytest.raises(SystemExit):
        psautohint([path])


def test_invalid_save_path(tmpdir):
    path = "%s/dummy/font.otf" % DATA_DIR
    out = str(tmpdir / basename(path) / "foo") + ".out"
    with pytest.raises(SystemExit):
        psautohint([path, '-o', out])
