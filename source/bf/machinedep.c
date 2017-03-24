/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/
/* machinedep.c

history:

JLee	April 8, 1988

Judy Lee: Wed Jul  6 17:55:30 1988
End Edit History
*/
#include <fcntl.h>      /* Needed only for _O_RDWR definition */
#include <errno.h>


#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <file.h>
#else
#include <sys/file.h>
#endif
#include <time.h>

#include <signal.h>
extern char *ctime();

#ifdef _WIN32
#include <windows.h>
/* as of Visual Studi 2005, the POSIX names are deprecated; need to use
Windows specific names instead. 
*/
#include <direct.h>
#define chdir _chdir
#define mkdir _mkdir
#define getcwd _getcwd

#include <io.h>
#define access _access
#else
#include <unistd.h> /* for access(), lockf() */
#endif

#include "ac.h"
#include "fipublic.h"
#include "machinedep.h"




#define CURRENTID "CurrentID"
#define MAXID "MaxID"
#define MAXUNIQUEID 16777215	/* 2^24 - 1 */
#define BUFFERSZ 512		/* buffer size used by unique id file */
#define MAXIDFILELEN 100	/* maximum length of line in unique id file */

static char uniqueIDFile[MAXPATHLEN];
static int16_t warncnt = 0;
#if defined (_WIN32)
static char Delimiter[] = "\\";
#else
static char Delimiter[] = "/";
#endif

static int (*errorproc)(int16_t); /* proc to be called from LogMsg if error occurs */

/* used for cacheing of log messages */
static char lastLogStr[MAXMSGLEN + 1] = "";
static int16_t lastLogLevel = -1;
static bool lastLogPrefix;
static int logCount = 0;

static void LogMsg1(char *str, int16_t level, int16_t code, bool prefix);

int16_t WarnCount()
{
    return warncnt;
}

void ResetWarnCount()
{
    warncnt = 0;
}

#ifdef IS_LIB
#define Write(s) { if (libReportCB != NULL)libReportCB( s);}
#define WriteWarnorErr(f,s) {if (libErrorReportCB != NULL) libErrorReportCB( s);}
#else
#define Write(s) 
#define WriteWarnorErr(f,s)
#endif

void set_errorproc(userproc)
int (*userproc)(int16_t);
{
  errorproc = userproc;
}

/* called by LogMsg and when exiting (tidyup) */
static void FlushLogMsg(void)
{
  /* if message happened exactly 2 times, don't treat it specially */
  if (logCount == 1) {
    LogMsg1(lastLogStr, lastLogLevel, OK, lastLogPrefix);
  } else if (logCount > 1) {
    char newStr[MAXMSGLEN];
    sprintf(newStr, "The last message (%.20s...) repeated %d more times.\n",
	    lastLogStr, logCount);
    LogMsg1(newStr, lastLogLevel, OK, true);
  }
  logCount = 0;
}

 void LogMsg( 
		char *str,			/* message string */
		int16_t level,		/* error, warning, info */
		int16_t code,		/* exit value - if !OK, this proc will not return */
		bool prefix	/* prefix message with LOGERROR: or WARNING:, as appropriate */)
{
  /* changed handling of this to be more friendly (?) jvz */
  if (strlen(str) > MAXMSGLEN) {
    LogMsg1("The following message was truncated.\n", WARNING, OK, true);
    ++warncnt;
  }
  if (level == WARNING)
    ++warncnt;
  if (!strcmp(str, lastLogStr) && level == lastLogLevel) {
    ++logCount; /* same message */
  } else { /* new message */
    if (logCount) /* messages pending */
      FlushLogMsg();
    LogMsg1(str, level, code, prefix); /* won't return if LOGERROR */
    strncpy(lastLogStr, str, MAXMSGLEN);
    lastLogLevel = level;
    lastLogPrefix = prefix;
  }
}

static void LogMsg1(char *str, int16_t level, int16_t code, bool prefix)
{
  switch (level)
  {
  case INFO:
    Write(str);
    break;
  case WARNING:
    if (prefix)
      WriteWarnorErr(stderr, "WARNING: ");
    WriteWarnorErr(stderr, str);
    break;
  case LOGERROR:
    if (prefix)
      WriteWarnorErr(stderr, "ERROR: ");
    WriteWarnorErr(stderr, str);
    break;
  default:
     WriteWarnorErr(stderr, "ERROR - log level not recognized: ");
    WriteWarnorErr(stderr, str);
	break;  
  }
  if (level == LOGERROR && (code == NONFATALERROR || code == FATALERROR))
  	{
    (*errorproc)(code);
  	}
}

 void set_uniqueIDFile(str)
char *str;
{
  strcpy(uniqueIDFile, str);
}

void get_filename(char *name, char *str, const char *basename)
{
  sprintf(name, "%s%s%s", str, Delimiter, basename);
}



/* Returns true if the given file exists, is not a directory
   and user has read permission, otherwise it returns false. */
