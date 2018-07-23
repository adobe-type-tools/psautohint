from __future__ import print_function, division, absolute_import

import glob
import pytest
import py.path

from psautohint.autohint import ACOptions, hintFiles

from .differ import main as differ
from . import DATA_DIR


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
    referenceSrc = py.path.local(paths[0])
    referenceDst = tmpdir / referenceSrc.basename
    referenceSrc.copy(referenceDst)
    reference = str(referenceDst)
    inpaths = paths[1:]
    outpaths = [str(tmpdir / p) for p in inpaths]

    options = Options(reference, inpaths, outpaths)
    hintFiles(options)

    for inpath, outpath in zip(inpaths, outpaths):
        assert differ([inpath, outpath])
