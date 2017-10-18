from psautohint import autohint
from psautohint import psautohint

baseDir = "tests/data/source-code-pro"
masters = ("Black", "Bold", "ExtraLight", "Light", "Medium", "Regular", "Semibold")

glyphList = None

fonts = []
for master in masters:
    print("Hinting %s" % master)

    path = "%s/%s/font.otf" % (baseDir, master)
    font = autohint.openOpenTypeFile(path, "font.otf", None)
    names = font.getGlyphList()
    info = font.getFontInfo(font.getPSName(), path, False, False, [], [])
    info = info.getFontInfo()

    if glyphList is None:
        glyphList = names
    else:
        assert glyphList == names

    glyphs = []
    for name in names:
        glyph = font.convertToBez(name, False)
        glyphs.append(glyph[0])
    fonts.append(psautohint.autohint(info, glyphs, False, False, False))

glyphs = []
for i in range(len(glyphList)):
    glyphs.append([f[i] for f in fonts])

print("MM Hinting")
glyphs = psautohint.autohintmm(info, glyphs, masters, True)
