/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */

/*****************************************************************************/
/* This module implements the character list, which is built prior to        */
/* filling in the character table.                                           */
/*****************************************************************************/

#include "buildfont.h"
#include "charlistpriv.h"
#include "charlist.h"
#include "charpath.h"
#include "chartable.h"
#include "derivedchars.h"
#include "transitionalchars.h"
#include "machinedep.h"

#define MAXCLIST 800

static bool charsetexists;
static int maxclist = 0;

static void readcharset(bool);
indx FindCharListEntry(char *);
static indx DumbFindCharListEntry(char *);
static indx FileInCharList(char *);
static void checkduplicates(void);
static void checkfiledups(void);
static void checkcharlist(bool, bool, char *, indx);
static void swap(struct cl_elem *, struct cl_elem *);


static void
readcharset(bool release)
/*****************************************************************************/
/* Put entry for each character into charlist.                               */
/*****************************************************************************/
   {
   FILE *filelist = NULL;
   char cname[MAXCHARNAME];
   char filename[MAXFILENAME];
   char charsetfilename[MAXPATHLEN];
   bool foundone = false;
   int32_t masters;
   int32_t hintDir;

   getcharsetname(charsetfilename);
   charsetexists = (strlen(charsetfilename) > 0);
   if (!charsetexists) return;

#if 0/*DEBUG*/
   {
   char wd[MAXPATHLEN];
   getwd(wd);
   fprintf(OUTPUTBUFF, "open wd:%s\nfile: %s\n", wd, charsetfilename);
   }
#endif /*0*/

#if OLD
   filelist = ACOpenFile(charsetfilename, "r", (release?OPENERROR:OPENWARN));
   if (filelist == NULL) return;
#else

   filelist = ACOpenFile(charsetfilename, "r", OPENOK);
   if (filelist == NULL)
      {
      char charsetdotdot[MAXPATHLEN];

      sprintf(charsetdotdot, "../%s", charsetfilename);
      filelist = ACOpenFile(charsetdotdot, "r", OPENOK);

      if (filelist == NULL)
         {
                                    /* output warning on original name       */
         filelist = ACOpenFile(charsetfilename, "r",
               (release?OPENERROR:OPENWARN));
         return;
         }
      }
#endif

   while ((ReadNames(cname, filename, &masters, &hintDir, filelist)) != NULL)
      {
      AddCharListEntry(cname, filename, masters, hintDir,
            false, false, false, false);
      foundone = true;
      }

   fclose(filelist);

   if (!foundone)
      {
      sprintf(globmsg,
            "There are no character names in the character set file: %s.\n",
            charsetfilename);

      if (release)
         LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
      else
         LogMsg(globmsg, WARNING, OK, true);
      }
   }



extern indx
FindCharListEntry(char *cname)
   {
   indx low = 0;
   indx high = clsize - 1;
   indx mid;
   indx cond;

   while (low <= high)
      {
      mid = (low + high) / 2;
      cond = strcmp(cname, charlist[mid].charname);

      if (cond < 0)
         high = mid - 1;
      else if (cond > 0)
         low = mid + 1;
      else
         return mid;
      }

   return -1;
   }



static indx
DumbFindCharListEntry(char *cname)
   {
   int i;
   for (i = 0; i < clsize; i++)
      {
      if (STREQ(cname, charlist[i].charname))
         return (i);
      }

   return -1;
   }



static indx
FileInCharList(char *fname)
   {
   indx low = 0;
   indx high = clsize - 1;
   indx mid;
   indx cond;

   while (low <= high)
      {
      mid = (low + high) / 2;
      cond = strcmp(fname, charlist[mid].filename);

      if (cond < 0)
         high = mid - 1;
      else if (cond > 0)
         low = mid + 1;
      else
         return mid;
      }

   return -1;
   }



static void
checkduplicates()
   {
   indx i = 0;
   indx j;
   bool duplicate = false;

   while (i < clsize)
      {
      for (j = i+1; j < clsize; j++)
         {
         if (STRNEQ(charlist[i].charname, charlist[j].charname)) break;
                                    /* Not a duplicate.                      */

         sprintf(globmsg,
               "Duplicate character name: %s in the character set.\n",
               charlist[i].charname);
         LogMsg(globmsg, LOGERROR, OK, true);
         duplicate = true;
         }

      i = j;
      }

  if (duplicate)
      LogMsg("BuildFont cannot continue with duplication(s) "
            "in character set.\n", LOGERROR, NONFATALERROR, true);
   }



