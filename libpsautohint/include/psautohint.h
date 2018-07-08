/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

/* The following ifdef block is the standard way of creating macros which make
 * exporting from a DLL simpler. All files within this DLL are compiled with
 * the ACLIB_API symbol defined on the command line. This symbol should not be
 * defined on any project that uses this DLL. This way any other project whose
 * source files include this file see ACLIB_API functions as being imported
 * from a DLL, wheras this DLL sees symbols defined with this macro as being
 * exported.
*/
#ifndef PSAUTOHINT_H_
#define PSAUTOHINT_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef AC_C_LIB_EXPORTS

#ifdef _WIN32
#define ACLIB_API __declspec(dllexport)
#else

#if __GNUC__ && __MACH__
#define ACLIB_API __attribute__((visibility("default")))
#endif

#endif

#else
#define ACLIB_API
#endif

enum
{
    AC_Success,
    AC_FontinfoParseFail,
    AC_FatalError,
    AC_MemoryError,
    AC_UnknownError,
    AC_InvalidParameterError
};

/*
 * Function: AC_getVersion
 *
 * Returns AC library version.
 */
ACLIB_API const char* AC_getVersion(void);

/*
 * Function: AC_SetMemManager
 *
 * If this is supplied, then the AC lib will call this function for all memory
 * allocations. Otherwise it will use alloc/malloc/free.
 */
typedef void* (*AC_MEMMANAGEFUNCPTR)(void* ctxptr, void* old, size_t size);

ACLIB_API void AC_SetMemManager(void* ctxptr, AC_MEMMANAGEFUNCPTR func);

/*
 * Function: AC_SetReportCB
 *
 * If this is supplied, then the AC lib will use this call back to report
 * messages.
 *
 */
typedef void (*AC_REPORTFUNCPTR)(char* msg, int level);

ACLIB_API void AC_SetReportCB(AC_REPORTFUNCPTR reportCB);

/*
 * Function: AC_SetReportStemsCB
 *
 * If this is called, then the AC lib will write all the stem widths it
 * encounters.
 *
 * If allStems is false, then stems defined by curves are excluded from the
 * reporting.
 *
 * Note that the callbacks should not dispose of the glyphName memory; that
 * belongs to the AC lib. It should be copied immediately - it may may last
 * past the return of the callback.
 */
typedef void (*AC_REPORTSTEMPTR)(int top, int bottom, char* glyphName);

ACLIB_API void AC_SetReportStemsCB(AC_REPORTSTEMPTR hstemCB,
                                   AC_REPORTSTEMPTR vstemCB,
                                   unsigned int allStems);

/*
 * Function: AC_SetReportZonesCB
 *
 * If this is called , then the AC lib will write all the alignment zones it
 * encounters.
 *
 * Note that the callbacks should not dispose of the glyphName memory; that
 * belongs to the AC lib. It should be copied immediately - it may may last
 * past the return of the callback.
 */
typedef void (*AC_REPORTZONEPTR)(int top, int bottom, char* glyphName);

ACLIB_API void AC_SetReportZonesCB(AC_REPORTZONEPTR charCB,
                                   AC_REPORTZONEPTR stemCB);

/*
 * Function: AC_SetReportRetryCB
 *
 * If this is called, then the AC lib will call this function when it wants to
 * discard the previous log content and start from scratch.
 *
 * This is to be used when AC_SetReportZonesCB or AC_SetReportStemsCB are used.
 */
typedef void (*AC_RETRYPTR)(void);

ACLIB_API void AC_SetReportRetryCB(AC_RETRYPTR retryCB);

/*
 * Function: AutoColorString
 *
 * This function takes srcbezdata, a pointer to null terminated C string
 * containing glyph data in the bez format (see bez spec) and fontinfo, a
 * pointer to null terminated C string containing fontinfo for the bez glyph.
 *
 * Hint information is added to the bez data and returned to the caller through
 * the buffer dstbezdata. dstbezdata must be allocated before the call and a
 * pointer to its length passed as *length. If the space allocated is
 * insufficient for the target bezdata, it will be reallocated as needed.
 */
ACLIB_API int AutoColorString(const char* srcbezdata, const char* fontinfo,
                              char** dstbezdata, size_t* length, int allowEdit,
                              int allowHintSub, int roundCoords);

/*
 * Function: AutoColorStringMM
 *
 */
ACLIB_API int AutoColorStringMM(const char** srcbezdata, const char* fontinfo,
                                int nmasters, const char** masters,
                                char** dstbezdata, size_t* lengths);

/*
 * Function: AC_initCallGlobals
 *
 * This function must be called in the case where the program is switching
 * between any of the auto-hinting and stem reporting modes while running.
 */
ACLIB_API void AC_initCallGlobals(void);

#ifdef __cplusplus
}
#endif
#endif /* PSAUTOHINT_H_ */
