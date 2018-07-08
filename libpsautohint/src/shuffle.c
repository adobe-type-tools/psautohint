/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"
#define MAXCNT (100)

static unsigned char* links;
static int32_t rowcnt;

void
InitShuffleSubpaths(void)
{
    int32_t cnt = -1;
    PPathElt e = gPathStart;
    while (e != NULL) { /* every element is marked with its subpath count */
        if (e->type == MOVETO)
            cnt++;
        if (gDebug) {
            if (e->type == MOVETO) { /* DEBUG */
                LogMsg(INFO, OK, "subpath %d starts at %g %g\n", cnt,
                       FixToDbl(itfmx(e->x)), FixToDbl(itfmy(e->y)));
            }
        }
        e->count = (int16_t)cnt;
        e = e->next;
    }
    cnt++;
    rowcnt = cnt;
    links =
      (cnt < 4 || cnt >= MAXCNT) ? NULL : (unsigned char*)Alloc(cnt * cnt);
}

static void
PrintLinks(void)
{
    int32_t i, j;
    LogMsg(INFO, OK, "Links ");
    for (i = 0; i < rowcnt; i++) {
        LogMsg(INFO, OK, "%d  ", i);
        if (i < 10)
            LogMsg(INFO, OK, " ");
    }
    LogMsg(INFO, OK, "\n");
    for (i = 0; i < rowcnt; i++) {
        LogMsg(INFO, OK, " %d   ", i);
        if (i < 10)
            LogMsg(INFO, OK, " ");
        for (j = 0; j < rowcnt; j++) {
            LogMsg(INFO, OK, "%d   ", links[rowcnt * i + j]);
        }
        LogMsg(INFO, OK, "\n");
    }
}

static void
PrintSumLinks(char* sumlinks)
{
    int32_t i;
    LogMsg(INFO, OK, "Sumlinks ");
    for (i = 0; i < rowcnt; i++) {
        LogMsg(INFO, OK, "%d  ", i);
        if (i < 10)
            LogMsg(INFO, OK, " ");
    }
    LogMsg(INFO, OK, "\n");
    LogMsg(INFO, OK, "         ");
    for (i = 0; i < rowcnt; i++) {
        LogMsg(INFO, OK, "%d   ", sumlinks[i]);
    }
    LogMsg(INFO, OK, "\n");
}

static void
PrintOutLinks(unsigned char* outlinks)
{
    int32_t i;
    LogMsg(INFO, OK, "Outlinks ");
    for (i = 0; i < rowcnt; i++) {
        LogMsg(INFO, OK, "%d  ", i);
        if (i < 10)
            LogMsg(INFO, OK, " ");
    }
    LogMsg(INFO, OK, "\n");
    LogMsg(INFO, OK, "         ");
    for (i = 0; i < rowcnt; i++) {
        LogMsg(INFO, OK, "%d   ", outlinks[i]);
    }
    LogMsg(INFO, OK, "\n");
}

void
MarkLinks(PClrVal vL, bool hFlg)
{
    int32_t i, j;
    PClrSeg seg;
    PPathElt e;
    if (links == NULL)
        return;
    for (; vL != NULL; vL = vL->vNxt) {
        if (vL == NULL)
            continue;
        seg = vL->vSeg1;
        if (seg == NULL)
            continue;
        e = seg->sElt;
        if (e == NULL)
            continue;
        i = e->count;
        seg = vL->vSeg2;
        if (seg == NULL)
            continue;
        e = seg->sElt;
        if (e == NULL)
            continue;
        j = e->count;
        if (i == j)
            continue;
        if (gDebug) {
            if (hFlg)
                ShowHVal(vL);
            else
                ShowVVal(vL);
            LogMsg(INFO, OK, " : %d <-> %d\n", i, j);
        }
        links[rowcnt * i + j] = 1;
        links[rowcnt * j + i] = 1;
    }
}

static void
Outpath(unsigned char* links, unsigned char* outlinks, unsigned char* output,
        int32_t bst)
{
    unsigned char *lnks, *outlnks;
    int32_t i = bst;
    PPathElt e = gPathStart;
    while (e != NULL) {
        if (e->count == i)
            break;
        e = e->next;
    }
    MoveSubpathToEnd(e);
    if (gDebug) {
        LogMsg(INFO, OK, "move subpath %d to end\n", bst); /* DEBUG */
    }
    output[bst] = 1;
    lnks = &links[bst * rowcnt];
    outlnks = outlinks;
    for (i = 0; i < rowcnt; i++)
        *outlnks++ += *lnks++;
    if (gDebug)
        PrintOutLinks(outlinks);
}

/* The intent of this code is to order the subpaths so that
 the hints will not need to change constantly because it
 is jumping from one subpath to another.  Kanji characters
 had the most problems with this which caused huge files
 to be created. */
void
DoShuffleSubpaths(void)
{
    unsigned char sumlinks[MAXCNT], output[MAXCNT], outlinks[MAXCNT];
    unsigned char* lnks;
    int32_t i, j;
    if (links == NULL)
        return;
    if (gDebug)
        PrintLinks();
    for (i = 0; i < rowcnt; i++)
        output[i] = sumlinks[i] = outlinks[i] = 0;
    lnks = links;
    for (i = 0; i < rowcnt; i++) {
        for (j = 0; j < rowcnt; j++) {
            if (*lnks++ != 0)
                sumlinks[i]++;
        }
    }
    if (gDebug)
        PrintSumLinks((char*)sumlinks);
    while (true) {
        int32_t bst = -1;
        int32_t bstsum = 0;
        for (i = 0; i < rowcnt; i++) {
            if (output[i] == 0 && (bst == -1 || sumlinks[i] > bstsum)) {
                bstsum = sumlinks[i];
                bst = i;
            }
        }
        if (bst == -1)
            break;
        Outpath(links, outlinks, output, bst);
        while (true) {
            int32_t bstlnks;
            bst = -1;
            bstsum = 0;
            bstlnks = 0;
            for (i = 0; i < rowcnt; i++) {
                if (output[i] == 0 && outlinks[i] >= bstlnks) {
                    if (outlinks[i] > 0 &&
                        (bst == -1 || outlinks[i] > bstlnks ||
                         (outlinks[i] == bstlnks && sumlinks[i] > bstsum))) {
                        bstlnks = outlinks[i];
                        bst = i;
                        bstsum = sumlinks[i];
                    }
                }
            }
            if (bst == -1)
                break;
            Outpath(links, outlinks, output, bst);
        }
    }
}
