/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "memory.h"
#include "logging.h"

static void*
defaultAC_memmanage(void* ctxptr, void* old, size_t size)
{
    (void)ctxptr;
    if (size > 0) {
        if (NULL == old) {
            return malloc(size);
        } else {
            return realloc(old, size);
        }
    } else {
        if (NULL == old)
            return NULL;
        else {
            free(old);
            return NULL;
        }
    }
}

AC_MEMMANAGEFUNCPTR AC_memmanageFuncPtr = defaultAC_memmanage;
void* AC_memmanageCtxPtr = NULL;

void
setAC_memoryManager(void* ctxptr, AC_MEMMANAGEFUNCPTR func)
{
    AC_memmanageFuncPtr = func;
    AC_memmanageCtxPtr = ctxptr;
}

char*
AllocateMem(size_t nelem, size_t elsize, const char* description)
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
ReallocateMem(char* ptr, size_t size, const char* description)
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
