from __future__ import print_function, division, absolute_import

import glob
from os.path import basename
import pytest

from fontTools.misc.xmlWriter import XMLWriter
from fontTools.cffLib import CFFFontSet
from fontTools.ttLib import TTFont
from psautohint.autohint import ACOptions, hintFiles

from .differ import main as differ
from . import DATA_DIR


class Options(ACOptions):
    def __init__(self, inpath, outpath):
        super(Options, self).__init__()
        self.inputPaths = [inpath]
        self.outputPaths = [outpath]
        self.hintAll = True
        self.verbose = False


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


@pytest.mark.parametrize("cff", glob.glob("%s/*/*/font.cff" % DATA_DIR))
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


@pytest.mark.parametrize("path", glob.glob("%s/*/*/font.pf[ab]" % DATA_DIR))
def test_type1(path, tmpdir):
    out = str(tmpdir / basename(path)) + ".out"
    options = Options(path, out)
    with pytest.raises(NotImplementedError):
        hintFiles(options)
