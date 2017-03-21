/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/
/* rawPStobez.c - converts character description files in raw
   PostScript format to relativized bez format.	 The only
   operators allowed are moveto, lineto, curveto and closepath.
   This program assumes that the rawPS files are located in a
   directory named chars and stores the converted and encrypted
   files with the same name in a directory named bez.

history:

Judy Lee: Tue Jun 14 17:33:13 1988
End Edit History

*/
#include "buildfont.h"
#include "bftoac.h"
#include "cryptdefs.h"
#include "machinedep.h"


static char outstr[MAXLINE];	/* contains string to be encrypted */

int WriteStart(FILE *outfile, const char *name)
{
  sprintf(outstr, "%%%s\nsc\n", name);
  (void) DoContEncrypt(outstr, outfile, false, INLEN);
  return 0;
}
