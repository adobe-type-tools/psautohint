/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

/* filookup.c - looks up keywords in the fontinfo file and returns the value. */

#include <assert.h>

#include "ac.h"
#include "buildfont.h"
#include "filookup.h"
#include "fipublic.h"
#include "machinedep.h"

static int
misspace(int c)
{
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
        return 1;
    return 0;
}

static int
misdigit(int c)
{
    return c >= '0' && c <= '9';
}

/* Looks up the value of the specified keyword in the fontinfo
   file.  If the keyword doesn't exist and this is an optional
   key, returns a NULL.  Otherwise, returns the value string. */
extern char*
GetFntInfo(const ACFontInfo* fontinfo, char* keyword, bool optional)
{
    char* returnstring = NULL;
    int i;

    assert(fontinfo != NULL);

    for (i = 0; i < fontinfo->length; i++) {
        if (fontinfo->entries[i].key &&
            !strcmp(fontinfo->entries[i].key, keyword)) {
            returnstring = (char*)AllocateMem(
              (unsigned)strlen(fontinfo->entries[i].value) + 1, sizeof(char),
              "GetFntInfo return str");
            strcpy(returnstring, fontinfo->entries[i].value);
            return returnstring;
        }
    }

    if (!optional) {
        sprintf(globmsg, "ERROR: Fontinfo: Couldn't find fontinfo for %s\n",
                keyword);
        LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
    }

    return NULL;
}

/* Appends Aux{H,V}Stems which is optional to StemSnap{H,V} respectively. */
static char*
GetHVStems(const ACFontInfo* fontinfo, char* kw, bool optional)
{
    char *fistr1, *fistr2, *newfistr;
    char *end, *start;

    fistr1 = GetFntInfo(fontinfo,
                        ((STREQ(kw, "AuxHStems")) ? "StemSnapH" : "StemSnapV"),
                        optional);
    fistr2 = GetFntInfo(fontinfo, kw, ACOPTIONAL);
    if (fistr2 == NULL)
        return fistr1;
    if (fistr1 == NULL)
        return fistr2;
    /* Merge two arrays. */
    newfistr = AllocateMem((unsigned)(strlen(fistr1) + strlen(fistr2) + 1),
                           sizeof(char), "Aux stem value");
    end = (char*)strrchr(fistr1, ']');
    end[0] = '\0';
    start = (char*)strchr(fistr2, '[');
    start[0] = ' ';
    sprintf(newfistr, "%s%s", fistr1, fistr2);
    UnallocateMem(fistr1);
    UnallocateMem(fistr2);
    return newfistr;
}

/* This procedure parses the various fontinfo file stem keywords:
   StemSnap{H,V}, Dominant{H,V} and Aux{H,V}Stems.  If Aux{H,V}Stems
   is specified then the StemSnap{H,V} values are automatically
   added to the stem array.  ParseIntStems guarantees that stem values
   are unique and in ascending order.
   If blendstr is NULL it means we just want the values from the
   current font directory and not for all input directories
   (e.g., for a multi-master font).
 */
extern void
ParseIntStems(const ACFontInfo* fontinfo, char* kw, bool optional,
              int32_t maxstems, int* stems, int32_t* pnum, char* blendstr)
{
    char c;
    char* line;
    int val, cnt, i, ix, j, temp, targetCnt = -1, total = 0;
    bool singleint = false;
    int16_t dirCount = (blendstr == NULL ? 1 : GetTotalInputDirs());
    char* initline;

    *pnum = 0;
    for (ix = 0; ix < dirCount; ix++) {
        cnt = 0;
        if (STREQ(kw, "AuxHStems") || STREQ(kw, "AuxVStems"))
            initline = GetHVStems(fontinfo, kw, optional);
        else
            initline = GetFntInfo(fontinfo, kw, optional);
        if (initline == NULL) {
            if (targetCnt > 0) {
                sprintf(globmsg, "The keyword: %s does not have the same "
                                 "number of values\n  in each master design.\n",
                        kw);
                LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
            } else
                continue; /* optional keyword not found */
        }
        line = initline;
        /* Check for single integer instead of matrix. */
        if ((strlen(line) != 0) && (strchr(line, '[') == 0)) {
            singleint = true;
            goto numlst;
        }
        while (true) {
            c = *line++;
            switch (c) {
                case 0:
                    *pnum = 0;
                    UnallocateMem(initline);
                    return;
                case '[':
                    goto numlst;
                default:
                    break;
            }
        }
    numlst:
        while (*line != ']') {
            while (misspace(*line))
                line++; /* skip past any blanks */
            if (sscanf(line, " %d", &val) < 1)
                break;
            if (total >= maxstems) {
                sprintf(globmsg, "Cannot have more than %d values in fontinfo "
                                 "file array: \n  %s\n",
                        (int)maxstems, initline);
                LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
            }
            if (val < 1) {
                sprintf(
                  globmsg,
                  "Cannot have a value < 1 in fontinfo file array: \n  %s\n",
                  line);
                LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
            }
            stems[total++] = val;
            cnt++;
            if (singleint)
                break;
            while (misdigit(*line))
                line++; /* skip past the number */
        }
        /* insure they are in order */
        for (i = *pnum; i < total; i++)
            for (j = i + 1; j < total; j++)
                if (stems[i] > stems[j]) {
                    temp = stems[i];
                    stems[i] = stems[j];
                    stems[j] = temp;
                }
        /* insure they are unique - note: complaint for too many might precede
           guarantee of uniqueness */
        for (i = *pnum; i < total - 1; i++)
            if (stems[i] == stems[i + 1]) {
                for (j = (i + 2); j < total; j++)
                    stems[j - 1] = stems[j];
                total--;
                cnt--;
            }
        if (ix > 0 && (cnt != targetCnt)) {
            UnallocateMem(initline);
            sprintf(globmsg, "The keyword: %s does not have the same number of "
                             "values\n  in each master design.\n",
                    kw);
            LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
        }
        targetCnt = cnt;
        *pnum += cnt;
        UnallocateMem(initline);
    } /* end of for loop */
    if (blendstr == NULL || total == 0)
        return;
    /* Reset pnum so just the first set of snap values is written
       in the top level Private dictionary. */
    *pnum = cnt;
}
