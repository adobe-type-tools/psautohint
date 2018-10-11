/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"

void
AddVStem(Fixed right, Fixed left, bool curved)
{
    if (curved && !gAllStems)
        return;

    if (gAddVStemCB)
        gAddVStemCB(FIXED2FLOAT(right), FIXED2FLOAT(left), gGlyphName,
                    gAddStemUserData);
}

void
AddHStem(Fixed top, Fixed bottom, bool curved)
{
    if (curved && !gAllStems)
        return;

    if (gAddHStemCB)
        gAddHStemCB(FIXED2FLOAT(top), FIXED2FLOAT(bottom), gGlyphName,
                    gAddStemUserData);
}

void
AddGlyphExtremes(Fixed bot, Fixed top)
{
    if (gAddGlyphExtremesCB)
        gAddGlyphExtremesCB(FIXED2FLOAT(top), FIXED2FLOAT(bot), gGlyphName,
                            gAddExtremesUserData);
}

void
AddStemExtremes(Fixed bot, Fixed top)
{
    if (gAddStemExtremesCB)
        gAddStemExtremesCB(FIXED2FLOAT(top), FIXED2FLOAT(bot), gGlyphName,
                           gAddExtremesUserData);
}
