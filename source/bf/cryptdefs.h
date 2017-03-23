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

extern uint32_t ReadDecFile(
    FILE *, char *, char *, bool, int32_t, uint32_t, int16_t
);
