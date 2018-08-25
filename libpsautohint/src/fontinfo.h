/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

/* This interface is for C-program callers of the font info lookup
 * proc.  The caller of filookup is responsible for also calling
 * fiptrfree to get rid of the storage returned. */

#ifndef AC_FONTINFO_H_
#define AC_FONTINFO_H_

#include "ac.h"
#include "basic.h"

#define		OPTIONAL		true
#define		MANDATORY		false

/* Default value used by PS interpreter and Adobe's fonts to extend the range
 * of alignment zones. */
#define DEFAULTBLUEFUZZ FixOne

ACFontInfo* ParseFontInfo(const char* data);
void FreeFontInfo(ACFontInfo* fontinfo);

bool ReadFontInfo(const ACFontInfo* fontinfo);

#endif /* AC_FONTINFO_H_ */
