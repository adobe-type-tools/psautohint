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

#ifndef FIPUBLIC_H
#define FIPUBLIC_H

#include "ac.h"
#include "basic.h"

#define		ACOPTIONAL		1
#define		MANDATORY		0


#define DEFAULTBLUEFUZZ FixOne	/* Default value used by PS interpreter and Adobe's fonts
			   to extend the range of alignment zones. */

/* Looks up the value of the specified keyword in the fontinfo
   file.  If the keyword doesn't exist and this is an optional
   key, returns a NULL.	 Otherwise, returns the value string. */
char* GetFntInfo(const ACFontInfo*, char*, bool);

void ParseIntStems(const ACFontInfo* fontinfo, char*, bool, int32_t, int*,
                   int32_t*);

#endif /*FIPUBLIC_H*/
