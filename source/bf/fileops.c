/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/

#include "buildfont.h"
#include "afmcharsetdefs.h"
#include "cryptdefs.h"
#include "fipublic.h"
#include "machinedep.h"
#include "charpath.h"

#define BAKSUFFIX ".BAK"

static char charsetDir[MAXPATHLEN];
static char charsetname[MAXPATHLEN];
static char charsetPath[MAXPATHLEN];
static int16_t total_inputdirs = 1;   /* number of input directories           */
static int32_t MaxBytes;
char globmsg[MAXMSGLEN + 1];        /* used to format messages               */
static CharsetParser charsetParser;
static char charsetLayout[MAXPATHLEN];
static char initialWorkingDir[MAXPATHLEN];
static int initialWorkingDirLength;


#if IS_LIB
typedef void *(*AC_MEMMANAGEFUNCPTR)(void *ctxptr, void *old, uint32_t size);
extern AC_MEMMANAGEFUNCPTR AC_memmanageFuncPtr;
extern void *AC_memmanageCtxPtr;
#endif


typedef struct
   {
   int lo;
   int hi;
   } SubsetData;

extern bool multiplemaster; /* from buildfont.c */

extern int16_t strindex(s, t)         /* return index of t in s, -1 if none    */
char *s, *t;
{
  indx i, n;

  n = (indx)strlen(t);
  for (i = 0; s[i] != '\0'; i++)
    if (!strncmp(s + i, t, n))
      return i;
  return -1;
}


char *AllocateMem(unsigned int nelem, unsigned int elsize, const char *description)
{
#if IS_LIB
 char *ptr = (char *)AC_memmanageFuncPtr(AC_memmanageCtxPtr, NULL, nelem * elsize);
 if (NULL != ptr)
	 memset((void *)ptr, 0x0, nelem * elsize);
#else
  char *ptr = (char *)calloc(nelem, elsize);
#endif
  if (ptr == NULL)
  {
    sprintf(globmsg, "Cannot allocate %d bytes of memory for %s.\n",
      (int) (nelem * elsize), description);
    LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
  }
  return (ptr);
}

char *ReallocateMem(char *ptr, unsigned int size, const char *description)
{
#if IS_LIB
 char *newptr = (char *)AC_memmanageFuncPtr(AC_memmanageCtxPtr, (void *)ptr, size);
#else
  char *newptr = (char *)realloc(ptr, size);
#endif
  if (newptr == NULL)
  {
    sprintf(globmsg, "Cannot allocate %d bytes of memory for %s.\n",
      (int) size, description);
    LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
  }
  return (newptr);
}

void UnallocateMem(void *ptr)
{
#if IS_LIB
	AC_memmanageFuncPtr(AC_memmanageCtxPtr, (void *)ptr, 0);
#else
	if (ptr != NULL) {free((char *) ptr); ptr = NULL;}
#endif
}

/* ACOpenFile tries to open a file with the access attribute
   specified.  If fopen fails an error message is printed
   and the program exits if severity = FATAL. */
extern FILE *ACOpenFile(char * filename, char *access, int16_t severity)
{
  FILE *stream;
  char dirname[MAXPATHLEN];

  stream = fopen(filename, access);
  if (stream == NULL) {
    GetInputDirName(dirname,"");
    switch (severity) {
    case (OPENERROR):
      sprintf(globmsg, "The %s file does not exist or is not accessible (currentdir='%s').\n", filename, dirname);
      LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
      break;
    case (OPENWARN):
      sprintf(globmsg, "The %s file does not exist or is not accessible (currentdir='%s').\n", filename, dirname);
      LogMsg(globmsg, WARNING, OK, true);
      break;
    default:
      break;
    }
  }
  return (stream);
}

extern void FileNameLenOK(filename)
char *filename;
{
  if (strlen(filename) >= MAXFILENAME)
  {
    sprintf(globmsg, "File name: %s exceeds max allowable length of %d.\n",
      filename, (int) MAXFILENAME);
    LogMsg(globmsg, LOGERROR, FATALERROR, true);
  }
}

extern void CharNameLenOK(charname)
char *charname;
{
  if (strlen(charname) >= MAXCHARNAME)
  {
    sprintf(globmsg,
      "Character name: %s exceeds maximum allowable length of %d.\n",
      charname, (int) MAXCHARNAME);
    LogMsg(globmsg, LOGERROR, FATALERROR, true);
  }
}

extern bool BAKFile(filename)
char *filename;
{
  int16_t length = (int16_t)strlen(filename);
  if (length <= 4) return false;
  if (!strcmp(&filename[length-4], BAKSUFFIX))
    return true;
  return false;
}

extern int32_t GetMaxBytes()
{
  return MaxBytes;
}

extern void SetMaxBytes (value)
int32_t value;
{
  MaxBytes = value;
}

/* Sets the global variable charsetDir. */
extern void set_charsetdir(dirname)
char *dirname;
{
  strcpy(charsetDir, dirname);
}

extern void getcharsetname(csname)
char *csname;
{
  strcpy (csname, charsetname);
}

void
SetCharsetParser(char *fileTypeString)
   {
   if (fileTypeString == NULL)
      charsetParser = bf_CHARSET_STANDARD;
   else if (STREQ(fileTypeString, "CID"))
      charsetParser = bf_CHARSET_CID;
   else
      charsetParser = bf_CHARSET_STANDARD;
   }

CharsetParser
GetCharsetParser()
   {
   return charsetParser;
   }

void
SetCharsetLayout(char *fontinfoString)
   {
   strcpy(charsetLayout, (fontinfoString == NULL)
         ? BF_STD_LAYOUT : fontinfoString);
   }

char *
GetCharsetLayout()
   {
   return charsetLayout;
   }