static void
checkfiledups()
   {
   indx i = 0;
   indx j;
   bool duplicate = false;

   while (i < clsize)
      {
      for (j = i+1; j < clsize; j++)
         {
         if (STRNEQ(charlist[i].filename, charlist[j].filename)) break;
                                    /* Not a duplicate.                      */

         sprintf(globmsg, "Duplicate file name: %s in the character set.\n",
               charlist[i].filename);
         LogMsg(globmsg, LOGERROR, OK, true);
         duplicate = true;
         }

      i = j;
      }

   if (duplicate)
      LogMsg("BuildFont cannot continue with duplicate file name(s) in\n"
            "  character set.\n", LOGERROR, NONFATALERROR, true);
   }



static void
checkcharlist(bool release, bool quiet, char *indir, indx dirix)
/*****************************************************************************/
/* If any non-derived, non-composite file is not found, a warning or error   */
/* is given.                                                                 */
/*****************************************************************************/
   {
   indx i = 0;
   char inname[MAXPATHLEN];
   bool filemissing = false;

   while (i < clsize)
      {
      if (  !charlist[i].composite &&
            !charlist[i].derived &&
            (dirix < charlist[i].masters))  /* for cube */
         {
         if (!charlist[i].transitional)
            {
            get_filename(inname, indir, charlist[i].filename);

            if (!FileExists(inname, false))
               {
               if (!quiet) 
                  {
                  sprintf(globmsg, "The %s file does not exist or"
                        " is not accessible,\n"
                        "  but is in the character set.\n", inname);
                  LogMsg(globmsg, (release?LOGERROR:WARNING), OK, true);
                  }

               filemissing = true;
               }
            }
         }
      i++;
      }

   if (release && filemissing)
      LogMsg("BuildFont cannot continue with missing input file(s).\n", LOGERROR,
            NONFATALERROR, true);
   }



extern void
AddCharListEntry(char *cname, char *fname,
      int32_t masters,                 /* number of masters, 1-16               */
      int32_t hintDir,                 /* index of hint directory for this      */
                                    /* character in dir list                 */
      bool derived, bool composite, bool inBez, bool transitional)
   {
   indx fnlen;
   indx i;

   if (maxclist == 0)
      {
      charlist = (struct cl_elem *)AllocateMem(MAXCLIST,
            sizeof(struct cl_elem), "character list");
      maxclist = MAXCLIST;

      for (i = 0; i < maxclist; i++)
         charlist[i].inCharTable = charlist[i].inBezDir = false;

      clsize = 0;
      }

   if (clsize >= maxclist)
      {
      maxclist += 100;
      charlist = (struct cl_elem *) ReallocateMem((char *) charlist,
            (unsigned)(maxclist * sizeof(struct cl_elem)), "character list");

      for (i = clsize; i < maxclist; i++)
         charlist[i].inCharTable = charlist[i].inBezDir = false;
      }

   if (DumbFindCharListEntry(cname) >= 0)
      return;                       /* already in table, probably as a       */
                                    /* transitional character                */

   charlist[clsize].charname = AllocateMem((unsigned)(strlen(cname)+1),
         sizeof(char), "character name");
   strcpy(charlist[clsize].charname, cname);

   if ((fname != NULL) && ((fnlen=strlen(fname)) > 0))
      {
      charlist[clsize].filename = AllocateMem((unsigned)(fnlen+1),
            sizeof(char), "file name");
      strcpy(charlist[clsize].filename, fname);
      }
   else
      charlist[clsize].filename = charlist[clsize].charname;

   charlist[clsize].derived = derived;
   charlist[clsize].composite = composite;
   charlist[clsize].inCharTable = false;
   charlist[clsize].inBezDir = inBez;
   charlist[clsize].masters = masters;
   charlist[clsize].hintDir = hintDir;
   charlist[clsize].transitional = transitional;
   clsize++;
   }



