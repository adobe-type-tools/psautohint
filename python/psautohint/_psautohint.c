/*
 * Copyright 2014 Adobe Systems Incorporated. All rights reserved.
 * Copyright 2017 Khaled Hosny
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use these files except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#define PY_SSIZE_T_CLEAN 1
#include <Python.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_MSC_VER) || _MSC_VER >= 1800
#include <stdbool.h>
#else
typedef unsigned char bool;
#define true 1
#define false 0
#define snprintf(buf, len, format, ...)                                        \
    _snprintf_s(buf, len, len, format, __VA_ARGS__)
#endif

#include "psautohint.h"

static void
reportCB(char* msg, int level)
{
    static PyObject* logger = NULL;

    if (logger == NULL) {
        PyObject* logging = PyImport_ImportModule("logging");
        if (logging == NULL)
            return;
        logger = PyObject_CallMethod(logging, "getLogger", "s", "_psautohint");
        if (logger == NULL)
            return;
    }

    switch (level) {
        case AC_LogDebug:
            PyObject_CallMethod(logger, "debug", "s", msg);
            break;
        case AC_LogInfo:
            PyObject_CallMethod(logger, "info", "s", msg);
            break;
        case AC_LogWarning:
            PyObject_CallMethod(logger, "warning", "s", msg);
            break;
        case AC_LogError:
            PyObject_CallMethod(logger, "error", "s", msg);
            break;
        default:
            break;
    }
}

#define STR_LEN 200
#define WRITE_TO_BUFFER(text, a1, a2, a3)                                      \
    if (userData) {                                                            \
        char str[STR_LEN];                                                     \
        ACBuffer* buffer = (ACBuffer*)userData;                                \
        int len = snprintf(str, STR_LEN, text, a1, (double)a2, (double)a3);    \
        if (len > 0)                                                           \
            ACBufferWrite(buffer, str,                                         \
                          (size_t)(len <= STR_LEN ? len : STR_LEN));           \
    }

static void
charZoneCB(float top, float bottom, char* glyphName, void* userData)
{
    WRITE_TO_BUFFER("charZone %s top %f bottom %f\n", glyphName, top, bottom);
}

static void
stemZoneCB(float top, float bottom, char* glyphName, void* userData)
{
    WRITE_TO_BUFFER("stemZone %s top %f bottom %f\n", glyphName, top, bottom);
}

static void
hstemCB(float top, float bottom, char* glyphName, void* userData)
{
    WRITE_TO_BUFFER("HStem %s top %f bottom %f\n", glyphName, top, bottom);
}

static void
vstemCB(float right, float left, char* glyphName, void* userData)
{
    WRITE_TO_BUFFER("VStem %s right %f left %f\n", glyphName, right, left);
}

static void
reportRetry(void* userData)
{
    if (userData) {
        ACBuffer* buffer = (ACBuffer*)userData;
        ACBufferReset(buffer);
    }
}

#if PY_MAJOR_VERSION >= 3
#define MEMNEW(size) PyMem_RawCalloc(1, size)
#define MEMFREE(ptr) PyMem_RawFree(ptr)
#define MEMRENEW(ptr, size) PyMem_RawRealloc(ptr, size)
#else
#define MEMNEW(size) PyMem_Malloc(size)
#define MEMFREE(ptr) PyMem_Free(ptr)
#define MEMRENEW(ptr, size) PyMem_Realloc(ptr, size)
#endif

static void*
memoryManager(void* ctx, void* ptr, size_t size)
{
    if (!ptr && !size)
        return NULL;

    if (ptr && size)
        ptr = MEMRENEW(ptr, size);
    else if (size)
        ptr = MEMNEW(size);
    else
        MEMFREE(ptr);

    return ptr;
}

static PyObject* PsAutoHintError;

static char autohint_doc[] =
  "Autohint glyphs.\n"
  "\n"
  "Signature:\n"
  "  autohint(font_info, glyphs[, no_edit, allow_hint_sub, round])\n"
  "\n"
  "Args:\n"
  "  font_info: font information.\n"
  "  glyph: glyph data in bez format.\n"
  "  allow_edit: allow editing (changing) the paths when hinting.\n"
  "  allow_hint_sub: no multiple layers of coloring.\n"
  "  round: round coordinates.\n"
  "\n"
  "Output:\n"
  "  Autohinted glyph data in bez format.\n"
  "\n"
  "Raises:\n"
  "  psautohint.error: If autohinting fails.\n";

static PyObject*
autohint(PyObject* self, PyObject* args)
{
    int allowEdit = true, roundCoords = true, allowHintSub = true;
    int report = 0, allStems = false;
    PyObject* fontObj = NULL;
    PyObject* inObj = NULL;
    PyObject* outObj = NULL;
    char* inData = NULL;
    char* fontInfo = NULL;
    bool error = true;
    ACBuffer* reportBufffer = NULL;

    if (!PyArg_ParseTuple(args, "O!O!|iiiii", &PyBytes_Type, &fontObj,
                          &PyBytes_Type, &inObj, &allowEdit, &allowHintSub,
                          &roundCoords, &report, &allStems))
        return NULL;

    if (report) {
        reportBufffer = ACBufferNew(150);
        allowEdit = allowHintSub = false;
        switch (report) {
            case 1:
                AC_SetReportRetryCB(reportRetry, (void*)reportBufffer);
                AC_SetReportZonesCB(charZoneCB, stemZoneCB,
                                    (void*)reportBufffer);
                break;
            case 2:
                AC_SetReportRetryCB(reportRetry, (void*)reportBufffer);
                AC_SetReportStemsCB(hstemCB, vstemCB, allStems,
                                    (void*)reportBufffer);
                break;
            default:
                PyErr_SetString(PyExc_ValueError,
                                "Invalid \"report\" argument, must be 1 or 2");
                goto done;
        }
    }

    AC_SetMemManager(NULL, memoryManager);
    AC_SetReportCB(reportCB);

    fontInfo = PyBytes_AsString(fontObj);
    inData = PyBytes_AsString(inObj);
    if (inData && fontInfo) {
        int result = -1;

        size_t outLen = 4 * strlen(inData);
        char* output = MEMNEW(outLen);
        if (output) {
            result = AutoHintString(inData, fontInfo, &output, &outLen,
                                    allowEdit, allowHintSub, roundCoords);

            if (result == AC_Success) {
                error = false;
                if (reportBufffer) {
                    char* data;
                    size_t len;
                    ACBufferRead(reportBufffer, &data, &len);
                    outObj = PyBytes_FromStringAndSize(data, len);
                } else {
                    outObj = PyBytes_FromStringAndSize(output, outLen);
                }
            }

            MEMFREE(output);
        }

        if (result != AC_Success) {
            switch (result) {
                case -1:
                    /* Do nothing, we already called PyErr_* */
                    break;
                case AC_FatalError:
                    PyErr_SetString(PsAutoHintError, "Fatal error");
                    break;
                case AC_InvalidParameterError:
                    PyErr_SetString(PyExc_ValueError, "Invalid glyph data");
                    break;
                case AC_UnknownError:
                default:
                    PyErr_SetString(PsAutoHintError, "Hinting failed");
                    break;
            }
        }
    }

