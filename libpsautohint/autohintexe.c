/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "psautohint.h"

static const char* C_ProgramVersion = "1.8";
static const char* reportExt = ".rpt";
static const char* dfltExt = ".new";

static bool verbose = true; /* if true don't number of characters processed. */
static bool debug = false;

static void
printVersions(void)
{
    fprintf(stdout, "C program version %s. lib version %s.\n", C_ProgramVersion,
            AC_getVersion());
}

static void
printUsage(void)
{
    fprintf(stdout, "Usage: autohintexe [-u] [-h]\n");
    fprintf(stdout, "       autohintexe  -f <font info name> [-e] [-n] "
                    "[-q] [-s <suffix>] [-ra] [-rs] -a] [<file1> <file2> "
                    "... <filen>]\n");
    printVersions();
}

static void
printHelp(void)
{
    printUsage();
    fprintf(stdout, "   -u usage\n");
    fprintf(stdout, "   -h help message\n");
    fprintf(stdout, "   -e do not edit (change) the paths when hinting\n");
    fprintf(stdout, "   -n no multiple layers of hinting\n");
    fprintf(stdout, "   -q quiet\n");
    fprintf(stdout, "   -f <name> path to font info file\n");
    fprintf(stdout, "   -i <font info string> This can be used instead of "
                    "the -f parameter for data input \n");
    fprintf(stdout, "   <name1> [name2]..[nameN]  paths to glyph bez files\n");
    fprintf(stdout, "   -b the last argument is bez data instead of a file "
                    "name and the result will go to stdout\n");
    fprintf(
      stdout,
      "   -s <suffix> Write output data to 'file name' + 'suffix', rather\n");
    fprintf(
      stdout,
      "       than writing it to the same file name as the input file.\n");
    fprintf(stdout, "   -ra Write alignment zones data. Does not hint or "
                    "change glyph. Default extension is '.rpt'\n");
    fprintf(stdout, "   -rs Write stem widths data. Does not hint or "
                    "change glyph. Default extension is '.rpt'\n");
    fprintf(stdout, "   -a Modifies -ra and -rs: Includes stems between "
                    "curved lines: default is to omit these.\n");
    fprintf(stdout, "   -v print versions.\n");
}

static void
charZoneCB(float top, float bottom, char* glyphName, void* userData)
{
    ACBufferWriteF((ACBuffer*)userData, "charZone %s top %f bottom %f\n",
                   glyphName, (double)top, (double)bottom);
}

static void
stemZoneCB(float top, float bottom, char* glyphName, void* userData)
{
    ACBufferWriteF((ACBuffer*)userData, "stemZone %s top %f bottom %f\n",
                   glyphName, (double)top, (double)bottom);
}

static void
hstemCB(float top, float bottom, char* glyphName, void* userData)
{
    ACBufferWriteF((ACBuffer*)userData, "HStem %s top %f bottom %f\n",
                   glyphName, (double)top, (double)bottom);
}

static void
vstemCB(float right, float left, char* glyphName, void* userData)
{
    ACBufferWriteF((ACBuffer*)userData, "VStem %s right %f left %f\n",
                   glyphName, (double)right, (double)left);
}

static void
reportCB(char* msg, int level)
{
    switch (level) {
        case AC_LogDebug:
            if (debug)
                fprintf(stderr, "DEBUG: %s\n", msg);
            break;
        case AC_LogInfo:
            if (verbose)
                fprintf(stderr, "INFO: %s\n", msg);
            break;
        case AC_LogWarning:
            if (verbose)
                fprintf(stderr, "WARNING: %s\n", msg);
            break;
        case AC_LogError:
            fprintf(stderr, "ERROR: %s\n", msg);
            break;
        default:
            break;
    }
}

static void
reportRetry(void* userData)
{
    if (userData) {
        ACBuffer* buffer = (ACBuffer*)userData;
        ACBufferReset(buffer);
    }
}

static char*
getFileData(char* name)
{
    char* data;

    struct stat filestat;
    if ((stat(name, &filestat)) < 0) {
        fprintf(stderr,
                "ERROR: Could not open file '%s'. Please check "
                "that it exists and is not write-protected.\n",
                name);
        exit(AC_FatalError);
    }

    if (filestat.st_size == 0) {
        fprintf(stderr, "ERROR: File '%s' has zero size.\n", name);
        exit(AC_FatalError);
    }

    data = malloc(filestat.st_size + 1);
    if (data == NULL) {
        fprintf(stderr,
                "ERROR: Could not allcoate memory for contents of file %s.\n",
                name);
        exit(AC_FatalError);
    } else {
        size_t fileSize = 0;
        FILE* fp = fopen(name, "r");
        if (fp == NULL) {
            fprintf(stderr,
                    "ERROR: Could not open file '%s'. Please check "
                    "that it exists and is not write-protected.\n",
                    name);
            exit(AC_FatalError);
        }
        fileSize = fread(data, 1, filestat.st_size, fp);
        data[fileSize] = 0;
        fclose(fp);
    }
    return data;
}

