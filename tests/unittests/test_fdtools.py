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
    fddict = FDDict(0)
    fddict.setInfo("BlueFuzz", 0)
    dl = [fddict]
    gd, fd = parseFontInfoFile(dl, data, glyphs, 2000, -1000, "Foo")
    return gd, dl, fd


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
    assert font_dicts[1].BlueFuzz == 10
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
    fddict = FDDict(0)
    fddict.setInfo("BlueFuzz", 0)
    fddict.setInfo("BaselineYCoord", 0)
    fddict.setInfo("BaselineOvershoot", -10)

    for key in attributes:
        setattr(fddict, key, attributes[key])

    with pytest.raises(FontInfoParseError):
        fddict.buildBlueLists()


def test_merge_empty_fddicts():
    mergeFDDicts([FDDict(0), FDDict(1)], FDDict(2))


@pytest.mark.parametrize("stemdict", ["DominantH", "DominantV"])
def test_merge_fddicts_with_stemdicts(stemdict):
    fddict0 = FDDict(0)
    fddict1 = FDDict(1)
    fddict2 = FDDict(2)

    for i, fddict in enumerate([fddict1, fddict2]):
        fddict.setInfo("BlueFuzz", 0)
        fddict.setInfo("BaselineYCoord", 0)
        fddict.setInfo("BaselineOvershoot", -10)
        fddict.setInfo(stemdict, [str(i * 10), str(i * 20)])
        fddict.buildBlueLists()

    mergeFDDicts([fddict1, fddict2], fddict0)
