/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/
/* charpath.c */

#include "machinedep.h"
#include "buildfont.h"
#include "bftoac.h"
#include "charpath.h"


bool cubeLibrary;
char *currentChar; /* name of the current char for error messages */

static int16_t dirCount;
static indx hintsdirIx;

/* Returns the subr number to use for a given operator in subrIx and
 checks that the argument length of each subr call does not
 exceed the font interpreter stack limit. */
extern void GetLengthandSubrIx(int16_t opcount, int16_t *length, int16_t *subrIx)
{
    
    if (((opcount * dirCount) > FONTSTKLIMIT) && opcount != 1)
        if ((opcount/2 * dirCount) > FONTSTKLIMIT)
            if ((2 * dirCount) > FONTSTKLIMIT)
                *length = 1;
            else *length = 2;
            else *length = opcount/2;
            else *length = opcount;
    if (((*length) * dirCount) > FONTSTKLIMIT) {
        sprintf(globmsg, "Font stack limit exceeded for character: %s.\n", currentChar);
        LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
    }
    if (!cubeLibrary) {
        switch (*length) {
            case 1: *subrIx = 7; break;
            case 2: *subrIx = 8; break;
            case 3: *subrIx = 9; break;
            case 4: *subrIx = 10; break;
            case 6: *subrIx = 11; break;
            default:
                sprintf(globmsg, "Illegal operand length for character: %s.\n", currentChar);
                LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
                break;
        }
    } else { /* CUBE */
        switch (dirCount) {
            case 2:
                switch (*length) {
                    case 1: *subrIx = 7; break;
                    case 2: *subrIx = 8; break;
                    case 3: *subrIx = 9; break;
                    case 4: *subrIx = 10; break;
                    case 6: *subrIx = 11; break;
                    default:
                        sprintf(globmsg, "Illegal operand length for character: %s.\n", currentChar);
                        LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
                        break;
                }
                break;
                
            case 4:
                switch (*length) {
                    case 1: *subrIx = 12; break;
                    case 2: *subrIx = 13; break;
                    case 3: *subrIx = 14; break;
                    case 4: *subrIx = 15; break;
                    default:
                        sprintf(globmsg, "Illegal operand length for character: %s.\n", currentChar);
                        LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
                        break;
                }
                break;
                
            case 8:
                switch (*length) {
                    case 1: *subrIx = 16; break;
                    case 2: *subrIx = 17; break;
                    default:
                        sprintf(globmsg, "Illegal operand length for character: %s.\n", currentChar);
                        LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
                        break;
                }
                break;
                
            case 16:
                switch (*length) {
                    case 1: *subrIx = 18; break;
                    default:
                        sprintf(globmsg, "Illegal operand length for character: %s.\n", currentChar);
                        LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
                        break;
                }
                break;
                
            default:
                LogMsg("Illegal dirCount.\n", LOGERROR, NONFATALERROR, true);
                break;
        }
    }
}

/**********
 Normal MM fonts have their dimensionality wired into the subrs.
 That is, the contents of subr 7-11 are computed on a per-font basis.
 Cube fonts can be of 1-4 dimensions on a per-character basis.
 But there are only a few possible combinations of these numbers
 because we are limited by the stack size:
 
 dimensions  arguments      values    subr#
 1            1            2        7
 1            2            4        8
 1            3            6        9
 1            4            8       10
 1            6           12       11
 2            1            4       12
 2            2            8       13
 2            3           12       14
 2            4           16       15
 3            1            8       16
 3            2           16       17
 4            1           16       18
 
 *************/

extern void SetHintsDir(dirIx)
indx dirIx;
{
    hintsdirIx = dirIx;
}

extern int GetHintsDir(void)
{
    return hintsdirIx;
}
