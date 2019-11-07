from os.path import basename
import pytest

from psautohint.autohint import ACOptions, hintFiles

from .differ import main as differ
from . import DATA_DIR


class Options(ACOptions):
    def __init__(self, inpath, outpath, zones, stems, all_stems):
        super(Options, self).__init__()
        self.inputPaths = [inpath]
        self.outputPaths = [outpath]
        self.logOnly = True
        self.hintAll = True
        self.verbose = False
        self.report_zones = zones
        self.report_stems = stems
        self.report_all_stems = all_stems


@pytest.mark.parametrize("zones,stems,all_stems", [
    pytest.param(True, False, False, id="report_zones"),
    pytest.param(False, True, False, id="report_stems"),
    pytest.param(False, True, True, id="report_stems,all_stems"),
])
def test_otf(zones, stems, all_stems, tmpdir):
    path = "%s/dummy/font.otf" % DATA_DIR
    out = str(tmpdir / basename(path))
    options = Options(path, out, zones, stems, all_stems)

    hintFiles(options)

    if zones:
        suffixes = ['.top.txt', '.bot.txt']
    else:
        suffixes = ['.hstm.txt', '.vstm.txt']

    for suffix in suffixes:
        exp_suffix = suffix
        if all_stems:
            exp_suffix = '.all' + suffix
        assert differ([path + exp_suffix, out + suffix, '-l', '1'])
