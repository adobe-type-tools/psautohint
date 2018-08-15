from __future__ import print_function, division, absolute_import

import glob
from os.path import basename
import pytest

from psautohint import FontParseError
from psautohint.__main__ import main as psautohint

from . import make_temp_copy, DATA_DIR


# font.otf, font.cff, font.ufo
FONTS = glob.glob("%s/dummy/font.[ocu][tf][fo]" % DATA_DIR)


@pytest.mark.parametrize("path", FONTS)
def test_basic(path, tmpdir):
    # the input font is modified in-place, make a temp copy first
    psautohint([make_temp_copy(tmpdir, path)])


@pytest.mark.parametrize("path", FONTS)
def test_outpath(path, tmpdir):
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out])


def test_multi_outpath(tmpdir):
    """Test handling multiple output paths."""
    paths = sorted(glob.glob("%s/dummy/mm0/*.ufo" % DATA_DIR))
    # the reference font is modified in-place, make a temp copy first
    reference = make_temp_copy(tmpdir, paths[0])
    inpaths = paths[1:]
    outpaths = [str(tmpdir / basename(p)) for p in inpaths]

    psautohint(inpaths + ['-o'] + outpaths + ['-r', reference])


def test_multi_outpath_unequal(tmpdir):
    """Test that we exit if output paths don't match number of input paths."""
    paths = sorted(glob.glob("%s/dummy/mm0/*.ufo" % DATA_DIR))
    # the reference font is modified in-place, make a temp copy first
    reference = make_temp_copy(tmpdir, paths[0])
    inpaths = paths[1:]
    outpaths = [str(tmpdir / basename(p)) for p in inpaths][1:]

    with pytest.raises(SystemExit):
        psautohint(inpaths + ['-o'] + outpaths + ['-r', reference])


def test_multi_different_formats(tmpdir):
    """Test that we exit if input paths are of different formats."""
    base = "%s/dummy/mm0" % DATA_DIR
    paths = sorted(glob.glob(base + "/*.ufo"))
    otfs = sorted(glob.glob(base + "/*.otf"))
    # the reference font is modified in-place, make a temp copy first
    reference = make_temp_copy(tmpdir, otfs[0])
    inpaths = paths[1:]
    outpaths = [str(tmpdir / basename(p)) for p in inpaths]

    with pytest.raises(SystemExit):
        psautohint(inpaths + ['-o'] + outpaths + ['-r', reference])


def test_multi_reference_is_input(tmpdir):
    """Test that we exit if reference font is also one of the input paths."""
    paths = sorted(glob.glob("%s/dummy/mm0/*.ufo" % DATA_DIR))
    # the reference font is modified in-place, make a temp copy first
    reference = make_temp_copy(tmpdir, paths[0])
    inpaths = [reference] + paths[1:]
    outpaths = [str(tmpdir / basename(p)) for p in inpaths]

    with pytest.raises(SystemExit):
        psautohint(inpaths + ['-o'] + outpaths + ['-r', reference])


def test_multi_reference_is_duplicated(tmpdir):
    """Test that we exit if one of the input paths is duplicated."""
    paths = sorted(glob.glob("%s/dummy/mm0/*.ufo" % DATA_DIR))
    # the reference font is modified in-place, make a temp copy first
    reference = make_temp_copy(tmpdir, paths[0])
    inpaths = paths[1:] + [paths[1]]
    outpaths = [str(tmpdir / basename(p)) for p in inpaths]

    with pytest.raises(SystemExit):
        psautohint(inpaths + ['-o'] + outpaths + ['-r', reference])


@pytest.mark.parametrize("path", glob.glob("%s/dummy/font.pf[ab]" % DATA_DIR))
def test_type1(path, tmpdir):
    out = str(tmpdir / basename(path)) + ".out"

    with pytest.raises(SystemExit):
        psautohint([path, '-o', out])


@pytest.mark.parametrize("glyphs", [
    'a,b,c',     # Glyph List
    'a-z',       # Glyph range
    'FOO,BAR,a', # Some glyphs in the list do not exist.
])
def test_glyph_list(glyphs, tmpdir):
    path = "%s/dummy/font.ufo" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, '-g', glyphs])


@pytest.mark.parametrize("glyphs", [
    '/0,/1,/2',
    '/0-/10',
    'cid0,cid1,cid2',
    'cid0-cid10',
])
def test_cid_glyph_list(glyphs, tmpdir):
    path = "%s/source-code-pro/CID/font.otf" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, '-g', glyphs])


@pytest.mark.parametrize("glyphs", [
    'a,b,c',
    'a-z',
])
def test_exclude_glyph_list(glyphs, tmpdir):
    path = "%s/dummy/font.ufo" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, '-x', glyphs])


@pytest.mark.parametrize("glyphs", [
    'FOO,BAR',
    'FOO-BAR',
    'FOO-a',
    'a-BAR',
])
def test_missing_glyph_list(glyphs, tmpdir):
    path = "%s/dummy/font.ufo" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    with pytest.raises(FontParseError):
        psautohint([path, '-o', out, '-g', glyphs])


@pytest.mark.parametrize("path", ["%s/dummy/fontinfo" % DATA_DIR, DATA_DIR])
def test_unsupported_format(path):
    with pytest.raises(SystemExit):
        psautohint([path])


def test_missing_cff_table(tmpdir):
    path = "%s/dummy/nocff.otf" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    with pytest.raises(FontParseError):
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
def test_doc_option(option):
    with pytest.raises(SystemExit) as e:
        psautohint([option])
    assert e.type == SystemExit
    assert e.value.code == 0


def test_no_fddict(tmpdir):
    path = "%s/dummy/mm0/font0.ufo" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"

    psautohint([path, '-o', out, "--print-list-fddict"])


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
