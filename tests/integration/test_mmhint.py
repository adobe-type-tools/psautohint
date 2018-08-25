from __future__ import print_function, division, absolute_import

import glob
import pytest

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


@pytest.mark.parametrize("base", glob.glob("%s/*/*Masters" % DATA_DIR))
def test_mmufo(base, tmpdir):
    paths = sorted(glob.glob(base + "/*.ufo"))
    # the reference font is modified in-place, make a temp copy first
    reference = make_temp_copy(tmpdir, paths[0])
    inpaths = paths[1:]
    outpaths = [str(tmpdir / p) for p in inpaths]

    options = Options(reference, inpaths, outpaths)
    hintFiles(options)

    for inpath, outpath in zip(inpaths, outpaths):
        assert differ([inpath, outpath])


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
