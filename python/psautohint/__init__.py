from __future__ import print_function, absolute_import

import os
import sys
import logging

from fontTools.misc.py23 import tobytes, tounicode

from . import _psautohint


log = logging.getLogger(__name__)

__version__ = _psautohint.version


AUTOHINTEXE = os.path.join(
    os.path.dirname(__file__),
    "autohintexe" + (".exe" if sys.platform in ("win32", "cygwin") else "")
)
if not os.path.isfile(AUTOHINTEXE) or not os.access(AUTOHINTEXE, os.X_OK):
    import warnings
    warnings.warn(
        "embedded 'autohintexe' executable not found: %r" % AUTOHINTEXE
    )


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


def _hint_with_autohintexe(info, glyph, allow_edit, allow_hint_sub,
                           round_coordinates, report_zones,
                           report_stems, report_all_stems):
    import subprocess

    edit = "" if allow_edit else "-e"
    hintsub = "" if allow_hint_sub else "-n"
    decimal = "" if round_coordinates else "-d"
    zones = "-ra" if report_zones else ""
    stems = "-rs" if report_stems else ""
    allstems = "-a" if report_all_stems else ""
    cmd = [AUTOHINTEXE, edit, hintsub, decimal, zones, stems, allstems,
           "-D", "-i", info, "-b", glyph]
    cmd = [a for a in cmd if a]  # Filter out empty args, just in case.
    try:
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        outdata, errordata = p.communicate()

        # A bit of hack to parse the stderr output and route it through our
        # logger.
        if errordata:
            errordata = tounicode(errordata).strip().split("\n")
            for line in errordata:
                if ": " in line:
                    level, msg = line.split(": ", 1)
                    if level not in ("DEBUG", "INFO", "WARNING", "ERROR"):
                        level, msg = "INFO", line
                    getattr(log, level.lower())(msg)
                else:
                    log.info(line)

        return tounicode(outdata)
    except (subprocess.CalledProcessError, OSError) as err:
        raise _psautohint.error(err)


def hint_bez_glyph(info, glyph, allow_edit=True, allow_hint_sub=True,
                   round_coordinates=True, report_zones=False,
                   report_stems=False, report_all_stems=False,
                   use_autohintexe=False):
    if use_autohintexe:
        hinted = _hint_with_autohintexe(info,
                                        glyph,
                                        allow_edit,
                                        allow_hint_sub,
                                        round_coordinates,
                                        report_zones,
                                        report_stems,
                                        report_all_stems)
    else:
        report = 0
        if report_zones:
            report = 1
        elif report_stems:
            report = 2
        hinted = _psautohint.autohint(tobytes(info),
                                      tobytes(glyph),
                                      allow_edit,
                                      allow_hint_sub,
                                      round_coordinates,
                                      report,
                                      report_all_stems)

    return tounicode(hinted)


def hint_compatible_bez_glyphs(info, glyphs, masters, use_autohintexe=False):
    hinted = _psautohint.autohintmm(tuple(tobytes(g) for g in glyphs),
                                    tuple(tobytes(m) for m in masters))

    return [tounicode(g) for g in hinted]