done:
    ACBufferFree(reportBufffer);

    if (error)
        return NULL;
    return outObj;
}

static char autohintmm_doc[] =
  "Autohint glyphs.\n"
  "\n"
  "Signature:\n"
  "  autohintm(font_info, glyphs)\n"
  "\n"
  "Args:\n"
  "  glyphs: sequence of glyph data in bez format.\n"
  "  masters: sequence of master names.\n"
  "\n"
  "Output:\n"
  "  Sequence of autohinted glyph data in bez format.\n"
  "\n"
  "Raises:\n"
  "  psautohint.error: If autohinting fails.\n";

static PyObject*
autohintmm(PyObject* self, PyObject* args)
{
    PyObject* inObj = NULL;
    Py_ssize_t inCount = 0;
    PyObject* mastersObj = NULL;
    Py_ssize_t mastersCount = 0;
    PyObject* outSeq = NULL;
    const char** masters;
    bool error = true;
    Py_ssize_t i;

    if (!PyArg_ParseTuple(args, "O!O!", &PyTuple_Type, &inObj, &PyTuple_Type,
                          &mastersObj))
        return NULL;

    inCount = PyTuple_GET_SIZE(inObj);
    mastersCount = PyTuple_GET_SIZE(mastersObj);
    if (inCount != mastersCount) {
        PyErr_SetString(
          PyExc_TypeError,
          "Length of \"glyphs\" must equal length of \"masters\".");
        return NULL;
    }

    if (inCount <= 1) {
        PyErr_SetString(PyExc_TypeError, "Length of input glyphs must be > 1");
        return NULL;
    }

    masters = MEMNEW(mastersCount * sizeof(char*));
    if (!masters) {
        PyErr_NoMemory();
        return NULL;
    }

    for (i = 0; i < mastersCount; i++) {
        PyObject* obj = PyTuple_GET_ITEM(mastersObj, i);
        masters[i] = PyBytes_AsString(obj);
        if (!masters[i])
            goto done;
    }

    AC_SetMemManager(NULL, memoryManager);
    AC_SetReportCB(reportCB);

    outSeq = PyTuple_New(inCount);
    if (outSeq) {
        int result = -1;

        const char** inGlyphs = MEMNEW(inCount * sizeof(char*));
        char** outGlyphs = MEMNEW(inCount * sizeof(char*));
        size_t* outputSizes = MEMNEW(inCount * sizeof(size_t*));
        if (!inGlyphs || !outGlyphs || !outputSizes) {
            PyErr_NoMemory();
            goto finish;
        }

        for (i = 0; i < inCount; i++) {
            PyObject* glyphObj = PyTuple_GET_ITEM(inObj, i);
            inGlyphs[i] = PyBytes_AsString(glyphObj);
            if (!inGlyphs[i])
                goto finish;
            outputSizes[i] = 4 * strlen(inGlyphs[i]);
            outGlyphs[i] = MEMNEW(outputSizes[i]);
        }

        result = AutoHintStringMM(inGlyphs, mastersCount, masters, outGlyphs,
                                  outputSizes);
        if (result == AC_Success) {
            error = false;
            for (i = 0; i < inCount; i++) {
                PyObject* outObj =
                  PyBytes_FromStringAndSize(outGlyphs[i], outputSizes[i]);
                PyTuple_SET_ITEM(outSeq, i, outObj);
            }
        }

    finish:
        if (outGlyphs) {
            for (i = 0; i < inCount; i++)
                MEMFREE(outGlyphs[i]);
        }

        MEMFREE(inGlyphs);
        MEMFREE(outGlyphs);
        MEMFREE(outputSizes);

        if (result != AC_Success) {
            switch (result) {
                case -1:
                    /* Do nothing, we already called PyErr_* */
                    break;
                case AC_FatalError:
                    PyErr_SetString(PsAutoHintError, "Fatal error");
                    break;
                case AC_InvalidParameterError:
                    PyErr_SetString(PyExc_ValueError, "Invalid glyph data");
                    break;
                case AC_UnknownError:
                default:
                    PyErr_SetString(PsAutoHintError, "Hinting failed");
                    break;
            }
        }
    }

done:
    MEMFREE(masters);

    if (error) {
        Py_XDECREF(outSeq);
        return NULL;
    }

    return outSeq;
}

