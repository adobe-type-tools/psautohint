/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

/* optable.c - initializes table of known PostScript operators. */

#include "optable.h"
#include "ac.h"
#include "opcodes.h"

/*
Not all of the operators from opcodes.h are initialized here; in
   particular, some that seemed to be needed just for Courier were ignored.

Operators for which the encoding value has been incremented by ESCVAL must
   be preceded by an escape when written to the output charstring. */

/* This defines an element of the table used to translate ASCII operand
  names to the binary encoded equivalents. */
static struct
{
    int16_t op;
    char* operator;
} op_table[] = { { VMT, "vmt" },
                 { RDT, "rdt" },
                 { HDT, "hdt" },
                 { VDT, "vdt" },
                 { RCT, "rct" },
                 { CP, "cp" },
                 { RET, "ret" },
                 { ESC, "esc" },
                 { SBX, "sbx" },
                 { ED, "ed" },
                 { MT, "mt" },
                 { CT, "ct" },
                 { DT, "dt" },
                 { RMT, "rmt" },
                 { HMT, "hmt" },
                 { VHCT, "vhct" },
                 { HVCT, "hvct" },
                 /* special non-charstring perators start here */
                 { SC, "sc" },
                 { ID, "id" },
                 /* escape perators start here */
                 { CC + ESCVAL, "cc" },
                 { 0, NULL } };

char*
GetOperator(int16_t op)
{
    indx ix;

    for (ix = 0; op_table[ix].operator!= NULL; ix++) {
        if (op == op_table[ix].op) {
            return op_table[ix].operator;
        }
    }
    LogMsg(LOGERROR, NONFATALERROR, "The opcode: %d is invalid.\n", op);
    return "";
}
