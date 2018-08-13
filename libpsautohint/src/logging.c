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

void
set_errorproc(int (*userproc)(int16_t))
{
    errorproc = userproc;
}

void
LogMsg(int16_t level, /* error, warning, info */
       int16_t code,  /* exit value - if !OK, this proc will not return */
       char* format,  /* message string */
       ...)
{
    /* "glyphname: message" */
    char str[MAX_GLYPHNAME_LEN + 2 + MAXMSGLEN + 1] = { 0 };
    va_list va;

    if (strlen(gGlyphName) > 0)
        snprintf(str, strlen(gGlyphName) + 3, "%s: ", gGlyphName);

    va_start(va, format);
    vsnprintf(str + strlen(str), MAXMSGLEN, format, va);
    va_end(va);

    if (gLibReportCB != NULL)
        gLibReportCB(str, level);

    if (level == LOGERROR && (code == NONFATALERROR || code == FATALERROR)) {
        (*errorproc)(code);
    }
}