/* clang-format off */
static PyMethodDef psautohint_methods[] = {
  { "autohint", autohint, METH_VARARGS, autohint_doc },
  { "autohintmm", autohintmm, METH_VARARGS, autohintmm_doc },
  { NULL, NULL, 0, NULL }
};
/* clang-format on */

static char psautohint_doc[] =
  "Python wrapper for Adobe's PostScrupt autohinter.\n"
  "\n"
  "autohint() -- Autohint glyphs.\n";

#define SETUPMODULE                                                            \
    PyModule_AddStringConstant(m, "version", AC_getVersion());                 \
    PsAutoHintError = PyErr_NewException("psautohint.error", NULL, NULL);      \
    Py_INCREF(PsAutoHintError);                                                \
    PyModule_AddObject(m, "error", PsAutoHintError);

#if PY_MAJOR_VERSION >= 3
/* clang-format off */
static struct PyModuleDef psautohint_module = {
  PyModuleDef_HEAD_INIT,
  "_psautohint",
  psautohint_doc,
  0,
  psautohint_methods,
  NULL,
  NULL,
  NULL,
  NULL
};
/* clang-format on */

PyMODINIT_FUNC
PyInit__psautohint(void)
{
    PyObject* m;

    m = PyModule_Create(&psautohint_module);
    if (m == NULL)
        return NULL;

    SETUPMODULE

    return m;
}
#else /* Python < 3 */
PyMODINIT_FUNC
init_psautohint(void)
{
    PyObject* m;

    m = Py_InitModule3("_psautohint", psautohint_methods, psautohint_doc);
    if (m == NULL)
        return;

    SETUPMODULE

    return;
}
#endif
