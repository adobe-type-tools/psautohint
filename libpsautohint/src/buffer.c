/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include <stdarg.h>

#include "logging.h"
#include "memory.h"
#include "psautohint.h"

struct ACBuffer
{
    char* data;      /* buffer data, NOT null-terminated */
    size_t len;      /* actual length of the data */
    size_t capacity; /* allocated memory size */
};

ACLIB_API ACBuffer*
ACBufferNew(size_t size)
{
    ACBuffer* buffer;

    if (!size)
        return NULL;

    buffer = (ACBuffer*)AllocateMem(1, sizeof(ACBuffer), "buffer");
    buffer->data = AllocateMem(size, 1, "buffer data");
    buffer->data[0] = '\0';
    buffer->capacity = size;
    buffer->len = 0;

    return buffer;
}

ACLIB_API void
ACBufferFree(ACBuffer* buffer)
{
    if (!buffer)
        return;

    UnallocateMem(buffer->data);
    UnallocateMem(buffer);
}

ACLIB_API void
ACBufferReset(ACBuffer* buffer)
{
    if (!buffer)
        return;
    buffer->len = 0;
}

ACLIB_API void
ACBufferWrite(ACBuffer* buffer, char* data, size_t length)
{
    if (!buffer)
        return;

    if ((buffer->len + length) >= buffer->capacity) {
        size_t size = NUMMAX(buffer->capacity * 2, buffer->capacity + length);
        buffer->data = ReallocateMem(buffer->data, size, "buffer data");
        buffer->capacity = size;
    }
    memcpy(buffer->data + buffer->len, data, length);
    buffer->len += length;
}

#define STRLEN 1000
ACLIB_API void
ACBufferWriteF(ACBuffer* buffer, char* format, ...)
{
    char outstr[STRLEN];
    int len;
    va_list va;

    if (!buffer)
        return;

    va_start(va, format);
    len = vsnprintf(outstr, STRLEN, format, va);
    va_end(va);

    if (len > 0 && len <= STRLEN) {
        ACBufferWrite(buffer, outstr, len);
    } else {
        char* outstr = AllocateMem(1, len + 1, "Temporary buffer");

        va_start(va, format);
        len = vsnprintf(outstr, len + 1, format, va);
        va_end(va);

        if (len > 0)
            ACBufferWrite(buffer, outstr, len);
        else
            LogMsg(LOGERROR, FATALERROR, "Failed to write string to ACBuffer.");

        UnallocateMem(outstr);
    }
}

ACLIB_API void
ACBufferRead(ACBuffer* buffer, char** data, size_t* length)
{
    if (buffer) {
        *data = buffer->data;
        *length = buffer->len;
    } else {
        *data = NULL;
        *length = 0;
    }
}