static void
writeFileData(char* name, char* output, size_t outputsize, const char* fSuffix)
{
    FILE* fp;

    if ((fSuffix != NULL) && (fSuffix[0] != '\0')) {
        char* savedName;
        size_t nameSize;
        nameSize = strlen(name) + strlen(fSuffix) + 1;
        savedName = malloc(nameSize);
        savedName[0] = '\0';
        strcat(savedName, name);
        strcat(savedName, fSuffix);
        fp = fopen(savedName, "w");
        free(savedName);
    } else
        fp = fopen(name, "w");

    fwrite(output, 1, outputsize, fp);
    fclose(fp);
}

static FILE*
openReportFile(char* name, const char* fSuffix)
{
    FILE* file;
    if (fSuffix != NULL && fSuffix[0] != '\0') {
        char* savedName;
        size_t nameSize;
        nameSize = strlen(name) + strlen(fSuffix) + 1;
        savedName = malloc(nameSize);
        savedName[0] = '\0';
        strcat(savedName, name);
        strcat(savedName, fSuffix);
        file = fopen(savedName, "w");
        free(savedName);
    } else
        file = fopen(name, "w");
    return file;
}

int
main(int argc, char* argv[])
{
    /* See the discussion in the function definition for:
     autohintlib:control.c:Blues()
     static void Blues()
     */

    bool allowEdit, roundCoords, allowHintSub, badParam, allStems;
    bool argumentIsBezData = false;
    bool doMM = false;
    bool report_zones = false, report_stems = false;
    char* fontInfoFileName = NULL; /* font info file name, or suffix of
                                      environment variable holding
                                      the fontfino string. */
    char* fontinfo = NULL;         /* the string of fontinfo data */
    int firstFileNameIndex = -1;   /* arg index for first bez file name, or
                                      suffix of environment variable holding the
                                      bez string. */
    const char* fileSuffix = dfltExt;
    int total_files = 0;
    int result, argi;
    ACBuffer* reportBuffer = NULL;

    badParam = false;
    allStems = false;

    allowEdit = allowHintSub = roundCoords = true;

    /* read in options */
    argi = 0;
    while (++argi < argc) {
        char* current_arg = argv[argi];

        if (current_arg[0] == '\0') {
            continue;
        } else if (current_arg[0] != '-') {
            if (firstFileNameIndex == -1) {
                firstFileNameIndex = argi;
            }
            total_files++;
            continue;
        } else if (firstFileNameIndex != -1) {
            fprintf(stderr, "ERROR: Illegal command line. \"-\" option "
                            "found after first file name.\n");
            exit(1);
        }

        switch (current_arg[1]) {
            case '\0':
                badParam = true;
                break;
            case 'u':
                printUsage();
                exit(0);
            case 'h':
                printHelp();
                exit(0);
            case 'e':
                allowEdit = false;
                break;
            case 'd':
                roundCoords = false;
                break;
            case 'b':
                argumentIsBezData = true;
                break;
            case 'f':
                if (fontinfo != NULL) {
                    fprintf(stderr, "ERROR: Illegal command line. \"-f\" "
                                    "can’t be used together with the "
                                    "\"-i\" command.\n");
                    exit(1);
                }
                fontInfoFileName = argv[++argi];
                if ((fontInfoFileName[0] == '\0') ||
                    (fontInfoFileName[0] == '-')) {
                    fprintf(stderr, "ERROR: Illegal command line. \"-f\" "
                                    "option must be followed by a file "
                                    "name.\n");
                    exit(1);
                }
                fontinfo = getFileData(fontInfoFileName);
                break;
            case 'i':
                if (fontinfo != NULL) {
                    fprintf(stderr, "ERROR: Illegal command line. \"-i\" "
                                    "can’t be used together with the "
                                    "\"-f\" command.\n");
                    exit(1);
                }
                fontinfo = argv[++argi];
                if ((fontinfo[0] == '\0') || (fontinfo[0] == '-')) {
                    fprintf(stderr, "ERROR: Illegal command line. \"-i\" "
                                    "option must be followed by a font "
                                    "info string.\n");
                    exit(1);
                }
                break;
            case 's':
                fileSuffix = argv[++argi];
                if ((fileSuffix[0] == '\0') || (fileSuffix[0] == '-')) {
                    fprintf(stderr, "ERROR: Illegal command line. \"-s\" "
                                    "option must be followed by a string, "
                                    "and the string must not begin with "
                                    "'-'.\n");
                    exit(1);
                }
                break;
            case 'm':
                doMM = true;
                break;
            case 'n':
                allowHintSub = false;
                break;
            case 'q':
                verbose = false;
                break;
            case 'D':
                debug = true;
                break;
            case 'a':
                allStems = true;
                break;

            case 'r':
                allowEdit = allowHintSub = false;
                fileSuffix = reportExt;
                switch (current_arg[2]) {
                    case 'a':
                        report_zones = true;
                        break;
                    case 's':
                        report_stems = true;
                        break;
                    default:
                        fprintf(stderr, "ERROR: %s is an invalid parameter.\n",
                                current_arg);
                        badParam = true;
                        break;
                }
                break;
            case 'v':
                printVersions();
                exit(0);
            default:
                fprintf(stderr, "ERROR: %s is an invalid parameter.\n",
                        current_arg);
                badParam = true;
                break;
        }
    }

    if (report_zones || report_stems) {
        reportBuffer = ACBufferNew(150);
        AC_SetReportRetryCB(reportRetry, (void*)reportBuffer);
    }

    if (report_zones)
        AC_SetReportZonesCB(charZoneCB, stemZoneCB, (void*)reportBuffer);

    if (report_stems)
        AC_SetReportStemsCB(hstemCB, vstemCB, allStems, (void*)reportBuffer);

    if (firstFileNameIndex == -1) {
        fprintf(stderr,
                "ERROR: Illegal command line. Must provide bez file name.\n");
        badParam = true;
    }
    if (fontinfo == NULL) {
        fprintf(stderr,
                "ERROR: Illegal command line. Must provide font info.\n");
        badParam = true;
    }

    if (badParam)
        exit(AC_InvalidParameterError);

    AC_SetReportCB(reportCB);
    argi = firstFileNameIndex - 1;
    if (!doMM) {
        while (++argi < argc) {
            char* bezdata;
            ACBuffer* output;
            char* bezName = argv[argi];
            if (!argumentIsBezData) {
                bezdata = getFileData(bezName);
            } else {
                bezdata = bezName;
            }
            output = ACBufferNew(4 * strlen(bezdata));

            if (reportBuffer)
                ACBufferReset(reportBuffer);

            result = AutoHintString(bezdata, fontinfo, output, allowEdit,
                                    allowHintSub, roundCoords);
            if (!argumentIsBezData)
                free(bezdata);

            if (result == AC_Success) {
                char* data;
                size_t len;
                if (reportBuffer) {
                    ACBufferRead(reportBuffer, &data, &len);
                    if (!argumentIsBezData) {
                        FILE* file = openReportFile(bezName, fileSuffix);
                        fwrite(data, 1, len, file);
                        fclose(file);
                    } else {
                        fwrite(data, 1, len, stdout);
                    }
                } else {
                    ACBufferRead(output, &data, &len);
                    if (!argumentIsBezData)
                        writeFileData(bezName, data, len, fileSuffix);
                    else
                        fwrite(data, 1, len, stdout);
                }
            }

            ACBufferFree(output);
            output=NULL;
            if (result != AC_Success)
                exit(result);
        }
    } else /* assume files are MM bez files */
    {
        /** MM support */
        char**
          masters; /* master names - here, harwired to 001 - <num files>-1 */
        char** inGlyphs;       /* Input bez data */
        ACBuffer** outGlyphs;  /* output bez data */
        ACBuffer* hintedGlyph; /* reference hinted glyph */
        int i;

        masters = malloc(sizeof(char*) * total_files);
        inGlyphs = malloc(sizeof(char*) * total_files);
        outGlyphs = malloc(sizeof(ACBuffer*) * total_files);

        argi = firstFileNameIndex - 1;
        for (i = 0; i < total_files; i++) {
            char* bezName;
            argi++;
            bezName = argv[argi];
            masters[i] = malloc(strlen(bezName) + 1);
            strcpy(masters[i], bezName);
            inGlyphs[i] = getFileData(bezName);
            outGlyphs[i] = ACBufferNew(4 * strlen(inGlyphs[i]));
        }

        hintedGlyph = ACBufferNew(4 * strlen(inGlyphs[0]));

        result = AutoHintString(inGlyphs[0], fontinfo, hintedGlyph, allowEdit,
                                allowHintSub, roundCoords);
        if (result != AC_Success)
            exit(result);

        free(inGlyphs[0]);
        {
            char* data;
            size_t len;
            ACBufferRead(hintedGlyph, &data, &len);
            strncpy(inGlyphs[0], data, len);
        }
        result = AutoHintStringMM((const char**)inGlyphs, total_files,
                                  (const char**)masters, outGlyphs);

        for (i = 0; i < total_files; i++) {
            char* data;
            size_t len;
            ACBufferRead(outGlyphs[i], &data, &len);
            writeFileData(masters[i], data, len, "new");
            free(masters[i]);
            free(inGlyphs[i]);
            ACBufferFree(outGlyphs[i]);
            outGlyphs[i] = NULL;
        }
        free(inGlyphs);
        free(outGlyphs);
        free(masters);
        if (result != AC_Success)
            exit(result);
    }

    if (fontInfoFileName)
        free(fontinfo);

    ACBufferFree(reportBuffer);
    reportBuffer = NULL;
    AC_initCallGlobals(); /* clear out references to reportBuffer */

    return 0;
}
/* end of main */
