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

int32_t gPathEntries = 0; /* number of elements in a character path */
bool gAddHints = true;    /* whether to include hints in the font */

#define MAXPATHELT 100 /* initial maximum number of path elements */

static int32_t maxPathEntries = 0;
static PPathList currPathList = NULL;

static void CheckPath(void);

static void
CheckPath(void)
{

    if (currPathList->path == NULL) {
        currPathList->path = (CharPathElt*)AllocateMem(
          maxPathEntries, sizeof(CharPathElt), "path element array");
    }
    if (gPathEntries >= maxPathEntries) {
        int i;

        maxPathEntries += MAXPATHELT;
        currPathList->path = (PCharPathElt)ReallocateMem(
          (char*)currPathList->path, maxPathEntries * sizeof(CharPathElt),
          "path element array");
        /* Initialize certain fields in CharPathElt, since realloc'ed memory */
        /* may be non-zero. */
        for (i = gPathEntries; i < maxPathEntries; i++) {
            currPathList->path[i].hints = NULL;
            currPathList->path[i].isFlex = false;
            currPathList->path[i].sol = false;
            currPathList->path[i].eol = false;
            currPathList->path[i].remove = false;
        }
    }
}

PCharPathElt
AppendCharPathElement(int pathtype)
{

    CheckPath();
    currPathList->path[gPathEntries].type = pathtype;
    gPathEntries++;
    return (&currPathList->path[gPathEntries - 1]);
}

/* Called from CompareCharPaths when a new character is being read. */
void
ResetMaxPathEntries(void)
{
    maxPathEntries = MAXPATHELT;
}

void
SetCurrPathList(PPathList plist)
{
    currPathList = plist;
}

void
SetHintsElt(int16_t hinttype, CdPtr coord, int32_t elt1, int32_t elt2,
            bool mainhints)
{
    PHintElt* hintEntry;
    PHintElt lastHintEntry = NULL;

    if (!gAddHints)
        return;
    if (mainhints) /* define main hints for character */
        hintEntry = &currPathList->mainhints;
    else {
        CheckPath();
        hintEntry = &currPathList->path[gPathEntries].hints;
    }
    lastHintEntry = (PHintElt)AllocateMem(1, sizeof(HintElt), "hint element");
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

/* Called when character file contains hinting operators, but
   not the path element information needed for making blended
   fonts. */
void
SetNoHints(void)
{
    gAddHints = false;
}

/* According to Bill Paxton the offset locking commands should
   be replaced by hint substitution and is not necessary to
   use for blended fonts.  This means characters that should
   have these commands may not look as good on Classic LW's. */
/*
void SetOffsetLocking(locktype)
char *locktype;
{
  if (strcmp(locktype, "sol") == 0)
    currPathList[gPathEntries-1].sol = true;
  else
    currPathList[gPathEntries-1].eol = true;
}
*/
