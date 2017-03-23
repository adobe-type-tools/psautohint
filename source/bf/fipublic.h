/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/
/* This interface is for C-program callers of the font info lookup
 * proc.  The caller of filookup is responsible for also calling
 * fiptrfree to get rid of the storage returned. */

#ifndef FIPUBLIC_H
#define FIPUBLIC_H

#include "basic.h"

#define		NORMAL_RETURN		0
#define		OPTIONAL_NOT_FOUND	1
#define		ERROR_RETURN		2
#define		LINELEN			200
#define		ACOPTIONAL		1
#define		MANDATORY		0


#define DEFAULTBLUEFUZZ FixOne	/* Default value used by PS interpreter and Adobe's fonts
			   to extend the range of alignment zones. */
#define MAXDOMINANTSTEMS 2
#define MAXSTEMSNAP 12
#define MMFINAME "mmfontinfo"
#define FIFILENAME "fontinfo"

typedef struct fontinfo
{
  int16_t exit_status;		/* NORMAL_RETURN = 0 - normal return => */
  /* keyword found and argument ok, */
  /* value_string has value */
  /* OPTIONAL_NOT_FOUND = 1 */
  /* keyword not found, but optional => */
  /* value_string empty */
  /* ERROR_RETURN = 2 - error return =>    keyword not found or argument
     bad, */
  /* value_string has error msg describing problem */
  int16_t value_is_string;	/* 0 means false */
  char *value_string;
} FINODE, *FIPTR;


/* FreeFontInfo frees the memory associated with the pointer ptr.
   This is to reclaim the storage allocated in GetFntInfo.  */
extern void FreeFontInfo(
    char *
);

/* Looks up the value of the specified keyword in the fontinfo
   file.  If the keyword doesn't exist and this is an optional
   key, returns a NULL.	 Otherwise, returns the value string. */
extern char *GetFntInfo(
    char *, bool
);

extern void SetFntInfoFileName(
char *
);

extern void ResetFntInfoFileName(
void
);

extern void ParseIntStems(
char *, bool, int32_t, int *, int32_t*, char *
);

#endif /*FIPUBLIC_H*/
