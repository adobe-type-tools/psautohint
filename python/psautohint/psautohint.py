from __future__ import print_function, absolute_import

from . import _psautohint


def toBytes(o):
    if hasattr(o, "encode"):
        return o.encode("utf-8")
    if isinstance(o, (list, tuple)):
        return [toBytes(i) for i in o]
    return o


def toStr(o):
    if hasattr(o, "decode"):
        return o.decode("utf-8")
    if isinstance(o, (list, tuple)):
        return [toStr(i) for i in o]
    return o


def autohint(info, glyph, verbose=True, allow_edit=True, allow_hint_sub=True,
             round_coordinates=True, debug=False):
    hinted = _psautohint.autohint(toBytes(info),
                                  toBytes(glyph),
                                  verbose,
                                  allow_edit,
                                  allow_hint_sub,
                                  round_coordinates,
                                  debug)

    return toStr(hinted)


def autohintmm(info, glyphs, masters, verbose=True):
    hinted = _psautohint.autohintmm(toBytes(info),
                                    toBytes(glyphs),
                                    toBytes(masters),
                                    verbose)

    return toStr(hinted)
