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


#define fileeof(p) feof(p)
#define fileerror(p) ferror(p)


#define ACFILE ".ACLOCK" /* zero length file to indicate that AC
			is running in current directory. */


#define BFFILE ".BFLOCK" /* zero length file to indicate that buildfont
			is running in current directory. */

extern void FlushLogMsg(void);

extern bool createlockfile (
    char *, char *
);


extern void set_errorproc( int (*)(int16_t) );


extern void closefiles(
    void
);

extern void get_filedelimit(
    char *
);

extern void get_time(time_t *);

/* Gets the date and time. */
extern void get_datetime(
    char *
);

/* delimits str with / or : */
extern void get_filename(
    char *, char *, const char *
);

/* Returns name of AFM file. */
extern void get_afm_filename(
    char *
);

/* Returns name of IBM printer file. */
extern void get_ibm_fontname(
    char *
);

/* Returns name of Mac printer file. */
extern void get_mac_fontname(
    char *
);



/* Returns the full path name given ref num and dirID.
   If dirID is MININT then it returns the pathname of
   the current working directory. On UNIX it always
   returns the current working directory. */
extern void GetFullPathname(
char *, int16_t, int32_t
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

/* Checks for the existence of the specified file. */
extern bool FileExists(
    const char *, int16_t
);

/* Checks for the existence of the specified file. */
extern bool CFileExists(
    const char *, int16_t
);

extern void MoveDerivedFile(
char *, char *, char *, char *
);

/* Creates the resource file for the Macintosh downloadable printer font. */
extern void CreateResourceFile(
    char *
);

extern void  RenameFile(
    char *, char *
);

extern void set_current_dir(
    char *
);

extern void get_current_dir(
    char *
);

extern void  SetMacFileType(
    char *, char *
);

extern void ScanBezFiles(
    bool, indx, bool
);

extern void ScanDirFiles(
    bool, char *
);

extern void AppendFile (
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
