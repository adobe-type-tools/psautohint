from psautohint import autohint
from psautohint import psautohint

d = "tests/data/source-code-pro"
mm = ("Black", "Bold", "ExtraLight", "Light", "Medium", "Regular", "Semibold")
gg = []
ii = None

for m in mm:
    f = autohint.openOpenTypeFile("%s/%s/font.otf" % (d, m), "font.otf", None)
    g = f.convertToBez("A", False)
    gg.append(g[0])
    if ii is None:
        ii = f.getFontInfo(f.getPSName(), "%s/%s/font.otf" % (d, m), False, False, [], [])
        ii = ii.getFontInfo()

gg = psautohint.autohint(ii, gg, True)
gg = psautohint.autohintmm(ii, [gg], True)
