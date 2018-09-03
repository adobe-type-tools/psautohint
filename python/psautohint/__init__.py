from __future__ import print_function, absolute_import

import os
import sys
import plistlib

from fontTools.misc.py23 import tobytes, tounicode

from . import _psautohint


__version__ = _psautohint.version


class FontParseError(Exception):
    pass


def _font_is_ufo(path):
    if os.path.isdir(path) and path.lower().endswith('.ufo'):
        meta_path = os.path.join(path, 'metainfo.plist')
        if os.path.isfile(meta_path):
            metainfo = plistlib.readPlist(meta_path)
            keys = ('creator', 'formatVersion')
            if metainfo and all([key in metainfo for key in keys]):
                return True
    return False


def get_font_format(font_file_path):
    if os.path.isfile(font_file_path):
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
        if _font_is_ufo(font_file_path):
            return "UFO"
        return None


def hint_bez_glyph(info, glyph, allow_edit=True, allow_hint_sub=True,
                   round_coordinates=True):
    hinted = _psautohint.autohint(tobytes(info),
                                  tobytes(glyph),
                                  allow_edit,
                                  allow_hint_sub,
                                  round_coordinates)

    return tounicode(hinted)


def hint_compatible_bez_glyphs(info, glyphs, masters):
    hinted = _psautohint.autohintmm(tobytes(info),
                                    tuple(tobytes(g) for g in glyphs),
                                    tuple(tobytes(m) for m in masters))

    return [tounicode(g) for g in hinted]


AUTOHINTEXE = os.path.join(
    os.path.dirname(__file__),
    "autohintexe" + (".exe" if sys.platform in ("win32", "cygwin") else "")
)
if not os.path.isfile(AUTOHINTEXE) or not os.access(AUTOHINTEXE, os.X_OK):
    import warnings
    warnings.warn(
        "embedded 'autohintexe' executable not found: %r" % AUTOHINTEXE
    )
