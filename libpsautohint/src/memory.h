/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "psautohint.h"

#include "basic.h"

#ifndef AC_MEMORY_H_
#define AC_MEMORY_H_

void setAC_memoryManager(void* ctxptr, AC_MEMMANAGEFUNCPTR func);

void* AllocateMem(size_t, size_t, const char*);
void* ReallocateMem(void*, size_t, const char*);
void UnallocateMem(void* ptr);

#endif /* AC_MEMORY_H_ */
