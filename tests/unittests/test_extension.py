import pytest
import sys

from fontTools.misc.py23 import tounicode

from psautohint import _psautohint


INFO = b"FontName Foo"
NAME = b"Foo"
GLYPH = b"""% square
0 500 rb
60 500 ry
sc
560 500 mt
560 0 dt
60 0 dt
60 500 dt
cp
ed
"""


def test_autohint_good_args():
    _psautohint.autohint(INFO, GLYPH)


def test_autohintmm_good_args():
    _psautohint.autohintmm((GLYPH, GLYPH), (NAME, NAME))


@pytest.mark.parametrize("args", [
    [],                          # no arguments
    [INFO],                      # 1 argument
    [tounicode(INFO), GLYPH],    # 1st is string not bytes
    [INFO, tounicode(GLYPH)],    # 2nd is string not bytes
    [[INFO], tounicode(GLYPH)],  # 1st is a list
    [INFO, [tounicode(GLYPH)]],  # 2nd is a list
])
def test_autohint_bad_args(args):
    with pytest.raises(TypeError):
        _psautohint.autohint(*args)


@pytest.mark.parametrize("args", [
    [],                          # no arguments
    [(GLYPH, GLYPH)],            # 1 argument
    [GLYPH, (NAME, NAME)],       # 1st is not a tuple
    [(GLYPH, GLYPH), NAME],      # 2nd is not a tuple
    [(GLYPH, GLYPH), tuple()],   # 2nd is an empty tuple
    [(GLYPH, GLYPH), (NAME,)],   # 2nd is shorter than 1st
    [(GLYPH,), (NAME,)],         # 1st is one glyph
])
def test_autohintmm_bad_args(args):
    with pytest.raises(TypeError):
        _psautohint.autohintmm(*args)


@pytest.mark.parametrize("args", [
    [(tounicode(GLYPH), GLYPH), (NAME, NAME)],  # 1st should be bytes
    [(GLYPH, GLYPH), (tounicode(NAME), NAME)],  # 2nd should be bytes
])
@pytest.mark.skipif(sys.version_info < (3, 0),
                    reason="Python 2 accepts the unicode strings here!")
def test_autohintmm_unicode(args):
    with pytest.raises(TypeError):
        _psautohint.autohintmm(*args)


@pytest.mark.parametrize("glyph", [
    b"% foo\ned",        # ending comment with newline
    b"% foo\red",        # ending comment with linefeed
    b"\t% foo\nsc\ted",  # separating tokens with tab
    b"% foo\nsc ed",     # separating tokens with space
    b"% foo\nsc\ned",    # separating tokens with newline
    b"% foo\nsc\red",    # separating tokens with linefeed
    b"% foo",            # glyph name only
    b"% foo bar",        # extra data after glyph name
])
def test_autohint_good_glyph(glyph):
    result = _psautohint.autohint(INFO, glyph)
    assert result == b"% foo\nsc\ned\n"


@pytest.mark.parametrize("glyph", [
  b"% foo\ncf",            # unknown operator
  b"% foo\n" + 80 * b"f",  # too long unknown operator
  b"% " + 65 * b"A",       # too long glyph name
  b"% foo\n10 ",           # number left on stack at end of glyph
  b"% foo\n0a 0 rm\ned",   # bad number terminator
  b"% foo\nry",            # stack underflow
  b"% foo\n$",             # unexpected character
])
def test_autohint_bad_glyph(glyph):
    with pytest.raises(_psautohint.error):
        _psautohint.autohint(INFO, glyph)


@pytest.mark.parametrize("glyphs", [
    (b"cf", b"cf"),
])
def test_autohintmm_bad_glyphs(glyphs):
    with pytest.raises(_psautohint.error):
        _psautohint.autohintmm(glyphs, (NAME, NAME))


@pytest.mark.parametrize("info", [
  b"HCounterChars [" + b" ".join(b"A" * i for i in range(16)) + b"]",
  b"VCounterChars [" + b" ".join(b"A" * i for i in range(16)) + b"]",
])
def test_autohint_too_many_counter_glyphs(info):
    _psautohint.autohint(info, GLYPH)
