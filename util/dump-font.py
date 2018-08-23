from __future__ import print_function

import argparse
import os
import sys

from psautohint.autohint import ACOptions, openFile


def open_font(path):
    font = openFile(path, ACOptions())
    return font


def get_font_info(font):
    info = font.getFontInfo(False, False, [], [])
    return info.getFontInfo()


def split_at_comma(comma_str):
    return [item.strip() for item in comma_str.split(',')]


def main():
    parser = argparse.ArgumentParser(description="Dump font in bez format.")
    parser.add_argument("path", metavar="FILE", help="input font to process")
    parser.add_argument("-o", "--out", metavar="FILE",
                        help="output directory, default is inputh path "
                             "+ '.bez'")
    parser.add_argument("-n", "--no-fontinfo", action="store_true",
                        help="don't output fontinfo")
    parser.add_argument("-m", "--no-metainfo", action="store_true",
                        help="don't output metainfo")
    parser.add_argument("-g", "--glyphs", metavar="NAMES", type=split_at_comma,
                        help="comma-separated list of glyphs to dump, "
                             "all glyphs will be dumped by default")
    args = parser.parse_args()

    path = args.path
    outpath = args.out

    if not outpath:
        outpath = os.path.splitext(os.path.abspath(path))[0] + ".bez"

    os.makedirs(outpath)

    print("Writing output to:", outpath)

    font = open_font(path)

    names = args.glyphs
    if not names:
        names = font.getGlyphList()

    if not args.no_fontinfo:
        info = get_font_info(font)
        with open(os.path.join(outpath, "fontinfo"), "x") as fp:
            fp.write(info)

    glyphs = [font.convertToBez(n, True, True)[0] for n in names]
    for name, glyph in zip(names, glyphs):
        if glyph is None:
            glyph = ""
        glyph_path = name.lower() + ".bez"
        with open(os.path.join(outpath, glyph_path), "x") as fp:
            fp.write(glyph)


if __name__ == "__main__":
    sys.exit(main())
