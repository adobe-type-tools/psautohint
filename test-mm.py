from psautohint import autohint
from psautohint import psautohint

baseDir = "tests/data/source-code-pro"
masters = ("Black", "Bold", "ExtraLight", "Light", "Medium", "Regular", "Semibold")

glyphList = None

fonts = []
for master in masters:
    print("Hinting %s" % master)

    options = autohint.ACOptions()
    options.quiet = True

    path = "%s/%s/font.ufo" % (baseDir, master)
    font = autohint.openUFOFile(path, "font.ufo", False, options)
    font.useProcessedLayer = False
    names = font.getGlyphList()
    info = font.getFontInfo(font.getPSName(), path, False, False, [], [])
    info = info.getFontInfo()

    if glyphList is None:
        glyphList = names
    else:
        assert glyphList == names

    glyphs = []
    for name in glyphList:
        glyph = font.convertToBez(name, False, True)[0]
        if not glyph:
            glyph = "%%%s\n" % name
        glyphs.append(glyph)
    fonts.append(psautohint.autohint(info, glyphs, False, False, False))

glyphs = []
for i in range(len(glyphList)):
    glyphs.append([f[i] for f in fonts])

print("MM Hinting")
glyphs = psautohint.autohintmm(info, glyphs, masters, True)
