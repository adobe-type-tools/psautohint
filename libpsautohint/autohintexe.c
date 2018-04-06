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

static const char* C_ProgramVersion = "1.65240";
static const char* reportExt = ".rpt";
static const char* dfltExt = ".new";
static char* bezName = NULL;
static char* fileSuffix = NULL;
static FILE* reportFile = NULL;

static bool verbose = true; /* if true don't number of characters processed. */

static void openReportFile(char* name, char* fSuffix);

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
    fprintf(stdout, "   -n no multiple layers of coloring\n");
    fprintf(stdout, "   -q quiet\n");
    fprintf(stdout, "   -f <name> path to font info file\n");
    fprintf(stdout, "   -i <font info string> This can be used instead of "
                    "the -f parameter for data input \n");
    fprintf(stdout, "   <name1> [name2]..[nameN]  paths to glyph bez files\n");
    fprintf(stdout, "   -b the last argument is bez data instead of a file "
                    "name and the result will go to stdOut\n");
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
charZoneCB(int top, int bottom, char* glyphName)
{
    if (reportFile)
        fprintf(reportFile, "charZone %s top %f bottom %f\n", glyphName,
                top / 256.0, bottom / 256.0);
}

static void
stemZoneCB(int top, int bottom, char* glyphName)
{
    if (reportFile)
        fprintf(reportFile, "stemZone %s top %f bottom %f\n", glyphName,
                top / 256.0, bottom / 256.0);
}

static void
hstemCB(int top, int bottom, char* glyphName)
{
    if (reportFile)
        fprintf(reportFile, "HStem %s top %f bottom %f\n", glyphName,
                top / 256.0, bottom / 256.0);
}

static void
vstemCB(int right, int left, char* glyphName)
{
    if (reportFile)
        fprintf(reportFile, "VStem %s right %f left %f\n", glyphName,
                right / 256.0, left / 256.0);
}

static void
reportCB(char* msg)
{
    fprintf(stdout, "%s", msg);
}

static void
reportRetry(void)
{
    if (reportFile != NULL) {
        fclose(reportFile);
        openReportFile(bezName, fileSuffix);
    }
}

