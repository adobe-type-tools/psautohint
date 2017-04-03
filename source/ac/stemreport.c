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
    if (curved && !allstems)
        return;

    if (addVStemCB != NULL) {
        addVStemCB(top, bottom, glyphName);
    }
}

void
AddHStem(Fixed right, Fixed left, bool curved)
{
    if (curved && !allstems)
        return;

    if (addHStemCB != NULL) {
        addHStemCB(right, left, glyphName);
    }
}

void
AddCharExtremes(Fixed bot, Fixed top)
{
    if (addCharExtremesCB != NULL) {
        addCharExtremesCB(top, bot, glyphName);
    }
}

void
AddStemExtremes(Fixed bot, Fixed top)
{
    if (addStemExtremesCB != NULL) {
        addStemExtremesCB(top, bot, glyphName);
    }
}
