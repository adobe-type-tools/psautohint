/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"

/* number of default entries in counter hint glyph list. */
#define COUNTERDEFAULTENTRIES 4
#define COUNTERLISTSIZE 20

char* gVHintList[] = { "m",  "M",  "T",  "ellipsis", NULL, NULL, NULL,
                       NULL, NULL, NULL, NULL,       NULL, NULL, NULL,
                       NULL, NULL, NULL, NULL,       NULL, NULL };
char* gHHintList[] = { "element", "equivalence", "notelement", "divide", NULL,
                       NULL,      NULL,          NULL,         NULL,     NULL,
                       NULL,      NULL,          NULL,         NULL,     NULL,
                       NULL,      NULL,          NULL,         NULL,     NULL };

static char* UpperSpecialGlyphs[] = { "questiondown", "exclamdown", "semicolon",
                                      NULL };

static char* LowerSpecialGlyphs[] = { "question", "exclam", "colon", NULL };

static char* NoBlueList[] = { "at",       "bullet",     "copyright",
                              "currency", "registered", NULL };

bool
FindNameInList(char* nm, char** lst)
{
    char** l = lst;
    while (true) {
        char* lnm = *l;
        if (lnm == NULL)
            return false;
        if (strcmp(lnm, nm) == 0)
            return true;
        l++;
    }
}

/* Adds specified glyphs to CounterHintList array. */
int
AddCounterHintGlyphs(char* charlist, char* HintList[])
{
    const char* setList = "(), \t\n\r";
    char* token;
    bool firstTime = true;
    int16_t ListEntries = COUNTERDEFAULTENTRIES;

    while (true) {
        if (firstTime) {
            token = (char*)strtok(charlist, setList);
            firstTime = false;
        } else
            token = (char*)strtok(NULL, setList);
        if (token == NULL)
            break;
        if (FindNameInList(token, HintList))
            continue;
        /* Currently, HintList must end with a NULL pointer. */
        if (ListEntries == (COUNTERLISTSIZE - 1)) {
            LogMsg(WARNING, OK,
                   "Exceeded counter hints list size. (maximum is %d.) "
                   "Cannot add %s or subsequent characters.",
                   (int)COUNTERLISTSIZE, token);
            break;
        }
        HintList[ListEntries] =
          AllocateMem(1, strlen(token) + 1, "counter hints list");
        strcpy(HintList[ListEntries++], token);
    }
    return (ListEntries - COUNTERDEFAULTENTRIES);
}

int32_t
SpecialGlyphType(void)
{
    /* 1 = upper; -1 = lower; 0 = neither */
    if (FindNameInList(gGlyphName, UpperSpecialGlyphs))
        return 1;
    if (FindNameInList(gGlyphName, LowerSpecialGlyphs))
        return -1;
    return 0;
}

bool
HHintGlyph(void)
{
    return FindNameInList(gGlyphName, gHHintList);
}

bool
VHintGlyph(void)
{
    return FindNameInList(gGlyphName, gVHintList);
}

bool
NoBlueGlyph(void)
{
    return FindNameInList(gGlyphName, NoBlueList);
}

bool
MoveToNewHints(void)
{
    return strcmp(gGlyphName, "percent") == 0 ||
           strcmp(gGlyphName, "perthousand") == 0;
}