static bool FileExists(const char *filename, int16_t errormsg)
{
  struct stat stbuff;
  int filedesc;

  if ((strlen(filename) == 0) && !errormsg)
    return false;
  /* Check if this file exists and if it is a directory. */
  if (stat(filename, &stbuff) == -1)
  {
    if (errormsg)
    {
      sprintf(globmsg, "The %s file does not exist, but is required.\n", filename);
      LogMsg(globmsg, LOGERROR, OK, true);
    }
    return false;
  }
  if ((stbuff.st_mode & S_IFMT) == S_IFDIR)
  {
    if (errormsg)
    {
      sprintf(globmsg, "%s is a directory not a file.\n", filename);
      LogMsg(globmsg, LOGERROR, OK, true);
    }
    return false;
  }
  else

 /* Check for read permission. */
  if ((filedesc = access(filename, R_OK)) == -1)
  {
    if (errormsg)
    {
      sprintf(globmsg, "The %s file is not accessible.\n", filename);
      LogMsg(globmsg, LOGERROR, OK, true);
    }
    return false;
  }

  return true;
}

bool DirExists(char *dirname, bool absolute, bool create, bool errormsg)
{
    int32_t access_denied = access(dirname, F_OK);
    (void)absolute;
    
    if (access_denied)
    {
        if (errno == EACCES)
        {
            sprintf(globmsg, "The %s directory cannot be accessed.\n", dirname);
            LogMsg(globmsg, LOGERROR, OK, true);
            return false;
        }
        else
        {
            if (errormsg)
            {
                sprintf(globmsg, "The %s directory does not exist.", dirname);
                LogMsg(globmsg, create?WARNING:LOGERROR, OK, true);
            }
            if (!create)
            {
                if (errormsg)
                    LogMsg("\n", LOGERROR, OK, false);
                return false;
            }
            if (errormsg)
                LogMsg("  It will be created for you.\n", WARNING, OK, false);
            {
#ifdef _WIN32
                int result = mkdir(dirname);
#else
                int result = mkdir(dirname, 00775);
#endif
                
                if (result)
                {
                    sprintf(globmsg, "Can't create the %s directory.\n", dirname);
                    LogMsg(globmsg, LOGERROR, OK, true);
                    return false;
                }
            } /* end local block for mkdir */
        }
    }
    return true;
}



/* Returns the full name of the input directory. */
void GetInputDirName(name, suffix)
char *name;
char *suffix;
{
  char currdir[MAXPATHLEN];

  getcwd(currdir,MAXPATHLEN);
  sprintf(name, "%s%s%s", currdir, Delimiter, suffix);
}

uint32_t ACGetFileSize(filename)
char *filename;
{
  struct stat filestat;

  if ((stat(filename, &filestat)) < 0) return (0);
#ifdef CW80
	return((uint32_t) filestat.st_mtime);
#else
  return((uint32_t) filestat.st_size);
#endif
}

void RenameFile(oldName, newName)
char *oldName, *newName;
{
  if(FileExists(newName, false))
  {
	if(remove(newName))
	{
		sprintf(globmsg, "Could not remove file: %s for renaming (%d).\n", newName, errno);
		LogMsg(globmsg, LOGERROR, NONFATALERROR, true);	
	}
  }
  if(rename(oldName, newName))
  {
    sprintf(globmsg, "Could not rename file: %s to: %s (%d).\n", oldName, newName, errno );
    LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
  }
}


/* Converts :'s to /'s.  Colon's before the first alphabetic
   character are converted to ../ */
char *CheckBFPath(baseFontPath)
char *baseFontPath;
{
  char *uponelevel = "../";
  char *startpath, *oldsp;
  char newpath[MAXPATHLEN];
  int16_t length;

  newpath[0] = '\0';
  if (strchr(baseFontPath, *Delimiter) == NULL)
  {
    if (*baseFontPath == *Delimiter)
      LogMsg("BaseFontPath keyword in fontinfo file must indicate\
  a relative path name.\n", LOGERROR, NONFATALERROR, true);
    return baseFontPath;
  }

  /* Find number of colons at the start of the path */
  switch (length=(int16_t)strspn(baseFontPath, Delimiter))
  {
    case (0):
      break;
    case (1):
      LogMsg("BaseFontPath keyword in fontinfo file must indicate\
  a relative path name.\n", LOGERROR, NONFATALERROR, true);
      break;
    default:
      /* first two colons => up one, any subsequent single colon => up one */
      for (startpath = baseFontPath + 1; 
           startpath < baseFontPath + length; startpath++)
        if (*startpath == *Delimiter)
          strcat(newpath, uponelevel);
      break;
  }

  length = (int16_t)strlen(startpath);
  /* replace all colons by slashes in remainder of input string -
    Note: this does not handle > 1 colon embedded in the middle of
    a Mac path name correctly */
  for (oldsp = startpath; startpath < oldsp + length; startpath++)
    if (*startpath == *Delimiter)
      *startpath = *Delimiter;
  startpath = AllocateMem(((unsigned int)strlen(newpath)) + length + 1, sizeof(char),
    "return string for CheckBFPath");
  strcpy (startpath, newpath);
  strcat(startpath, oldsp);
		UnallocateMem(baseFontPath);
  return startpath;
}

/* copies from one path ref num to another - can be different forks of same file */

#if defined(_MSC_VER) && ( _MSC_VER < 1800)
float roundf(float x)
{
    float val =  (float)((x < 0) ? (ceil((x)-0.5)) : (floor((x)+0.5)));
    return val;
}
#endif


 unsigned char *CtoPstr(char *filename)
{
	int i, length=(int)strlen(filename);
	for (i=length; i>0; i--)
		filename[i]=filename[i-1];
	filename[0]=length;
	return (unsigned char*)filename;
}

 char *PtoCstr(unsigned char *filename)
{
	int i, length=filename[0];
	for (i=0; i<length; i++)
		filename[i]=filename[i+1];
	filename[i]='\0';
	
	return (char *)filename;
}

