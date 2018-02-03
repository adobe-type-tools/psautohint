from psautohint import autohint
from psautohint import psautohint

def getFonts(masters, baseDir):
    options = autohint.ACOptions()
    options.quiet = True

    fonts = []
    infos = []
    for master in masters:
        path = "%s/%s/font.ufo" % (baseDir, master)
        font = autohint.openUFOFile(path, None, False, options)
        font.useProcessedLayer = False
        names = font.getGlyphList()
        _, fontDictList = font.getfdInfo(font.getPSName(), path, False, False, [], [], names)
        info = fontDictList[0].getFontInfo()
        fonts.append(font)
        infos.append(info)

    return fonts, infos

def getGlyphList(fonts):
    glyphList = fonts[0].getGlyphList()
    assert all([font.getGlyphList() == glyphList for font in fonts])

    return glyphList

def mmHint(masters, fonts, infos, glyphList):
    hinted = []
    failed = []
    for name in glyphList:
        glyphs = []
        for i, (font, info) in enumerate(zip(fonts, infos)):
            glyph = font.convertToBez(name, False, True)[0]
            if not glyph:
                glyph = "%%%s\n" % name
            if i == 0:
                glyph = psautohint.autohint(info, [glyph], False, False, False, False)[0]
            glyphs.append(glyph)

        try:
            glyphs = psautohint.autohintmm(infos[0], [glyphs], masters, True)
        except Exception as e:
            failed.append(name)
        hinted.append(glyphs)

    return hinted, failed

def main():
    families = [
        ["source-code-pro", (
            ["Light", "Black"],
            ["LightIt", "BlackIt"]
            ),
        ],
        ["source-serif-pro", (
            ["Light", "Black"],
            ),
        ],
    ]

    for family, masterss in families:
        for masters in masterss:
            fonts, infos = getFonts(masters, "tests/data/%s" % family)
            familyName = family
            if hasattr(fonts[0], "fontInfo"):
                familyName = fonts[0].fontInfo.get("familyName")
            print("Hinting %s: %s" % (familyName, " ".join(masters)))
            glyphList = getGlyphList(fonts)
            hinted, failed = mmHint(masters, fonts, infos, glyphList)
            if failed:
                print("ERROR: Hinting the follwing glyphs failed:")
                print("\t%s" % ", ".join(failed))

if __name__ == "__main__":
    main()
