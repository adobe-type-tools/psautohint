/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac_C_lib.h"
#include "basic.h"

#ifndef AC_MEMORY_H_
#define AC_MEMORY_H_

extern AC_MEMMANAGEFUNCPTR AC_memmanageFuncPtr;
extern void* AC_memmanageCtxPtr;

void setAC_memoryManager(void* ctxptr, AC_MEMMANAGEFUNCPTR func);

char* AllocateMem(size_t, size_t, const char*);
char* ReallocateMem(char*, size_t, const char*);
void UnallocateMem(void* ptr);

#endif /* AC_MEMORY_H_ */
