import glob
import pytest

from psautohint.autohint import ACOptions, hintFiles

from .differ import main as differ


class Options(ACOptions):
    def __init__(self, reference, inpaths, outpaths):
        super(Options, self).__init__()
        self.inputPaths = inpaths
        self.outputPaths = outpaths
        self.reference_font = reference
        self.hintAll = True
        self.verbose = False


@pytest.mark.parametrize("base", glob.glob("data/*/*Masters"))
def test_mmufo(base):
    paths = sorted(glob.glob(base + "/*.ufo"))
    reference = paths[0]
    inpaths = paths[1:]
    outpaths = [p + ".out" for p in inpaths]

    options = Options(reference, inpaths, outpaths)
    hintFiles(options)

    for inpath, outpath in zip(inpaths, outpaths):
        assert differ([inpath, outpath])
