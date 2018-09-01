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

static AC_MEMMANAGEFUNCPTR AC_memmanageFuncPtr = defaultAC_memmanage;
static void* AC_memmanageCtxPtr = NULL;

void
setAC_memoryManager(void* ctxptr, AC_MEMMANAGEFUNCPTR func)
{
    AC_memmanageFuncPtr = func;
    AC_memmanageCtxPtr = ctxptr;
}

void*
AllocateMem(size_t nelem, size_t elsize, const char* description)
{
    /* calloc(nelem, elsize) */
    void* ptr = AC_memmanageFuncPtr(AC_memmanageCtxPtr, NULL, nelem * elsize);
    if (NULL != ptr)
        memset(ptr, 0x0, nelem * elsize);

    if (ptr == NULL) {
        LogMsg(LOGERROR, FATALERROR,
               "Cannot allocate %zu bytes of memory for %s.", nelem * elsize,
               description);
    }
    return (ptr);
}

void*
ReallocateMem(void* ptr, size_t size, const char* description)
{
    /* realloc(ptr, size) */
    void* newptr = AC_memmanageFuncPtr(AC_memmanageCtxPtr, ptr, size);

    if (newptr == NULL) {
        LogMsg(LOGERROR, FATALERROR,
               "Cannot reallocate %zu bytes of memory for %s.", size,
               description);
    }
    return (newptr);
}

void
UnallocateMem(void* ptr)
{
    /* free(ptr) */
    AC_memmanageFuncPtr(AC_memmanageCtxPtr, ptr, 0);
}