#define INCREMENT 3
static SubsetData *subsetdata = NULL;
static int allocated = 0;
static bool usesSubset = false;

bool
UsesSubset(void)
   {
   return usesSubset;
   }

bool
InSubsetData(int cid)
   {
   int i;

   for (i = 0; i < allocated; i++)
      {
      if ((cid >= subsetdata[i].lo) && (cid <= subsetdata[i].hi))
         return true;
      }

   return false;
   }

extern char *GetFItoAFMCharSetName()
{
  char *filename;
  
  if (strlen(charsetDir) == 0)
    return NULL; 
  filename = AllocateMem((unsigned int)(strlen(charsetDir) + strlen(AFMCHARSETTABLE) + 2),
    sizeof(char), "AFM charset filename");
  get_filename(filename, charsetDir, AFMCHARSETTABLE);
  return filename;
}

extern int16_t GetTotalInputDirs()
{
  return total_inputdirs;
}

extern void SetTotalInputDirs(int16_t total)
{
  total_inputdirs = total;
}

/* insures that the meaningful data in the buffer is terminated
 * by an end of line marker and the null terminator for a string. */
extern int32_t ACReadFile(textptr, fd, filename, filelength)
char *textptr;
FILE *fd;
char *filename;
int32_t filelength;
{
  int32_t cc;

  cc = ReadDecFile(
    fd, filename, textptr, true, MAXINT, (uint32_t) (filelength),
    OTHER);
  fclose(fd);
  if (textptr[cc - 1] != NL || textptr[cc - 1] != '\r')
    textptr[cc++] = NL;
  textptr[cc] = '\0';
  return ((int32_t) cc); 
}           /* ACReadFile */

bool
IsHintRowMatch(char *hintDirName, char *rowDirName)
   {
   char fontDirName[MAXPATHLEN];
   int fontDirLength;

   /**************************************************************************/
   /* It is necessary to concatenate the hintDirName and rowDirName when     */
   /* doing comparisons because each of these may involve multiple path      */
   /* components: a/b and x/y/z -> a/b/x/y/z                                 */
   /**************************************************************************/
   sprintf(fontDirName, "%s/%s/", hintDirName, rowDirName);
   fontDirLength = (int)strlen(fontDirName);
   return ( (initialWorkingDirLength >= fontDirLength) &&
            (strcmp(initialWorkingDir + initialWorkingDirLength - fontDirLength,
               fontDirName) == 0));
   }

bool
IsInFullCharset(char *bezName)
   {
   FILE *charsetFile;
   char line[501];
   char hintDirName[128];
   char rowDirName[128];
   char cname[128];

   if ((charsetFile = ACOpenFile(charsetPath, "r", OPENOK)) == NULL)
      return false;
   while (fgets(line, 500, charsetFile) != NULL)
      {
      switch (charsetParser)
         {
      case bf_CHARSET_CID:
         if (  (sscanf(line, "%*d%s%s%s", hintDirName, rowDirName, cname)
                  == 3) &&
               IsHintRowMatch(hintDirName, rowDirName) &&
               (STREQ(cname, bezName)))
            {
            fclose(charsetFile);
            return true;
            }
         break;
      case bf_CHARSET_STANDARD:
      default:
         if (  (sscanf(line, "%s", cname) == 1) &&
               (STREQ(cname, bezName)))
            {
            fclose(charsetFile);
            return true;
            }
         break;
         }
      }

   fclose(charsetFile);
   return false;
   }

/*****************************************************************************/
/* ReadNames is used for reading each successive character name from the     */
/* character set file.   It returns next free location for characters.       */
/*****************************************************************************/
extern char *
ReadNames(char *cname, char *filename, int32_t *masters, int32_t *hintDir,
      FILE *stream)
   {
#ifdef IS_GGL
	/* This should never be called with the GGL */
	return NULL;
#else
   int total_assigns;
   char line[501];
   static char *result;
   int done = 0;
   int cid;
   char hintDirName[128];
   char rowDirName[128];
/*   char fontDirName[256]; */
/*   int workingDirLength; */
/*   int fontDirLength; */

   while (!done && ((result = fgets(line, 500, stream)) != NULL))
      {
      switch (charsetParser)
         {
      case bf_CHARSET_CID:
         total_assigns = sscanf(line, "%d%s%s%s",
               &cid, hintDirName, rowDirName, cname);
         if (total_assigns == 4)
            {
            if (IsHintRowMatch(hintDirName, rowDirName))
               {
		 if (subsetdata)  {
		   if (InSubsetData(cid)) {
		     strcpy(filename, cname);
		     *masters = GetTotalInputDirs();
		     *hintDir = GetHintsDir();
		     done = 1;
		   }
		   else
		     done = 0;
		 }
		 else {
		   strcpy(filename, cname);
		   *masters = GetTotalInputDirs();
		   *hintDir = GetHintsDir();
		   done = 1;
		 }
               }
            }
         break;

      case bf_CHARSET_STANDARD:
      default:
         total_assigns = sscanf(line, " %s %s %d %d",
               cname, filename, masters, hintDir);
         if (total_assigns >= 1)
            {
            CharNameLenOK(cname);

            if (total_assigns == 1) strcpy(filename, cname);

	    if (multiplemaster) {
	      FileNameLenOK(filename);

	      if (total_assigns < 3) *masters = GetTotalInputDirs();

	      if (total_assigns < 4)
		*hintDir = GetHintsDir();
	      else
		--*hintDir; /* hintDir is zero-origin */
	    }
	    else {
	      strcpy(filename, cname);
	      *masters = 1;
	      *hintDir = 0;
	    }

            done = 1;
            }
         break;
         }
      }

   return result;
#endif
   }
