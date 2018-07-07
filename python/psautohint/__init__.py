from __future__ import print_function, absolute_import

import os
import plistlib

from ._psautohint import version as __version__


def _font_is_ufo(path):
    if os.path.isdir(path) and path.lower().endswith('.ufo'):
        meta_path = os.path.join(path, 'metainfo.plist')
        if os.path.isfile(meta_path):
            metainfo = plistlib.readPlist(meta_path)
            if all([key in metainfo for key in ('creator', 'formatVersion')]):
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
            elif head == b'\x80\x01\xb6\x12':
                return 'PFB'
            elif head in (b'%!PS', b'%!Fo'):
                for fullhead in (b'%!PS-AdobeFont', b'%!FontType1'):
                    f.seek(0)
                    if f.read(len(fullhead)) == fullhead:
                        return 'PFA'
        return None
    else:
        if _font_is_ufo(font_file_path):
            return "UFO"
        return None
