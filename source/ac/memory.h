/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "basic.h"

#ifndef AC_MEMORY_H_
#define AC_MEMORY_H_

typedef void* (*AC_MEMMANAGEFUNCPTR)(void* ctxptr, void* old, uint32_t size);
extern AC_MEMMANAGEFUNCPTR AC_memmanageFuncPtr;
extern void* AC_memmanageCtxPtr;

void setAC_memoryManager(void* ctxptr, AC_MEMMANAGEFUNCPTR func);

char* AllocateMem(unsigned int, unsigned int, const char*);
char* ReallocateMem(char*, unsigned int, const char*);
void UnallocateMem(void* ptr);

#endif /* AC_MEMORY_H_ */
