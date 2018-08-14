import glob
import pytest

from psautohint.fdTools import (mergeFDDicts, parseFontInfoFile, FDDict,
                                FontInfoParseError)

from . import DATA_DIR


def parse(path, glyphs=None):
    with open(path) as fp:
        data = fp.read()
    if glyphs is None:
        glyphs = []
    fddict = FDDict()
    fddict.BlueFuzz = 0
    return parseFontInfoFile([fddict], data, glyphs, 2000, -1000, "Foo")


def test_finalfont():
    path = "%s/fontinfo/finalfont" % DATA_DIR
    _, _, final_dict = parse(path)
    assert final_dict is not None


def test_base_token_value_is_a_list():
    path = "%s/fontinfo/base_token_list" % DATA_DIR
    _, font_dicts, _ = parse(path)
    assert len(font_dicts) > 0


def test_no_dominant_h_or_v(caplog):
    path = "%s/fontinfo/no_dominant_h_v" % DATA_DIR
    _, font_dicts, _ = parse(path)
    assert len(font_dicts) > 0
    msgs = [r.getMessage() for r in caplog.records]
    assert "The FDDict 'UPPERCASE' in fontinfo has no DominantH value" in msgs
    assert "The FDDict 'UPPERCASE' in fontinfo has no DominantV value" in msgs


def test_bluefuzz_and_fontname():
    path = "%s/fontinfo/bluefuzz_fontname" % DATA_DIR
    _, font_dicts, _ = parse(path, ["A", "B", "C", "D"])
    assert len(font_dicts) > 1
    assert font_dicts[1].BlueFuzz == "10"
    assert font_dicts[1].FontName == "Bar"


@pytest.mark.parametrize("path", glob.glob("%s/fontinfo/bad_*" % DATA_DIR))
def test_bad_fontinfo(path):
    with pytest.raises(FontInfoParseError):
        parse(path)


@pytest.mark.parametrize("attributes", [
    {"CapOvershoot": 5, "CapHeight": 5,
     "AscenderOvershoot": 0, "AscenderHeight": 8},
    {"BaselineOvershoot": None},
    {"BaselineOvershoot": 10},
    {"CapOvershoot": 0},
    {"AscenderHeight": -10, "AscenderOvershoot": 0},
    {"AscenderHeight": 0, "AscenderOvershoot": -10},
    {"Baseline5Overshoot": 10, "Baseline5": 0},
])
def test_fddict_bad_zone(attributes):
    fddict = FDDict()
    fddict.BlueFuzz = 0
    fddict.BaselineYCoord = 0
    fddict.BaselineOvershoot = -10

    for key in attributes:
        setattr(fddict, key, attributes[key])

    with pytest.raises(FontInfoParseError):
        fddict.buildBlueLists()


def test_merge_empty_fddicts():
    mergeFDDicts([FDDict(), FDDict()], FDDict())


@pytest.mark.parametrize("stemdict", ["DominantH", "DominantV"])
def test_merge_fddicts_with_stemdicts(stemdict):
    fddict0 = FDDict()
    fddict1 = FDDict()
    fddict2 = FDDict()

    for i, fddict in enumerate([fddict1, fddict2]):
        fddict.BlueFuzz = 0
        fddict.BaselineYCoord = 0
        fddict.BaselineOvershoot = -10
        setattr(fddict, stemdict,
                "[" + " ".join([str(i * 10), str(i * 20)]) + "]")
        fddict.buildBlueLists()

    mergeFDDicts([fddict1, fddict2], fddict0)
