/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/
/* cryptdefs.h
Original Version: Linda Gass January 4, 1987
Edit History:
Linda Gass: Fri Jul 17 15:16:59 1987
Linda Weinert: 3/10/87 - introduce ifndef
End Edit History.
*/

#include "basic.h"

#define BIN 0
#define HEX 1

#define OTHER 0
#define EEXEC 1
#define FONTPASSWORD 2

#define INLEN -1

extern int32_t DoContEncrypt(
    char *, FILE *, boolean, int32_t
);

extern int32_t ContEncrypt(char *indata, FILE  *outstream, boolean fileinput, int32_t incount, boolean dblenc);

extern int16_t DoInitEncrypt(
    FILE *, int16_t, int32_t, int32_t, boolean
);

extern int32_t DoEncrypt(
    char *, FILE *, boolean, int32_t, int16_t, int32_t, int32_t, boolean, boolean
);

extern int32_t ContDecrypt(
    char *, char *, boolean, boolean, int32_t, uint32_t
);

extern uint32_t ReadDecFile(
    FILE *, char *, char *, boolean, int32_t, uint32_t, int16_t
);

extern void SetLenIV (
    int16_t
);
