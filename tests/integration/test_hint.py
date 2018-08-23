from __future__ import print_function, division, absolute_import

import glob
from os.path import basename
import pytest

from fontTools.misc.xmlWriter import XMLWriter
from fontTools.cffLib import CFFFontSet
from fontTools.ttLib import TTFont
from psautohint.autohint import ACOptions, hintFiles
from psautohint import FontParseError

from .differ import main as differ
from . import DATA_DIR


class Options(ACOptions):
    def __init__(self, inpath, outpath):
        super(Options, self).__init__()
        self.inputPaths = [inpath]
        self.outputPaths = [outpath]
        self.hintAll = True
        self.verbose = False
        self.read_hints = True


@pytest.mark.parametrize("ufo", glob.glob("%s/*/*/font.ufo" % DATA_DIR))
def test_ufo(ufo, tmpdir):
    out = str(tmpdir / basename(ufo))
    options = Options(ufo, out)
    hintFiles(options)

    assert differ([ufo, out])


@pytest.mark.parametrize("otf", glob.glob("%s/*/*/font.otf" % DATA_DIR))
def test_otf(otf, tmpdir):
    out = str(tmpdir / basename(otf)) + ".out"
    options = Options(otf, out)
    hintFiles(options)

    for path in (otf, out):
        font = TTFont(path)
        assert "CFF " in font
        writer = XMLWriter(str(tmpdir / basename(path)) + ".xml")
        font["CFF "].toXML(writer, font)
        writer.close()

    assert differ([str(tmpdir / basename(otf)) + ".xml",
                   str(tmpdir / basename(out)) + ".xml"])


@pytest.mark.parametrize("cff", glob.glob("%s/dummy/*.cff" % DATA_DIR))
def test_cff(cff, tmpdir):
    out = str(tmpdir / basename(cff)) + ".out"
    options = Options(cff, out)
    hintFiles(options)

    for path in (cff, out):
        font = CFFFontSet()
        writer = XMLWriter(str(tmpdir / basename(path)) + ".xml")
        with open(path, "rb") as fp:
            font.decompile(fp, None)
            font.toXML(writer)
        writer.close()

    assert differ([str(tmpdir / basename(cff)) + ".xml",
                   str(tmpdir / basename(out)) + ".xml"])


@pytest.mark.parametrize("path", glob.glob("%s/dummy/font.p*" % DATA_DIR))
def test_type1(path, tmpdir):
    out = str(tmpdir / basename(path)) + ".out"
    options = Options(path, out)
    with pytest.raises(NotImplementedError):
        hintFiles(options)


def test_unsupported_format(tmpdir):
    path = "%s/dummy/fontinfo" % DATA_DIR
    out = str(tmpdir / basename(path))
    options = Options(path, out)

    with pytest.raises(FontParseError):
        hintFiles(options)


def test_missing_cff_table(tmpdir):
    path = "%s/dummy/nocff.otf" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"
    options = Options(path, out)

    with pytest.raises(FontParseError):
        hintFiles(options)


def test_ufo_write_to_default_layer(tmpdir):
    path = "%s/dummy/defaultlayer.ufo" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"
    options = Options(path, out)
    options.writeToDefaultLayer = True
    hintFiles(options)

    assert differ([path, out])


@pytest.mark.parametrize("path",
                         glob.glob("%s/dummy/*_metainfo.ufo" % DATA_DIR))
def test_ufo_bad(path, tmpdir):
    out = str(tmpdir / basename(path)) + ".out"
    options = Options(path, out)

    with pytest.raises(FontParseError):
        hintFiles(options)


@pytest.mark.parametrize("path", glob.glob("%s/dummy/bad_*.p*" % DATA_DIR))
def test_type1_bad(path, tmpdir):
    out = str(tmpdir / basename(path)) + ".out"
    options = Options(path, out)

    with pytest.raises(FontParseError):
        hintFiles(options)


def test_counter_glyphs(tmpdir):
    path = "%s/dummy/font.ufo" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"
    options = Options(path, out)
    options.vCounterGlyphs = ["m", "M", "T"]
    hintFiles(options)


def test_seac_op(tmpdir, caplog):
    path = "%s/dummy/seac.otf" % DATA_DIR
    out = str(tmpdir / basename(path)) + ".out"
    options = Options(path, out)

    hintFiles(options)

    msgs = [r.getMessage() for r in caplog.records]
    assert "Skipping Aacute: can't process SEAC composite glyphs." in msgs


@pytest.mark.parametrize("path",
                         glob.glob("%s/dummy/bad_privatedict_*" % DATA_DIR))
def test_bad_privatedict(path, tmpdir):
    out = str(tmpdir / basename(path)) + ".out"
    options = Options(path, out)

    with pytest.raises(FontParseError):
        hintFiles(options)


@pytest.mark.parametrize("path",
                         glob.glob("%s/dummy/bad_privatedict_*" % DATA_DIR))
def test_bad_privatedict_accept(path, tmpdir):
    """Same as above test, but PrivateDict is accepted because of
       `allow_no_blues` option."""
    out = str(tmpdir / basename(path)) + ".out"
    options = Options(path, out)
    options.allow_no_blues = True

    hintFiles(options)


@pytest.mark.parametrize("path",
                         glob.glob("%s/dummy/ok_privatedict_*" % DATA_DIR))
def test_ok_privatedict_accept(path, tmpdir, caplog):
    out = str(tmpdir / basename(path)) + ".out"
    options = Options(path, out)

    hintFiles(options)

    msg = "There is no value or 0 value for Dominant"
    assert any(r.getMessage().startswith(msg) for r in caplog.records)
