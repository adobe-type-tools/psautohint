/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/
/* bftoac.h */

/* procedures in AC called from buildfont */

#ifndef BFTOAC_H
#define BFTOAC_H

extern bool AutoColor(bool, bool, bool, bool, bool, bool, bool, bool);

extern bool CreateACTimes (void);

extern void FindCurveBBox(Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed *, Fixed *, Fixed *, Fixed *);

extern bool GetInflectionPoint(Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed *);

extern bool ReadCharFileNames (char *, bool *);

extern void setPrefix(char *);

extern void SetReadFileName(char *);

#endif /*BFTOAC_H*/