static char*
getFileData(char* name)
{
    char* data;

    struct stat filestat;
    if ((stat(name, &filestat)) < 0) {
        fprintf(stdout, "Error. Could not open file '%s'. Please check "
                        "that it exists and is not write-protected.\n",
                name);
        exit(AC_FatalError);
    }

    if (filestat.st_size == 0) {
        fprintf(stdout, "Error. File '%s' has zero size.\n", name);
        exit(AC_FatalError);
    }

    data = malloc(filestat.st_size + 1);
    if (data == NULL) {
        fprintf(stdout,
                "Error. Could not allcoate memory for contents of file %s.\n",
                name);
        exit(AC_FatalError);
    } else {
        size_t fileSize = 0;
        FILE* fp = fopen(name, "r");
        if (fp == NULL) {
            fprintf(stdout, "Error. Could not open file '%s'. Please check "
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
writeFileData(char* name, char* output, char* fSuffix)
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

    fwrite(output, 1, strlen(output), fp);
    fclose(fp);
}

static void
openReportFile(char* name, char* fSuffix)
{
    if ((fSuffix != NULL) && (fSuffix[0] != '\0')) {
        char* savedName;
        size_t nameSize;
        nameSize = strlen(name) + strlen(fSuffix) + 1;
        savedName = malloc(nameSize);
        savedName[0] = '\0';
        strcat(savedName, name);
        strcat(savedName, fSuffix);
        reportFile = fopen(savedName, "w");
        free(savedName);
    } else
        reportFile = fopen(name, "w");
}

static void
closeReportFile(void)
{
    if (reportFile != NULL)
        fclose(reportFile);
}

int
main(int argc, char* argv[])
{
    /* See the discussion in the function definition for:
     autohintlib:control.c:Blues()
     static void Blues()
     */

    bool allowEdit, roundCoords, allowHintSub, debug, badParam, allStems;
    bool argumentIsBezData = false;
    bool doMM = false;
    bool report = false;
    char* fontInfoFileName = NULL; /* font info file name, or suffix of
                                      environment variable holding
                                      the fontfino string. */
    char* fontinfo = NULL;         /* the string of fontinfo data */
    int firstFileNameIndex = -1;   /* arg index for first bez file name, or
                                      suffix of environment variable holding the
                                      bez string. */

    int16_t total_files = 0;
    int result, argi;

    badParam = false;
    debug = false;
    allStems = false;

    allowEdit = allowHintSub = roundCoords = true;
    fileSuffix = (char*)dfltExt;

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
            fprintf(stdout, "Error. Illegal command line. \"-\" option "
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
                    fprintf(stdout, "Error. Illegal command line. \"-f\" "
                                    "can’t be used together with the "
                                    "\"-i\" command.\n");
                    exit(1);
                }
                fontInfoFileName = argv[++argi];
                if ((fontInfoFileName[0] == '\0') ||
                    (fontInfoFileName[0] == '-')) {
                    fprintf(stdout, "Error. Illegal command line. \"-f\" "
                                    "option must be followed by a file "
                                    "name.\n");
                    exit(1);
                }
                fontinfo = getFileData(fontInfoFileName);
                break;
            case 'i':
                if (fontinfo != NULL) {
                    fprintf(stdout, "Error. Illegal command line. \"-i\" "
                                    "can’t be used together with the "
                                    "\"-f\" command.\n");
                    exit(1);
                }
                fontinfo = argv[++argi];
                if ((fontinfo[0] == '\0') || (fontinfo[0] == '-')) {
                    fprintf(stdout, "Error. Illegal command line. \"-i\" "
                                    "option must be followed by a font "
                                    "info string.\n");
                    exit(1);
                }
                break;
            case 's':
                fileSuffix = argv[++argi];
                if ((fileSuffix[0] == '\0') || (fileSuffix[0] == '-')) {
                    fprintf(stdout, "Error. Illegal command line. \"-s\" "
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
                fileSuffix = (char*)reportExt;
                switch (current_arg[2]) {
                    case 'a':
                        AC_SetReportRetryCB(reportRetry);
                        AC_SetReportZonesCB(charZoneCB, stemZoneCB);
                        report = true;
                        break;
                    case 's':
                        AC_SetReportRetryCB(reportRetry);
                        AC_SetReportStemsCB(hstemCB, vstemCB, allStems);
                        report = true;
                        break;
                    default:
                        fprintf(stdout, "Error. %s is an invalid parameter.\n",
                                current_arg);
                        badParam = true;
                        break;
                }
                break;
            case 'v':
                printVersions();
                exit(0);
            default:
                fprintf(stdout, "Error. %s is an invalid parameter.\n",
                        current_arg);
                badParam = true;
                break;
        }
    }

    if (firstFileNameIndex == -1) {
        fprintf(stdout,
                "Error. Illegal command line. Must provide bez file name.\n");
        badParam = true;
    }
    if (fontInfoFileName == NULL) {
        fprintf(
          stdout,
          "Error. Illegal command line. Must provide font info file name.\n");
        badParam = true;
    }

    if (badParam)
        exit(AC_InvalidParameterError);

    AC_SetReportCB(reportCB, verbose);
    argi = firstFileNameIndex - 1;
    if (!doMM)
    {
        while (++argi < argc) {
            char* bezdata;
            char* output;
            size_t outputsize = 0;
            bezName = argv[argi];
            if (!argumentIsBezData) {
                bezdata = getFileData(bezName);
            } else {
                bezdata = bezName;
            }
            outputsize = 4 * strlen(bezdata);
            output = malloc(outputsize);

            if (!argumentIsBezData && report) {
                openReportFile(bezName, fileSuffix);
            }

            result = AutoColorString(bezdata, fontinfo, &output, &outputsize,
                                     allowEdit, allowHintSub, roundCoords, debug);

            if (reportFile != NULL) {
                closeReportFile();
            } else {
                if ((outputsize != 0) && (result == AC_Success)) {
                    if (!argumentIsBezData) {
                        writeFileData(bezName, output, fileSuffix);
                    } else {
                        printf("%s", output);
                    }
                }
            }

            free(output);
            if (result != AC_Success)
                exit(result);
        }
    }
    else /* assume files are MM bez files */
    {
        /** MM support */
        char** masters; /* master names - here, harwired to 001 - <num files>-1 */
        char** inGlyphs; /* Input bez data */
        char** outGlyphs;/* output bez data */
        size_t* outputSizes; /* size of output data */
        int i;

        masters = malloc(sizeof(char*)*total_files);
        outputSizes = malloc(sizeof(size_t)*total_files);
        inGlyphs = malloc(sizeof(char*)*total_files);
        outGlyphs = malloc(sizeof(char*)*total_files);

        argi = firstFileNameIndex -1;
        for (i = 0; i < total_files; i++)
        {
            argi++;
            bezName = argv[argi];
            masters[i] = malloc(strlen(bezName));
            strcpy(masters[i],bezName);
            inGlyphs[i] = getFileData(bezName);
            outputSizes[i] = 4 * strlen(inGlyphs[i]);
            outGlyphs[i] = malloc(outputSizes[i]);
        }

        result = AutoColorString(inGlyphs[0], fontinfo, &outGlyphs[0], &outputSizes[0],
                                 allowEdit, allowHintSub, roundCoords, debug);
        if (result != AC_Success)
            exit(result);

        free(inGlyphs[0]);
        inGlyphs[0] = malloc(sizeof(char*)*outputSizes[0]);
        strcpy(inGlyphs[0],outGlyphs[0] );
        result = AutoColorStringMM((const char **)inGlyphs, fontinfo,
                                   total_files, (const char **)masters, outGlyphs, outputSizes);

        for (i = 0; i < total_files; i++)
        {
            writeFileData(masters[i], outGlyphs[i], "new");
            free(masters[i]);
            free(inGlyphs[i]);
            free(outGlyphs[i]);
        }
        free(inGlyphs);
        free(outGlyphs);
        free(masters);
        free(outputSizes);
        if (result != AC_Success)
            exit(result);

    }

    return 0;
}
/* end of main */
