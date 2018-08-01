/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"

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

static char* SolEol0List[] = { "asciitilde",
                               "asterisk",
                               "bullet",
                               "period",
                               "periodcentered",
                               "colon",
                               "dieresis",
                               "divide",
                               "ellipsis",
                               "exclam",
                               "exclamdown",
                               "guillemotleft",
                               "guillemotright",
                               "guilsinglleft",
                               "guilsinglright",
                               "quotesingle",
                               "quotedbl",
                               "quotedblbase",
                               "quotesinglbase",
                               "quoteleft",
                               "quoteright",
                               "quotedblleft",
                               "quotedblright",
                               "tilde",
                               NULL };

static char* SolEol1List[] = { "i", "j", "questiondown", "semicolon", NULL };

static char* SolEolNeg1List[] = { "question", NULL };

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

int32_t
SolEolGlyphCode(void)
{
    if (FindNameInList(gGlyphName, SolEol0List))
        return 0;
    if (FindNameInList(gGlyphName, SolEol1List))
        return 1;
    if (FindNameInList(gGlyphName, SolEolNeg1List))
        return -1;
    return 2;
}

/* This change was made to prevent bogus sol-eol's.  And to prevent
   adding sol-eol if there are a lot of subpaths. */
bool
SpecialSolEol(void)
{
    int32_t code = SolEolGlyphCode();
    int32_t count;
    if (code == 2)
        return false;
    count = CountSubPaths();
    if (code != 0 && count != 2)
        return false;
    if (code == 0 && count > 3)
        return false;
    return true;
}

static PathElt*
SubpathEnd(PathElt* e)
{
    while (true) {
        e = e->next;
        if (e == NULL)
            return gPathEnd;
        if (e->type == MOVETO)
            return e->prev;
    }
}

static PathElt*
SubpathStart(PathElt* e)
{
    while (e != gPathStart) {
        if (e->type == MOVETO)
            break;
        e = e->prev;
    }
    return e;
}

static PathElt*
SolEol(PathElt* e)
{
    e = SubpathStart(e);
    e->sol = true;
    e = SubpathEnd(e);
    e->eol = true;
    return e;
}

static void
SolEolAll(void)
{
    PathElt* e;
    e = gPathStart->next;
    while (e != NULL) {
        e = SolEol(e);
        e = e->next;
    }
}

static void
SolEolUpperOrLower(bool upper)
{
    PathElt *e, *s1, *s2;
    Fixed x1, y1, s1y, s2y;
    bool s1Upper;
    if (gPathStart == NULL)
        return;
    e = s1 = gPathStart->next;
    GetEndPoint(e, &x1, &y1);
    s1y = -y1;
    e = SubpathEnd(e);
    e = e->next;
    if (e == NULL)
        return;
    s2 = e;
    GetEndPoint(e, &x1, &y1);
    s2y = -y1;
    s1Upper = (s1y > s2y);
    if ((upper && s1Upper) || (!upper && !s1Upper))
        SolEol(s1);
    else
        SolEol(s2);
}

/* This change was made to prevent bogus sol-eol's.  And to prevent
   adding sol-eol if there are a lot of subpaths. */
void
AddSolEol(void)
{
    if (gPathStart == NULL)
        return;
    if (!SpecialSolEol())
        return;
    switch (SolEolGlyphCode()) {
        /* 1 means upper, -1 means lower, 0 means all */
        case 0:
            SolEolAll();
            break;
        case 1:
            SolEolUpperOrLower(true);
            break;
        case -1:
            SolEolUpperOrLower(false);
            break;
    }
}

bool
MoveToNewHints(void)
{
    return strcmp(gGlyphName, "percent") == 0 ||
           strcmp(gGlyphName, "perthousand") == 0;
}
