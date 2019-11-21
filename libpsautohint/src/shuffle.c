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

static int32_t rowcnt;

unsigned char*
InitShuffleSubpaths(void)
{
    int32_t cnt = -1;
    PathElt* e = gPathStart;
    while (e != NULL) { /* every element is marked with its subpath count */
        if (e->type == MOVETO)
            cnt++;
        if (e->type == MOVETO) {
            LogMsg(LOGDEBUG, OK, "subpath %d starts at %g %g.", cnt,
                   FixToDbl(e->x), FixToDbl(-e->y));
        }
        e->count = (int16_t)cnt;
        e = e->next;
    }
    cnt++;
    rowcnt = cnt;
    if (cnt < 4 || cnt >= MAXCNT)
        return NULL;
    return Alloc(cnt * cnt);
}

static void
PrintLinks(unsigned char* links)
{
    int32_t i, j;
    LogMsg(LOGDEBUG, OK, "Links ");
    for (i = 0; i < rowcnt; i++) {
        LogMsg(LOGDEBUG, OK, "%d  ", i);
        if (i < 10)
            LogMsg(LOGDEBUG, OK, " ");
    }
    LogMsg(LOGDEBUG, OK, "\n");
    for (i = 0; i < rowcnt; i++) {
        LogMsg(LOGDEBUG, OK, " %d   ", i);
        if (i < 10)
            LogMsg(LOGDEBUG, OK, " ");
        for (j = 0; j < rowcnt; j++) {
            LogMsg(LOGDEBUG, OK, "%d   ", links[rowcnt * i + j]);
        }
        LogMsg(LOGDEBUG, OK, "\n");
    }
}

static void
PrintSumLinks(char* sumlinks)
{
    int32_t i;
    LogMsg(LOGDEBUG, OK, "Sumlinks ");
    for (i = 0; i < rowcnt; i++) {
        LogMsg(LOGDEBUG, OK, "%d  ", i);
        if (i < 10)
            LogMsg(LOGDEBUG, OK, " ");
    }
    LogMsg(LOGDEBUG, OK, "\n");
    LogMsg(LOGDEBUG, OK, "         ");
    for (i = 0; i < rowcnt; i++) {
        LogMsg(LOGDEBUG, OK, "%d   ", sumlinks[i]);
    }
    LogMsg(LOGDEBUG, OK, "\n");
}

static void
PrintOutLinks(unsigned char* outlinks)
{
    int32_t i;
    LogMsg(LOGDEBUG, OK, "Outlinks ");
    for (i = 0; i < rowcnt; i++) {
        LogMsg(LOGDEBUG, OK, "%d  ", i);
        if (i < 10)
            LogMsg(LOGDEBUG, OK, " ");
    }
    LogMsg(LOGDEBUG, OK, "\n");
    LogMsg(LOGDEBUG, OK, "         ");
    for (i = 0; i < rowcnt; i++) {
        LogMsg(LOGDEBUG, OK, "%d   ", outlinks[i]);
    }
    LogMsg(LOGDEBUG, OK, "\n");
}

void
MarkLinks(HintVal* vL, bool hFlg, unsigned char* links)
{
    int32_t i, j;
    HintSeg* seg;
    PathElt* e;
    if (links == NULL)
        return;
    for (; vL != NULL; vL = vL->vNxt) {
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
        if (hFlg)
            ShowHVal(vL);
        else
            ShowVVal(vL);
        LogMsg(LOGDEBUG, OK, " : %d <-> %d", i, j);
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
    PathElt* e = gPathStart;
    while (e != NULL) {
        if (e->count == i)
            break;
        e = e->next;
    }
    if (e == NULL)
        return;
    MoveSubpathToEnd(e);
    LogMsg(LOGDEBUG, OK, "move subpath %d to end.", bst);
    output[bst] = 1;
    lnks = &links[bst * rowcnt];
    outlnks = outlinks;
    for (i = 0; i < rowcnt; i++)
        *outlnks++ += *lnks++;
    PrintOutLinks(outlinks);
}

/* The intent of this code is to order the subpaths so that
 the hints will not need to change constantly because it
 is jumping from one subpath to another.  Kanji glyphs
 had the most problems with this which caused huge files
 to be created. */
void
DoShuffleSubpaths(unsigned char* links)
{
    unsigned char sumlinks[MAXCNT], output[MAXCNT], outlinks[MAXCNT];
    unsigned char* lnks;
    int32_t i, j;
    memset(sumlinks, 0, MAXCNT * sizeof(unsigned char));
    memset(output, 0, MAXCNT * sizeof(unsigned char));
    memset(outlinks, 0, MAXCNT * sizeof(unsigned char));
    if (links == NULL)
        return;
    PrintLinks(links);
    for (i = 0; i < rowcnt; i++)
        output[i] = sumlinks[i] = outlinks[i] = 0;
    lnks = links;
    for (i = 0; i < rowcnt; i++) {
        for (j = 0; j < rowcnt; j++) {
            if (*lnks++ != 0)
                sumlinks[i]++;
        }
    }
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
