/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/
/* optable.c - initializes table of known PostScript operators. */

#include "buildfont.h"
#include "machinedep.h"
#include "opcodes.h"
#include "optable.h"

/* This defines an element of the table used to translate ASCII operand 
  names to the binary encoded equivalents. */
typedef struct op_element
{
  int16_t encoding;
  char operator[MAXOPLEN];
} op_element;

#define KNOWNOPSIZE 18

static struct op_element *op_table = NULL;

/* Globals used outside this module. */

extern void init_ops()
{
  indx ix = 0;

  op_table = (struct op_element *) AllocateMem(KNOWNOPSIZE, sizeof(struct op_element), "operator table");
  /* The table is initialized in the following strange way to make it
     difficult to find out what the secret font operators are by looking at
     the object code for this source.  Anyone with a debugger can look at the
     operators once they have been initialized, however.  It would make sense
     to only have this initialization done once when buildfont is first
     started, but now it is done once per font processed.

  The operator strings need to be left justified and null terminated.

  Not all of the operators from opcodes.h are initialized here; in
     particular, some that seemed to be needed just for Courier were ignored.

  Operators for which the encoding value has been incremented by ESCVAL must
     be preceded by an escape when written to the output charstring. */
  sprintf(op_table[ix].operator, "%c%c%c%c", 'v', 'm', 't', '\0');
  op_table[ix++].encoding = VMT;
  sprintf(op_table[ix].operator, "%c%c%c%c", 'r', 'd', 't', '\0');
  op_table[ix++].encoding = RDT;
  sprintf(op_table[ix].operator, "%c%c%c%c", 'h', 'd', 't', '\0');
  op_table[ix++].encoding = HDT;
  sprintf(op_table[ix].operator, "%c%c%c%c", 'v', 'd', 't', '\0');
  op_table[ix++].encoding = VDT;
  sprintf(op_table[ix].operator, "%c%c%c%c", 'r', 'c', 't', '\0');
  op_table[ix++].encoding = RCT;
  sprintf(op_table[ix].operator, "%c%c%c%c", 'c', 'p', '\0', '\0');
  op_table[ix++].encoding = CP;
  sprintf(op_table[ix].operator, "%c%c%c%c", 'r', 'e', 't', '\0');
  op_table[ix++].encoding = RET;
  sprintf(op_table[ix].operator, "%c%c%c%c", 'e', 's', 'c', '\0');
  op_table[ix++].encoding = ESC;
  sprintf(op_table[ix].operator, "%c%c%c%c", 's', 'b', 'x', '\0');
  op_table[ix++].encoding = SBX;
  sprintf(op_table[ix].operator, "%c%c%c%c", 'e', 'd', '\0', '\0');
  op_table[ix++].encoding = ED;
  sprintf(op_table[ix].operator, "%c%c%c%c", 'm', 't', '\0', '\0');
  op_table[ix++].encoding = MT;
  sprintf(op_table[ix].operator, "%c%c%c%c", 'r', 'm', 't', '\0');
  op_table[ix++].encoding = RMT;
  sprintf(op_table[ix].operator, "%c%c%c%c", 'h', 'm', 't', '\0');
  op_table[ix++].encoding = HMT;
  sprintf(op_table[ix].operator, "%c%c%c%c", 'v', 'h', 'c', 't');
  op_table[ix++].encoding = VHCT;
  sprintf(op_table[ix].operator, "%c%c%c%c", 'h', 'v', 'c', 't');
  op_table[ix++].encoding = HVCT;
  /* special non-charstring operators start here */
  sprintf(op_table[ix].operator, "%c%c%c%c", 's', 'c', '\0', '\0');
  op_table[ix++].encoding = SC;
  sprintf(op_table[ix].operator, "%c%c%c%c", 'i', 'd', '\0', '\0');
  op_table[ix++].encoding = ID;
  /* escape operators start here */
  sprintf(op_table[ix].operator, "%c%c%c%c", 'c', 'c', '\0', '\0');
  op_table[ix++].encoding = CC + ESCVAL;

  if (ix > KNOWNOPSIZE)
  {
    LogMsg("Initialization of op table failed.\n",
      LOGERROR, NONFATALERROR, true);
  }
}

void GetOperator(int16_t encoding, char *operator)
{
  indx ix;

  for (ix = 0; ix < KNOWNOPSIZE; ix++)
    if (encoding == op_table[ix].encoding)
    {
      strcpy(operator, op_table[ix].operator);
      return;
    }
  sprintf(globmsg, "The opcode: %d is invalid.\n", (int)encoding);
  LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
}

/* Checks if the operator passed in is a recognized PS operator.
   If it is the associated opcode is returned.	 */
extern int16_t op_known(operator)
char *operator;

{
  indx ix;

  for (ix = 0; ix < KNOWNOPSIZE; ix++)
    if (STREQ(op_table[ix].operator, operator))
      return op_table[ix].encoding;
  return MININT;		/* Unknown operator */
}

