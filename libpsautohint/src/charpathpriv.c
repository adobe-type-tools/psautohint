/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

/* This source file should contain any procedure(s) that are called
   from AC.  It should not contain calls to procedures implemented
   in object files that are not bound into AC. */

#include "charpath.h"
#include "memory.h"

int32_t gPathEntries = 0; /* number of elements in a glyph path */
bool gAddHints = true;    /* whether to include hints in the font */

#define MAXPATHELT 100 /* initial maximum number of path elements */

static int32_t maxPathEntries = 0;
static PathList* currPathList = NULL;

static void CheckPath(void);

static void
CheckPath(void)
{

    if (currPathList->path == NULL) {
        currPathList->path = (GlyphPathElt*)AllocateMem(
          maxPathEntries, sizeof(GlyphPathElt), "path element array");
    }
    if (gPathEntries >= maxPathEntries) {
        int i;

        maxPathEntries += MAXPATHELT;
        currPathList->path = (GlyphPathElt*)ReallocateMem(
          (char*)currPathList->path, maxPathEntries * sizeof(GlyphPathElt),
          "path element array");
        /* Initialize certain fields in GlyphPathElt, since realloc'ed memory */
        /* may be non-zero. */
        for (i = gPathEntries; i < maxPathEntries; i++) {
            currPathList->path[i].hints = NULL;
            currPathList->path[i].isFlex = false;
        }
    }
}

GlyphPathElt*
AppendGlyphPathElement(int pathtype)
{

    CheckPath();
    currPathList->path[gPathEntries].type = pathtype;
    gPathEntries++;
    return (&currPathList->path[gPathEntries - 1]);
}

/* Called from CompareGlyphPaths when a new glyph is being read. */
void
ResetMaxPathEntries(void)
{
    maxPathEntries = MAXPATHELT;
}

void
SetCurrPathList(PathList* plist)
{
    currPathList = plist;
}

void
SetHintsElt(int16_t hinttype, Cd* coord, int32_t elt1, int32_t elt2,
            bool mainhints)
{
    HintElt** hintEntry;
    HintElt* lastHintEntry = NULL;

    if (!gAddHints)
        return;
    if (mainhints) /* define main hints for glyph */
        hintEntry = &currPathList->mainhints;
    else {
        CheckPath();
        hintEntry = &currPathList->path[gPathEntries].hints;
    }
    lastHintEntry = (HintElt*)AllocateMem(1, sizeof(HintElt), "hint element");
    lastHintEntry->type = hinttype;
    lastHintEntry->leftorbot = coord->x;
    lastHintEntry->rightortop = coord->y; /* absolute coordinates */
    lastHintEntry->pathix1 = elt1;
    lastHintEntry->pathix2 = elt2;
    while (*hintEntry != NULL && (*hintEntry)->next != NULL)
        hintEntry = &(*hintEntry)->next;
    if (*hintEntry == NULL)
        *hintEntry = lastHintEntry;
    else
        (*hintEntry)->next = lastHintEntry;
}
