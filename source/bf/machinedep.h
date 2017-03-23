/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/
/* machinedep.h - defines machine dependent procedures used by
   buildfont.

history:

Judy Lee: Mon Jul 18 14:13:03 1988
End Edit History
*/

#include "basic.h"

#ifndef MACHINEDEP_H
#define MACHINEDEP_H

#ifdef _WIN32
#include <dir.h>
#include <math.h>
#else
#include <sys/dir.h>
#endif


#define fileerror(p) ferror(p)


extern void set_errorproc( int (*)(int16_t) );

/* delimits str with / or : */
extern void get_filename(
    char *, char *, const char *
);

extern char *CheckBFPath(
char *
);

extern void GetInputDirName(
    char *, char *
);

extern uint32_t ACGetFileSize(
    char *
);

/* Checks if the character set directory exists. */
extern bool DirExists(
    char *, bool, bool, bool
);

extern void  RenameFile(
    char *, char *
);

void FlushLogFiles();
void OpenLogFiles();
typedef int (* includeFile) (const struct direct *);
typedef int (* sortFn)(const struct direct **, const struct direct **);

#ifdef _WIN32
int BFscandir(char* dirName, struct direct ***nameList, includeFile IncludeFile, sortFn Sort);
#else
int BFscandir(const char* dirName, struct direct ***nameList, includeFile IncludeFile, sortFn Sort);
#endif
extern char *GetPathName (
   char *
);


extern int bf_alphasort(const struct direct **f1, const struct direct **f2);

#if defined(_MSC_VER) && ( _MSC_VER < 1800)
float roundf(float x);
#endif


#endif /*MACHINEDEP_H*/
