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
    for name in glyphList:
        glyphs = []
        print("Hinting %s" % name)
        for i, (font, info) in enumerate(zip(fonts, infos)):
            glyph = font.convertToBez(name, False, True)[0]
            if not glyph:
                glyph = "%%%s\n" % name
            if i == 0:
                glyph = psautohint.autohint(info, [glyph], False, False, False, False)[0]
            glyphs.append(glyph)

        try:
            glyphs = _psautohint.autohintmm(infos[0], [glyphs], masters, True)
        except:
            for i, glyph in enumerate(glyphs):
                print(masters[i])
                print(glyph)
            raise
        hinted.append(glyphs)

    return hinted

def main():
    masters = ["Black", "ExtraLight"]
    fonts, infos = getFonts(masters, "tests/data/source-code-pro")
    glyphList = getGlyphList(fonts)
    hinted = mmHint(masters, fonts, infos, glyphList)

if __name__ == "__main__":
    main()
