/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"
#include "machinedep.h"

char* VColorList[] = { "m",  "M",  "T",  "ellipsis", NULL, NULL, NULL,
                       NULL, NULL, NULL, NULL,       NULL, NULL, NULL,
                       NULL, NULL, NULL, NULL,       NULL, NULL };
char* HColorList[] = { "element", "equivalence", "notelement", "divide", NULL,
                       NULL,      NULL,          NULL,         NULL,     NULL,
                       NULL,      NULL,          NULL,         NULL,     NULL,
                       NULL,      NULL,          NULL,         NULL,     NULL };

static char* UpperSpecialChars[] = { "questiondown", "exclamdown", "semicolon",
                                     NULL };

static char* LowerSpecialChars[] = { "question", "exclam", "colon", NULL };

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

static bool
StrEqual(char* s1, char* s2)
{
    unsigned char c1, c2;
    while (true) {
        c1 = *s1++;
        c2 = *s2++;
        if (c1 != c2)
            return false;
        if (c1 == 0 && c2 == 0)
            return true;
        if (c1 == 0 || c2 == 0)
            return false;
    }
}

extern bool
FindNameInList(char* nm, char** lst)
{
    char **l, *lnm;
    l = lst;
    while (true) {
        lnm = *l;
        if (lnm == NULL)
            return false;
        if (StrEqual(lnm, nm))
            return true;
        l++;
    }
}

/* Adds specified characters to CounterColorList array. */
extern int
AddCounterColorChars(char* charlist, char* ColorList[])
{
    const char* setList = "(), \t\n\r";
    char* token;
    int16_t length;
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
        if (FindNameInList(token, ColorList))
            continue;
        /* Currently, ColorList must end with a NULL pointer. */
        if (ListEntries == (COUNTERLISTSIZE - 1)) {
            snprintf(globmsg, MAXMSGLEN,
                     "Exceeded counter hints list size. (maximum is "
                     "%d.)\n  Cannot add %s or subsequent "
                     "characters.\n",
                     (int)COUNTERLISTSIZE, token);
            LogMsg(globmsg, WARNING, OK);
            break;
        }
        length = (int16_t)strlen(token);
        ColorList[ListEntries] =
          AllocateMem(1, length + 1, "counter hints list");
        strcpy(ColorList[ListEntries++], token);
    }
    return (ListEntries - COUNTERDEFAULTENTRIES);
}

int32_t
SpecialCharType(void)
{
/* 1 = upper; -1 = lower; 0 = neither */
    if (FindNameInList(bezGlyphName, UpperSpecialChars))
        return 1;
    if (FindNameInList(bezGlyphName, LowerSpecialChars))
        return -1;
    return 0;
}

bool
HColorChar(void)
{
    return FindNameInList(bezGlyphName, HColorList);
}

bool
VColorChar(void)
{
    return FindNameInList(bezGlyphName, VColorList);
}

bool
NoBlueChar(void)
{
    return FindNameInList(bezGlyphName, NoBlueList);
}

int32_t
SolEolCharCode(void)
{
    if (FindNameInList(bezGlyphName, SolEol0List))
        return 0;
    if (FindNameInList(bezGlyphName, SolEol1List))
        return 1;
    if (FindNameInList(bezGlyphName, SolEolNeg1List))
        return -1;
    return 2;
}

/* This change was made to prevent bogus sol-eol's.  And to prevent
   adding sol-eol if there are a lot of subpaths. */
bool
SpecialSolEol(void)
{
    int32_t code = SolEolCharCode();
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

static PPathElt
SubpathEnd(PPathElt e)
{
    while (true) {
        e = e->next;
        if (e == NULL)
            return pathEnd;
        if (e->type == MOVETO)
            return e->prev;
    }
}

static PPathElt
SubpathStart(PPathElt e)
{
    while (e != pathStart) {
        if (e->type == MOVETO)
            break;
        e = e->prev;
    }
    return e;
}

static PPathElt
SolEol(PPathElt e)
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
    PPathElt e;
    e = pathStart->next;
    while (e != NULL) {
        e = SolEol(e);
        e = e->next;
    }
}

static void
SolEolUpperOrLower(bool upper)
{
    PPathElt e, s1, s2;
    Fixed x1, y1, s1y, s2y;
    bool s1Upper;
    if (pathStart == NULL)
        return;
    e = s1 = pathStart->next;
    GetEndPoint(e, &x1, &y1);
    s1y = itfmy(y1);
    e = SubpathEnd(e);
    e = e->next;
    if (e == NULL)
        return;
    s2 = e;
    GetEndPoint(e, &x1, &y1);
    s2y = itfmy(y1);
    s1Upper = (s1y > s2y);
    if ((upper && s1Upper) || (!upper && !s1Upper))
        (void)SolEol(s1);
    else
        (void)SolEol(s2);
}

/* This change was made to prevent bogus sol-eol's.  And to prevent
   adding sol-eol if there are a lot of subpaths. */
void
AddSolEol(void)
{
    if (pathStart == NULL)
        return;
    if (!SpecialSolEol())
        return;
    switch (SolEolCharCode()) {
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
MoveToNewClrs(void)
{
    /* XXX: fileName is always empty, we probably want to use bezGlyphName here
     * but this causes a change in the hinting of percent and perthousand
     * glyphs and need to be verified. */
    return StrEqual(fileName, "percent") || StrEqual(fileName, "perthousand");
}