extern bool
CheckCharListEntry(char *cname, char *fname, bool derived,
      bool composite, bool transitional, bool release, indx dirIx)
   {
   bool error = false;
   indx cli = FindCharListEntry(cname);

   if (cli < 0)
      {
      if (release && derived)
         {
         sprintf(globmsg, "Derived character: %s is not found"
               " in the character set.\n", cname);
         LogMsg(globmsg, LOGERROR, OK, true);
         error = true;
         }

      else if (release && transitional)
         {
         sprintf(globmsg, "Transitional character: %s is not found"
               " in the character set.\n", cname);
         LogMsg(globmsg, LOGERROR, OK, true);
         error = true;
         }

      else if (charsetexists)
         {
         if (transitional)
            sprintf(globmsg, "Transitional character: %s is not found"
                  " in the character set.\n", cname);
         if (derived)
            sprintf(globmsg, "Derived character: %s is not found"
                  " in the character set.\n", cname);
         if (composite)
            sprintf(globmsg, "Composite character: %s is not found"
                  " in the character set.\n", cname);

         LogMsg(globmsg, WARNING, OK, true);
         }

      return error;
      }

   if (charlist[cli].transitional && transitional && (dirIx == 0))
      {
      sprintf(globmsg, "%s is a duplicate transitional character name.\n",
            charlist[cli].charname);
      LogMsg(globmsg, LOGERROR, OK, true);
      error = true;
      }

   if (charlist[cli].derived && derived && (dirIx == 0))
      {
      sprintf(globmsg, "%s is a duplicate derived character name.\n",
            charlist[cli].charname);
      LogMsg(globmsg, LOGERROR, OK, true);
      error = true;
      }

   if (charlist[cli].composite && composite && (dirIx == 0))
      {
      sprintf(globmsg, "%s is a duplicate composite character name.\n",
            charlist[cli].charname);
      LogMsg(globmsg, LOGERROR, OK, true);
      error = true;
      }

   charlist[cli].derived = charlist[cli].derived || derived;
   charlist[cli].transitional = charlist[cli].transitional || transitional;
   charlist[cli].composite = charlist[cli].composite || composite;

   if (charlist[cli].derived && charlist[cli].composite)
      {
      sprintf(globmsg, "Character name %s was defined more than once as"
            " a derived or composite character .\n", charlist[cli].charname);
      LogMsg(globmsg, LOGERROR, OK, true);
      error = true;
      }

   if ((strlen(fname) > 0) && STRNEQ(charlist[cli].filename, fname))
      {
      sprintf(globmsg, "%s character has filename %s in character set and \n"
            "  filename %s in %s file.\n", charlist[cli].charname,
            charlist[cli].filename, fname,
            charlist[cli].derived ? DERIVEDCHARFILENAME : COMPFILE);
      LogMsg(globmsg, LOGERROR, OK, true);
      error = true;
      }

   return error;
   }



static void
swap(struct cl_elem *c1, struct cl_elem *c2)
   {
   struct cl_elem temp;

   temp = *c1;
   *c1 = *c2;
   *c2 = temp;
   }



extern void
sortcharlist(bool byfilename)
/*****************************************************************************/
/* sort the charlist by character name                                       */
/*****************************************************************************/
   {
   indx gap, i, j;

   for (gap = clsize / 2; gap > 0; gap = gap / 2)
      for (i = gap; i < clsize; i++)
         for (j = i - gap; j >= 0; j = j - gap)
            {
            if (!byfilename)
               {
               if (strcmp(charlist[j].charname, charlist[j + gap].charname) < 0)
                  break;
               }
            else if (strcmp(charlist[j].filename, charlist[j+gap].filename) < 0)
               break;

            swap(&charlist[j], &charlist[j+gap]);
            }
   }



/*****************************************************************************/
/* Put entry for each character into charlist.                               */
/*****************************************************************************/
/*extern void
makecharlist(bool release, bool quiet, char *indir, indx dirix)
   {
   indx i;

   if (dirix == 0)
      {
      if (maxclist == 0)
         {
         charlist = (struct cl_elem *) AllocateMem(MAXCLIST,
               sizeof(struct cl_elem), "character list");
         maxclist = MAXCLIST;

         for (i = 0; i < maxclist; i++)
            charlist[i].inCharTable = charlist[i].inBezDir = false;

         clsize = 0;
         }

      readcharset(release);
      }

   sortcharlist(false);
   if (dirix == 0) checkduplicates();
   readderived(release, dirix);
   readcomposite(release, dirix);
   checkcharlist(release, quiet, indir, dirix);
   sortcharlist(true);
   checkfiledups();
   }


*/
extern bool
NameInCharList(char *fname)
   {
   return (FileInCharList(fname) >= 0);
   }


