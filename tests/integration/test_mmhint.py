import glob
from os.path import basename
import pytest

from fontTools.misc.xmlWriter import XMLWriter
from fontTools.ttLib import TTFont
from psautohint.autohint import ACOptions, ACHintError, hintFiles

from .differ import main as differ
from . import make_temp_copy, DATA_DIR


class Options(ACOptions):

    def __init__(self, reference, inpaths, outpaths):
        super(Options, self).__init__()
        self.inputPaths = inpaths
        self.outputPaths = outpaths
        self.reference_font = reference
        self.hintAll = True
        self.verbose = False
        self.verbose = False


@pytest.mark.parametrize("base", glob.glob("%s/*/Masters" % DATA_DIR))
def test_mmufo(base, tmpdir):
    paths = sorted(glob.glob(base + "/*.ufo"))
    # the reference font is modified in-place, make a temp copy first
    reference = make_temp_copy(tmpdir, paths[0])
    inpaths = paths[1:]
    outpaths = [str(tmpdir / basename(p)) + ".out" for p in inpaths]

    options = Options(reference, inpaths, outpaths)
    hintFiles(options)

    for inpath, outpath in zip(inpaths, outpaths):
        assert differ([inpath, outpath])


@pytest.mark.parametrize("base", glob.glob("%s/*/OTFMasters" % DATA_DIR))
def test_mmotf(base, tmpdir):
    paths = sorted(glob.glob(base + "/*.otf"))
    # the reference font is modified in-place, make a temp copy first
    reference = make_temp_copy(tmpdir, paths[0])
    inpaths = paths[1:]
    outpaths = [str(tmpdir / basename(p)) + ".out" for p in inpaths]

    options = Options(reference, inpaths, outpaths)
    hintFiles(options)

    refs = [p + ".ref" for p in paths]
    for ref, out in zip(refs, [reference] + outpaths):
        for path in (ref, out):
            font = TTFont(path)
            assert "CFF " in font
            writer = XMLWriter(str(tmpdir / basename(path)) + ".xml")
            font["CFF "].toXML(writer, font)
            writer.close()

        assert differ([str(tmpdir / basename(ref)) + ".xml",
                       str(tmpdir / basename(out)) + ".xml"])


@pytest.mark.parametrize("otf", glob.glob("%s/vf_tests/*.otf" % DATA_DIR))
def test_vfotf(otf, tmpdir):
    out = str(tmpdir / basename(otf)) + ".out"
    options = Options(None, [otf], [out])
    options.allow_no_blues = True
    hintFiles(options)

    for path in (otf, out):
        font = TTFont(path)
        assert "CFF2" in font
        writer = XMLWriter(str(tmpdir / basename(path)) + ".xml")
        font["CFF2"].toXML(writer, font)
        writer.close()
    assert differ([str(tmpdir / basename(otf)) + ".xml",
                   str(tmpdir / basename(out)) + ".xml"])


def test_incompatible_masters(tmpdir):
    base = "%s/source-serif-pro/" % DATA_DIR
    paths = [base + "Light/font.ufo", base + "Black/font.ufo"]
    # the reference font is modified in-place, make a temp copy first
    reference = make_temp_copy(tmpdir, paths[0])
    inpaths = paths[1:]
    outpaths = [str(tmpdir / p) for p in inpaths]

    options = Options(reference, inpaths, outpaths)
    with pytest.raises(ACHintError):
        hintFiles(options)


def test_sparse_mmotf(tmpdir):
    base = "%s/sparse_masters" % DATA_DIR
    paths = sorted(glob.glob(base + "/*.otf"))
    # the reference font is modified in-place, make a temp copy first
    # MasterSet_Kanji-w0.00.otf has to be the reference font.
    reference = make_temp_copy(tmpdir, paths[0])
    inpaths = paths[1:]
    outpaths = [str(tmpdir / basename(p)) + ".out" for p in inpaths]

    options = Options(reference, inpaths, outpaths)
    options.allow_no_blues = True
    hintFiles(options)

    refs = [p + ".ref" for p in paths]
    for ref, out in zip(refs, [reference] + outpaths):
        for path in (ref, out):
            font = TTFont(path)
            assert "CFF " in font
            writer = XMLWriter(str(tmpdir / basename(path)) + ".xml")
            font["CFF "].toXML(writer, font)
            writer.close()

        assert differ([str(tmpdir / basename(ref)) + ".xml",
                       str(tmpdir / basename(out)) + ".xml"])
