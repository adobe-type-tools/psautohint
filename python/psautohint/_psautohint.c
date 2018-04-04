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
#endif

#include "psautohint.h"

static void
reportCB(char* msg)
{
#if PY_MAJOR_VERSION >= 3
    PySys_FormatStdout("%s", msg);
#else
    /* Formatted string should not exceed 1000 bytes, see:
     * https://docs.python.org/2/c-api/sys.html#c.PySys_WriteStdout */
    PySys_WriteStdout("%.1000s", msg);
#endif
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
  "  autohint(font_info, glyphs[, verbose, no_edit, allow_hint_sub, "
  "round, debug])\n"
  "\n"
  "Args:\n"
  "  font_info: font information.\n"
  "  glyph: glyph data in bez format.\n"
  "  verbose: print verbose messages.\n"
  "  allow_edit: allow editing (changing) the paths when hinting.\n"
  "  allow_hint_sub: no multiple layers of coloring.\n"
  "  round: round coordinates.\n"
  "  debug: print debug messages.\n"
  "\n"
  "Output:\n"
  "  Autohinted glyph data in bez format.\n"
  "\n"
  "Raises:\n"
  "  psautohint.error: If authinting fails.\n";

static PyObject*
autohint(PyObject* self, PyObject* args)
{
    int allowEdit = true, roundCoords = true, allowHintSub = true;
    int verbose = true;
    int debug = false;
    PyObject* fontObj = NULL;
    PyObject* inObj = NULL;
    PyObject* outObj = NULL;
    char* inData = NULL;
    char* fontInfo = NULL;
    bool error = false;

    if (!PyArg_ParseTuple(args, "O!O!|iiiii", &PyBytes_Type, &fontObj,
                          &PyBytes_Type, &inObj, &verbose, &allowEdit,
                          &allowHintSub, &roundCoords, &debug))
        return NULL;

    AC_SetMemManager(NULL, memoryManager);
    AC_SetReportCB(reportCB, verbose);

    fontInfo = PyBytes_AsString(fontObj);
    inData = PyBytes_AsString(inObj);
    if (!inData || !fontInfo) {
        error = true;
    } else {
        int result;

        size_t outLen = 4 * strlen(inData);
        char*  output = MEMNEW(outLen);
        if (!output) {
            result = AC_MemoryError;
        } else {
            result =
              AutoColorString(inData, fontInfo, &output, &outLen, allowEdit,
                              allowHintSub, roundCoords, debug);

            if (outLen != 0 && result == AC_Success)
                outObj = PyBytes_FromString(output);

            MEMFREE(output);
        }

        if (result != AC_Success) {
            switch (result) {
                case AC_FontinfoParseFail:
                    PyErr_SetString(PsAutoHintError,
                                    "Parsing font info failed");
                    break;
                case AC_FatalError:
                    PyErr_SetString(PsAutoHintError, "Fatal error");
                    break;
                case AC_MemoryError:
                    PyErr_NoMemory();
                    break;
                case AC_UnknownError:
                    PyErr_SetString(PsAutoHintError, "Hinting failed");
                    break;
                case AC_InvalidParameterError:
                    PyErr_SetString(PyExc_ValueError, "Invalid glyph data");
                    break;
            }
            error = true;
        }
    }

    if (error)
        return NULL;
    return outObj;
}

static char autohintmm_doc[] =
  "Autohint glyphs.\n"
  "\n"
  "Signature:\n"
  "  autohintm(font_info, glyphs[, verbose])\n"
  "\n"
  "Args:\n"
  "  font_info: font information.\n"
  "  glyphs: sequence of glyph data in bez format.\n"
  "  masters: sequence of master names.\n"
  "  verbose: print verbose messages.\n"
  "\n"
  "Output:\n"
  "  Sequence of autohinted glyph data in bez format.\n"
  "\n"
  "Raises:\n"
  "  psautohint.error: If authinting fails.\n";

static PyObject*
autohintmm(PyObject* self, PyObject* args)
{
    int verbose = true;
    PyObject* inSeq = NULL;
    Py_ssize_t inCount = 0;
    PyObject* mastersSeq = NULL;
    Py_ssize_t mastersCount = 0;
    PyObject* fontObj = NULL;
    PyObject* outSeq = NULL;
    char* fontInfo = NULL;
    const char** masters;
    bool error = false;
    Py_ssize_t i;

    if (!PyArg_ParseTuple(args, "O!OO|i", &PyBytes_Type, &fontObj, &inSeq,
                          &mastersSeq, &verbose))
        return NULL;

    inSeq = PySequence_Fast(inSeq, "argument must be sequence");
    mastersSeq = PySequence_Fast(mastersSeq, "argument must be sequence");

    if (!inSeq || !mastersSeq)
        return NULL;

    inCount = PySequence_Fast_GET_SIZE(inSeq);
    mastersCount = PySequence_Fast_GET_SIZE(mastersSeq);
    if (inCount != mastersCount) {
        PyErr_SetString(
          PsAutoHintError,
          "Length of \"glyphs\" must equal length of \"masters\".");
        return NULL;
    }

    if (inCount <= 1) {
        PyErr_SetString(PsAutoHintError, "Length of input glyphs must be > 1");
        return NULL;
    }

    masters = MEMNEW(mastersCount * sizeof(char*));
    if (!masters) {
        PyErr_NoMemory();
        return NULL;
    }

    for (i = 0; i < mastersCount; i++) {
        PyObject* obj = PySequence_Fast_GET_ITEM(mastersSeq, i);
        masters[i] = PyBytes_AsString(obj);
    }

    fontInfo = PyBytes_AsString(fontObj);

    AC_SetMemManager(NULL, memoryManager);
    AC_SetReportCB(reportCB, verbose);

    outSeq = PyTuple_New(inCount);
    if (!outSeq) {
        error = true;
    } else {
        int result;

        const char** inGlyphs = MEMNEW(inCount * sizeof(char*));
        char** outGlyphs = MEMNEW(inCount * sizeof(char*));
        size_t* outputSizes = MEMNEW(inCount * sizeof(size_t*));
        if (!inGlyphs || !outGlyphs || !outputSizes) {
            result = AC_MemoryError;
            goto done;
        }

        for (i = 0; i < inCount; i++) {
            PyObject* glyphObj = PySequence_Fast_GET_ITEM(inSeq, i);
            inGlyphs[i] = PyBytes_AsString(glyphObj);
            outputSizes[i] = 4 * strlen(inGlyphs[i]);
            outGlyphs[i] = MEMNEW(outputSizes[i]);
        }

        result = AutoColorStringMM(inGlyphs, fontInfo, mastersCount, masters,
                                   outGlyphs, outputSizes);
        if (result == AC_Success) {
            for (i = 0; i < inCount; i++) {
                PyObject* outObj = PyBytes_FromString(outGlyphs[i]);
                PyTuple_SET_ITEM(outSeq, i, outObj);
            }
        }

    done:
        if (outGlyphs) {
            for (i = 0; i < inCount; i++)
                MEMFREE(outGlyphs[i]);
        }
        MEMFREE(inGlyphs);
        MEMFREE(outGlyphs);
        MEMFREE(outputSizes);

        if (result != AC_Success) {
            switch (result) {
                case AC_FontinfoParseFail:
                    PyErr_SetString(PsAutoHintError,
                                    "Parsing font info failed");
                    break;
                case AC_FatalError:
                    PyErr_SetString(PsAutoHintError, "Fatal error");
                    break;
                case AC_MemoryError:
                    PyErr_NoMemory();
                    break;
                case AC_UnknownError:
                    PyErr_SetString(PsAutoHintError, "Hinting failed");
                    break;
                case AC_InvalidParameterError:
                    PyErr_SetString(PyExc_ValueError, "Invalid glyph data");
                    break;
            }
            error = true;
        }
    }

    MEMFREE(masters);

    Py_XDECREF(inSeq);
    Py_XDECREF(mastersSeq);

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
