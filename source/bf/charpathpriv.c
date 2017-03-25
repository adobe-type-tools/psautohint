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

#include "bftoac.h"
#include "buildfont.h"
#include "charpath.h"

#define MAXPATHELT 100 /* initial maximum number of path elements */

static int32_t maxPathEntries = 0;
static PPathList currPathList = NULL;
int32_t path_entries;
bool addHints = true;

static void CheckPath(void);

static void
CheckPath(void)
{

    if (currPathList->path == NULL) {
        currPathList->path =
          (CharPathElt*)AllocateMem((unsigned int)maxPathEntries,
                                    sizeof(CharPathElt), "path element array");
    }
    if (path_entries >= maxPathEntries) {
        int i;

        maxPathEntries += MAXPATHELT;
        currPathList->path = (PCharPathElt)ReallocateMem(
          (char*)currPathList->path, maxPathEntries * sizeof(CharPathElt),
          "path element array");
        /* Initialize certain fields in CharPathElt, since realloc'ed memory */
        /* may be non-zero. */
        for (i = path_entries; i < maxPathEntries; i++) {
            currPathList->path[i].hints = NULL;
            currPathList->path[i].isFlex = false;
            currPathList->path[i].sol = false;
            currPathList->path[i].eol = false;
            currPathList->path[i].remove = false;
        }
    }
}

extern PCharPathElt
AppendCharPathElement(int pathtype)
{

    CheckPath();
    currPathList->path[path_entries].type = pathtype;
    path_entries++;
    return (&currPathList->path[path_entries - 1]);
}

extern void
SetHintsElt(int16_t hinttype, CdPtr coord, int32_t elt1, int32_t elt2,
            bool mainhints)
{
    PHintElt* hintEntry;
    PHintElt lastHintEntry = NULL;

    if (!addHints)
        return;
    if (mainhints) /* define main hints for character */
        hintEntry = &currPathList->mainhints;
    else {
        CheckPath();
        hintEntry = &currPathList->path[path_entries].hints;
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
extern void
SetNoHints(void)
{
    addHints = false;
}

/* According to Bill Paxton the offset locking commands should
   be replaced by hint substitution and is not necessary to
   use for blended fonts.  This means characters that should
   have these commands may not look as good on Classic LW's. */
/*
extern void SetOffsetLocking(locktype)
char *locktype;
{
  if (STREQ(locktype, "sol"))
    currPathList[path_entries-1].sol = true;
  else
    currPathList[path_entries-1].eol = true;
}
*/
