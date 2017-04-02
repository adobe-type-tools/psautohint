/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "buildfont.h"
#include "charpath.h"
#include "fipublic.h"
#include "machinedep.h"

static int16_t total_inputdirs = 1; /* number of input directories           */
char globmsg[MAXMSGLEN + 1];        /* used to format messages               */

typedef void* (*AC_MEMMANAGEFUNCPTR)(void* ctxptr, void* old, uint32_t size);
extern AC_MEMMANAGEFUNCPTR AC_memmanageFuncPtr;
extern void* AC_memmanageCtxPtr;

char*
AllocateMem(unsigned int nelem, unsigned int elsize, const char* description)
{
    /* calloc(nelem, elsize) */
    char* ptr =
      (char*)AC_memmanageFuncPtr(AC_memmanageCtxPtr, NULL, nelem * elsize);
    if (NULL != ptr)
        memset((void*)ptr, 0x0, nelem * elsize);

    if (ptr == NULL) {
        LogMsg(LOGERROR, NONFATALERROR,
               "Cannot allocate %d bytes of memory for %s.\n",
               (int)(nelem * elsize), description);
    }
    return (ptr);
}

char*
ReallocateMem(char* ptr, unsigned int size, const char* description)
{
    /* realloc(ptr, size) */
    char* newptr =
      (char*)AC_memmanageFuncPtr(AC_memmanageCtxPtr, (void*)ptr, size);

    if (newptr == NULL) {
        LogMsg(LOGERROR, NONFATALERROR,
               "Cannot allocate %d bytes of memory for %s.\n", (int)size,
               description);
    }
    return (newptr);
}

void
UnallocateMem(void* ptr)
{
    /* free(ptr) */
    AC_memmanageFuncPtr(AC_memmanageCtxPtr, (void*)ptr, 0);
}

extern int16_t
GetTotalInputDirs(void)
{
    return total_inputdirs;
}
