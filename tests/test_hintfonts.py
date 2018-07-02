from __future__ import print_function, division, absolute_import

import glob
import pytest

from fontTools.misc.xmlWriter import XMLWriter
from fontTools.ttLib import TTFont
from psautohint.autohint import ACOptions, hintFiles

from .differ import main as differ


class Options(ACOptions):
    def __init__(self, inpath, outpath):
        super(Options, self).__init__()
        self.inputPaths = [inpath]
        self.outputPath = outpath
        self.hintAll = True
        self.verbose = False


@pytest.mark.parametrize("ufo", glob.glob("data/*/*/font.ufo"))
def test_ufo(ufo):
    out = ufo + ".out"
    options = Options(ufo, out)
    hintFiles(options)

    assert differ([ufo, out])


@pytest.mark.parametrize("otf", glob.glob("data/*/*/font.otf"))
def test_otf(otf):
    out = otf + ".out"
    options = Options(otf, out)
    hintFiles(options)

    for path in (otf, out):
        font = TTFont(path)
        assert "CFF " in font
        writer = XMLWriter(path + ".xml")
        font["CFF "].toXML(writer, font)
        del writer
        del font

    assert differ([otf + ".xml", out + ".xml"])
