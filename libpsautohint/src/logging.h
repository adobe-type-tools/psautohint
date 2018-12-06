/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "psautohint.h"

#include "basic.h"

#ifndef BF_LOGGING_H_
#define BF_LOGGING_H_

/* defines for LogMsg code param */
#define OK 0
#define NONFATALERROR 1
#define FATALERROR 2

/* defines for LogMsg level param */
#define LOGDEBUG  AC_LogDebug
#define INFO      AC_LogInfo
#define WARNING   AC_LogWarning
#define LOGERROR  AC_LogError

/* maximum message length */
#define MAXMSGLEN 500

/* global log function which is supplied by the following */
extern AC_REPORTFUNCPTR gLibReportCB;

void LogMsg(int16_t, int16_t, char *, ...);

void set_errorproc( int (*)(int16_t) );

#endif /* BF_LOGGING_H_ */