/*
extern void
CopyCharListToTable()
   {
   indx i;

   for (i = 0; i < clsize; i++)
      if (  !charlist[i].composite &&
            !charlist[i].derived &&
            !charlist[i].transitional &&
            charlist[i].inCharTable)
         create_entry(charlist[i].charname, charlist[i].filename, UNINITWIDTH,
               false, 0, charlist[i].masters, charlist[i].hintDir);
   }

*/

extern void
CheckAllListInTable(bool release)
   {
   indx i;
   bool onemissing = false;

   for (i = 0; i < clsize; i++)
      {
      if (  charlist[i].derived
         || charlist[i].composite
         || charlist[i].transitional
         || charlist[i].inBezDir) continue;

      sprintf(globmsg, "Character: %s is in character set,"
            " but was not found in the\n  %s directory.\n",
            charlist[i].charname, bezdir);
      LogMsg(globmsg, (release?LOGERROR:WARNING), OK, true);
      onemissing = true;
      }

   if (release && onemissing)
      LogMsg("BuildFont cannot continue with missing character(s).\n", 
            LOGERROR, NONFATALERROR, true);
   }

extern bool IsInFullCharset(char *bezName);

extern bool
CompareCharListToBez(char *fname, bool release, indx dirix, bool quiet)
   {
   indx i = FileInCharList(fname);
   char filename[MAXPATHLEN];

   get_filename(filename, bezdir, fname);
   if (i < 0) /* fname not found in charlist */
      {
      if (release)
         {
         if (!UsesSubset() || !IsInFullCharset(fname))
            {
            sprintf(globmsg, "File name: %s in %s directory will not\n"
                  "  be included in the font because it is not"
                  " in the character set.\n", fname, bezdir);

            LogMsg(globmsg, WARNING, OK, true);
            }
         }
      else
         {
         /********************************************************************/
         /* Check if this is a directory since we already know it exists in  */
         /* the bez directory.                                               */
         /********************************************************************/
         if (FileExists(filename, true))
            {
            if (dirix == 0)
               {
               if (charsetexists)
                  {
                  /* if subset: if charname "fname" is in layouts,
                     then do not issue this message. */
                  if (!UsesSubset() || !IsInFullCharset(fname))
                     {
		     if (!quiet) 
		     {
                     sprintf(globmsg,
                           "Extra character file: %s in %s directory will\n"
                           "  be included in the font.\n", fname, bezdir);
                     LogMsg(globmsg, WARNING, OK, true);
		     }
                     }
                  }

               AddCharListEntry(fname, fname, GetTotalInputDirs(),
                     GetHintsDir(), false, false, true, false);
               sortcharlist(true);
               }
            }
         }

      return false;
      }

   if (charlist[i].composite) return false;

   if (charlist[i].derived)
      {
      sprintf(globmsg, "File name: %s in %s directory\n"
            "  is the same as a derived file name.\n", fname, bezdir);
      LogMsg(globmsg, LOGERROR, OK, true);
      return true;
      }

   /**************************************************************************/
   /* don't worry about transitional                                         */
   /**************************************************************************/
   if (!FileExists(filename, false))
      return false;                 /* Error msg will be issued in           */
                                    /* CheckAllListInTable or checkcharlist. */
   charlist[i].inBezDir = true;
   return false;
   }



extern void
ResetCharListBools(bool firstTime, indx dirix)
/*****************************************************************************/
/* This proc is called to compare the inBezDir and inCharTable booleans.     */
/* Its purpose is to ensure that only characters in every input directory    */
/* are put into the chartable.  If a character is in the bez directory and   */
/* this is the first time thru then inCharTable will be set to true.         */
/* Subsequent calls must have both inBezDir and inCharTable true for         */
/* inCharTable to stay true.                                                 */
/*****************************************************************************/
   {
   indx i;

   for (i = 0; i < clsize; i++)
      {
      if (charlist[i].masters <= dirix) continue;
                                    /* added for cube                        */
      if (charlist[i].inBezDir)
         {
         if (!charlist[i].inCharTable && firstTime)
            charlist[i].inCharTable = true;
         }
      else
         charlist[i].inCharTable = false;

      charlist[i].inBezDir = false;  
      }
   }



extern void
FreeCharList()
   {
   indx i;
   for (i = 0; i < clsize; i++)
      {
      if (charlist[i].filename != charlist[i].charname)
		{
         if (charlist[i].charname)
			 UnallocateMem(charlist[i].charname);
		}

      if (charlist[i].filename)
		  UnallocateMem(charlist[i].filename);
      }

   UnallocateMem(charlist);
   clsize = 0;
   }
