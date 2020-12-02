import os
import logging

from . import _psautohint


log = logging.getLogger(__name__)

__version__ = _psautohint.version


class FontParseError(Exception):
    pass


def _font_is_ufo(path):
    from fontTools.ufoLib import UFOReader
    from fontTools.ufoLib.errors import UFOLibError
    try:
        UFOReader(path, validate=False)
        return True
    except (UFOLibError, KeyError, TypeError):
        return False


def get_font_format(font_file_path):
    if _font_is_ufo(font_file_path):
        return "UFO"
    elif os.path.isfile(font_file_path):
        with open(font_file_path, 'rb') as f:
            head = f.read(4)
            if head == b'OTTO':
                return 'OTF'
            elif head[0:2] == b'\x01\x00':
                return 'CFF'
            elif head[0:2] == b'\x80\x01':
                return 'PFB'
            elif head in (b'%!PS', b'%!Fo'):
                for fullhead in (b'%!PS-AdobeFont', b'%!FontType1',
                                 b'%!PS-Adobe-3.0 Resource-CIDFont'):
                    f.seek(0)
                    if f.read(len(fullhead)) == fullhead:
                        if b"CID" not in fullhead:
                            return 'PFA'
                        else:
                            return 'PFC'
        return None
    else:
        return None


def hint_bez_glyph(info, glyph, allow_edit=True, allow_hint_sub=True,
                   round_coordinates=True, report_zones=False,
                   report_stems=False, report_all_stems=False):
    report = 0
    if report_zones:
        report = 1
    elif report_stems:
        report = 2
    # In/out of C code is bytes. In/out of Python code is str.
    hinted_b = _psautohint.autohint(info.encode('ascii'),
                                    glyph.encode('ascii'),
                                    allow_edit,
                                    allow_hint_sub,
                                    round_coordinates,
                                    report,
                                    report_all_stems)
    hinted = hinted_b.decode('ascii')

    return hinted


def hint_compatible_bez_glyphs(info, glyphs, masters):
    hinted = _psautohint.autohintmm(tuple(g.encode('ascii') for g in glyphs),
                                    tuple(m.encode('ascii') for m in masters))

    return [g.decode('ascii') for g in hinted]
