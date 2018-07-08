/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include <stdarg.h>

#include "ac.h"

AC_REPORTFUNCPTR gLibReportCB = NULL;

/* proc to be called from LogMsg if error occurs */
static int (*errorproc)(int16_t);

/* used for cacheing of log messages */
static char lastLogStr[MAXMSGLEN + 1] = "";
static int16_t lastLogLevel = -1;
static int logCount = 0;

static void LogMsg1(char* str, int16_t level, int16_t code);

void
set_errorproc(int (*userproc)(int16_t))
{
    errorproc = userproc;
}

/* called by LogMsg and when exiting (tidyup) */
static void
FlushLogMsg(void)
{
    /* if message happened exactly 2 times, don't treat it specially */
    if (logCount == 1) {
        LogMsg1(lastLogStr, lastLogLevel, OK);
    } else if (logCount > 1) {
        char newStr[MAXMSGLEN + 1];
        snprintf(newStr, MAXMSGLEN,
                 "The last message (%.20s...) repeated %d more times.\n",
                 lastLogStr, logCount);
        LogMsg1(newStr, lastLogLevel, OK);
    }
    logCount = 0;
}

void
LogMsg(int16_t level, /* error, warning, info */
       int16_t code,  /* exit value - if !OK, this proc will not return */
       char* format,  /* message string */
       ...)
{
    char str[MAXMSGLEN + 1];
    va_list va;

    va_start(va, format);
    vsnprintf(str, MAXMSGLEN, format, va);
    va_end(va);

    if (!strcmp(str, lastLogStr) && level == lastLogLevel) {
        ++logCount;   /* same message */
    } else {          /* new message */
        if (logCount) /* messages pending */
            FlushLogMsg();
        LogMsg1(str, level, code); /* won't return if LOGERROR */
        strncpy(lastLogStr, str, MAXMSGLEN);
        lastLogLevel = level;
    }
}

static void
LogMsg1(char* str, int16_t level, int16_t code)
{
    if (gLibReportCB != NULL)
        gLibReportCB(str, level);

    if (level == LOGERROR && (code == NONFATALERROR || code == FATALERROR)) {
        (*errorproc)(code);
    }
}
