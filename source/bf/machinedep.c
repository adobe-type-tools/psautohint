/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

/*

history:

JLee	April 8, 1988

Judy Lee: Wed Jul  6 17:55:30 1988
End Edit History
*/

#include "machinedep.h"
#include "ac.h"
#include "fipublic.h"

static int16_t warncnt = 0;

/* proc to be called from LogMsg if error occurs */
static int (*errorproc)(int16_t);

/* used for cacheing of log messages */
static char lastLogStr[MAXMSGLEN + 1] = "";
static int16_t lastLogLevel = -1;
static bool lastLogPrefix;
static int logCount = 0;

static void LogMsg1(char* str, int16_t level, int16_t code, bool prefix);

#define Write(s)                                                               \
    {                                                                          \
        if (libReportCB != NULL)                                               \
            libReportCB(s);                                                    \
    }
#define WriteWarnorErr(f, s)                                                   \
    {                                                                          \
        if (libErrorReportCB != NULL)                                          \
            libErrorReportCB(s);                                               \
    }

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
        LogMsg1(lastLogStr, lastLogLevel, OK, lastLogPrefix);
    } else if (logCount > 1) {
        char newStr[MAXMSGLEN];
        sprintf(newStr, "The last message (%.20s...) repeated %d more times.\n",
                lastLogStr, logCount);
        LogMsg1(newStr, lastLogLevel, OK, true);
    }
    logCount = 0;
}

void
LogMsg(
  char* str,     /* message string */
  int16_t level, /* error, warning, info */
  int16_t code,  /* exit value - if !OK, this proc will not return */
  bool prefix /* prefix message with LOGERROR: or WARNING:, as appropriate */)
{
    /* changed handling of this to be more friendly (?) jvz */
    if (strlen(str) > MAXMSGLEN) {
        LogMsg1("The following message was truncated.\n", WARNING, OK, true);
        ++warncnt;
    }
    if (level == WARNING)
        ++warncnt;
    if (!strcmp(str, lastLogStr) && level == lastLogLevel) {
        ++logCount;   /* same message */
    } else {          /* new message */
        if (logCount) /* messages pending */
            FlushLogMsg();
        LogMsg1(str, level, code, prefix); /* won't return if LOGERROR */
        strncpy(lastLogStr, str, MAXMSGLEN);
        lastLogLevel = level;
        lastLogPrefix = prefix;
    }
}

static void
LogMsg1(char* str, int16_t level, int16_t code, bool prefix)
{
    switch (level) {
        case INFO:
            Write(str);
            break;
        case WARNING:
            if (prefix)
                WriteWarnorErr(stderr, "WARNING: ");
            WriteWarnorErr(stderr, str);
            break;
        case LOGERROR:
            if (prefix)
                WriteWarnorErr(stderr, "ERROR: ");
            WriteWarnorErr(stderr, str);
            break;
        default:
            WriteWarnorErr(stderr, "ERROR - log level not recognized: ");
            WriteWarnorErr(stderr, str);
            break;
    }
    if (level == LOGERROR && (code == NONFATALERROR || code == FATALERROR)) {
        (*errorproc)(code);
    }
}

#if defined(_MSC_VER) && (_MSC_VER < 1800)
float
roundf(float x)
{
    float val = (float)((x < 0) ? (ceil((x)-0.5)) : (floor((x) + 0.5)));
    return val;
}
#endif
