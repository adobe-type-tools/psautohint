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
AddVStem(Fixed top, Fixed bottom, bool curved)
{
    if (curved && !gAllStems)
        return;

    if (gAddVStemCB != NULL) {
        gAddVStemCB(top, bottom, gGlyphName);
    }
}

void
AddHStem(Fixed right, Fixed left, bool curved)
{
    if (curved && !gAllStems)
        return;

    if (gAddHStemCB != NULL) {
        gAddHStemCB(right, left, gGlyphName);
    }
}

void
AddCharExtremes(Fixed bot, Fixed top)
{
    if (gAddCharExtremesCB != NULL) {
        gAddCharExtremesCB(top, bot, gGlyphName);
    }
}

void
AddStemExtremes(Fixed bot, Fixed top)
{
    if (gAddStemExtremesCB != NULL) {
        gAddStemExtremesCB(top, bot, gGlyphName);
    }
}
