/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#include "ac.h"
#include <stdlib.h>

#include <sys/stat.h>
#include <sys/types.h>

const char* C_ProgramVersion = "1.65240";
const char* reportExt = ".rpt";
const char* dfltExt = ".new";
char* bezName = NULL;
char* fileSuffix = NULL;
FILE* reportFile = NULL;

bool verbose = true; /* if true don't number of characters processed. */

static void openReportFile(char* name, char* fSuffix);

static void
printVersions(void)
{
    fprintf(OUTPUTBUFF, "C program version %s. lib version %s.\n",
            C_ProgramVersion, AC_getVersion());
}

static void
printUsage(void)
{
    fprintf(OUTPUTBUFF, "Usage: autohintexe [-u] [-h]\n");
    fprintf(OUTPUTBUFF, "       autohintexe  -f <font info name> [-e] [-n] "
                        "[-q] [-s <suffix>] [-ra] [-rs] -a] [<file1> <file2> "
                        "... <filen>]\n");
    printVersions();
}

static void
printHelp(void)
{
    printUsage();
    fprintf(OUTPUTBUFF, "   -u usage\n");
    fprintf(OUTPUTBUFF, "   -h help message\n");
    fprintf(OUTPUTBUFF, "   -e do not edit (change) the paths when hinting\n");
    fprintf(OUTPUTBUFF, "   -n no multiple layers of coloring\n");
    fprintf(OUTPUTBUFF, "   -q quiet\n");
    fprintf(OUTPUTBUFF, "   -f <name> path to font info file\n");
    fprintf(OUTPUTBUFF, "   -i <font info string> This can be used instead of "
                        "the -f parameter for data input \n");
    fprintf(OUTPUTBUFF,
            "   <name1> [name2]..[nameN]  paths to glyph bez files\n");
    fprintf(OUTPUTBUFF, "   -b the last argument is bez data instead of a file "
                        "name and the result will go to stdOut\n");
    fprintf(
      OUTPUTBUFF,
      "   -s <suffix> Write output data to 'file name' + 'suffix', rather\n");
    fprintf(
      OUTPUTBUFF,
      "       than writing it to the same file name as the input file.\n");
    fprintf(OUTPUTBUFF, "   -ra Write alignment zones data. Does not hint or "
                        "change glyph. Default extension is '.rpt'\n");
    fprintf(OUTPUTBUFF, "   -rs Write stem widths data. Does not hint or "
                        "change glyph. Default extension is '.rpt'\n");
    fprintf(OUTPUTBUFF, "   -a Modifies -ra and -rs: Includes stems between "
                        "curved lines: default is to omit these.\n");
    fprintf(OUTPUTBUFF, "   -v print versions.\n");
}

static int
main_cleanup(int16_t code)
{
    if (code != AC_Success)
        exit(code);

    return 0;
}

static void
charZoneCB(Fixed top, Fixed bottom, char* glyphName)
{
    if (reportFile)
        fprintf(reportFile, "charZone %s top %f bottom %f\n", glyphName,
                top / 256.0, bottom / 256.0);
}

static void
stemZoneCB(Fixed top, Fixed bottom, char* glyphName)
{
    if (reportFile)
        fprintf(reportFile, "stemZone %s top %f bottom %f\n", glyphName,
                top / 256.0, bottom / 256.0);
}

static void
hstemCB(Fixed top, Fixed bottom, char* glyphName)
{
    if (reportFile)
        fprintf(reportFile, "HStem %s top %f bottom %f\n", glyphName,
                top / 256.0, bottom / 256.0);
}

static void
vstemCB(Fixed right, Fixed left, char* glyphName)
{
    if (reportFile)
        fprintf(reportFile, "VStem %s right %f left %f\n", glyphName,
                right / 256.0, left / 256.0);
}

static void
reportCB(char* msg)
{
    fprintf(OUTPUTBUFF, "%s", msg);
    fprintf(OUTPUTBUFF, "\n");
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
        fprintf(OUTPUTBUFF, "Error. Could not open file '%s'. Please check "
                            "that it exists and is not write-protected.\n",
                name);
        main_cleanup(FATALERROR);
    }

    if (filestat.st_size == 0) {
        fprintf(OUTPUTBUFF, "Error. File '%s' has zero size.\n", name);
        main_cleanup(FATALERROR);
    }

    data = malloc(filestat.st_size + 1);
    if (data == NULL) {
        fprintf(OUTPUTBUFF,
                "Error. Could not allcoate memory for contents of file %s.\n",
                name);
        main_cleanup(FATALERROR);
    } else {
        size_t fileSize = 0;
        FILE* fp = fopen(name, "r");
        if (fp == NULL) {
            fprintf(OUTPUTBUFF, "Error. Could not open file '%s'. Please check "
                                "that it exists and is not write-protected.\n",
                    name);
            main_cleanup(FATALERROR);
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
    size_t nameSize = 1 + strlen(name);
    char* savedName;
    int usedNewName = 0;

    if ((fSuffix != NULL) && (fSuffix[0] != '\0')) {
        nameSize += strlen(fSuffix);
        savedName = malloc(nameSize);
        savedName[0] = '\0';
        strcat(savedName, name);
        strcat(savedName, fSuffix);
        usedNewName = 1;
    } else
        savedName = malloc(nameSize);

    fp = fopen(savedName, "w");
    fwrite(output, 1, strlen(output), fp);
    fclose(fp);

    if (usedNewName)
        free(savedName);
}

static void
openReportFile(char* name, char* fSuffix)
{
    size_t nameSize = 1 + strlen(name);
    char* savedName;
    int usedNewName = 0;

    if ((fSuffix != NULL) && (fSuffix[0] != '\0')) {
        nameSize += strlen(fSuffix);
        savedName = malloc(nameSize);
        savedName[0] = '\0';
        strcat(savedName, name);
        strcat(savedName, fSuffix);
        usedNewName = 1;
    } else
        savedName = malloc(nameSize);

    reportFile = fopen(savedName, "w");

    if (usedNewName)
        free(savedName);
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

    int allowEdit, roundCoords, allowHintSub, debug, badParam, allStems;
    bool argumentIsBezData = false;
    char* fontInfoFileName =
      NULL; /* font info file name, or suffix of environment variable holding
               the fontfino string. */
    char* fontinfo = NULL;       /* the string of fontinfo data */
    int firstFileNameIndex = -1; /* arg index for first bez file name, or
                                    suffix of environment variable holding the
                                    bez string. */

    char* current_arg;
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
        current_arg = argv[argi];

        if (current_arg[0] == '\0') {
            continue;
        } else if (current_arg[0] != '-') {
            if (firstFileNameIndex == -1) {
                firstFileNameIndex = argi;
            }
            total_files++;
            continue;
        } else if (firstFileNameIndex != -1) {
            fprintf(OUTPUTBUFF, "Error. Illegal command line. \"-\" option "
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
                break;
            case 'h':
                printHelp();
                exit(0);
                break;
            case 'e':
                allowEdit = false;
            case 'd':
                roundCoords = false;
                break;
            case 'b':
                argumentIsBezData = true;
                break;
            case 'f':
                if (fontinfo != NULL) {
                    fprintf(OUTPUTBUFF, "Error. Illegal command line. \"-f\" "
                                        "can’t be used together with the "
                                        "\"-i\" command.\n");
                    exit(1);
                }
                fontInfoFileName = argv[++argi];
                if ((fontInfoFileName[0] == '\0') ||
                    (fontInfoFileName[0] == '-')) {
                    fprintf(OUTPUTBUFF, "Error. Illegal command line. \"-f\" "
                                        "option must be followed by a file "
                                        "name.\n");
                    exit(1);
                }
                fontinfo = getFileData(fontInfoFileName);
                break;
            case 'i':
                if (fontinfo != NULL) {
                    fprintf(OUTPUTBUFF, "Error. Illegal command line. \"-i\" "
                                        "can’t be used together with the "
                                        "\"-f\" command.\n");
                    exit(1);
                }
                fontinfo = argv[++argi];
                if ((fontinfo[0] == '\0') || (fontinfo[0] == '-')) {
                    fprintf(OUTPUTBUFF, "Error. Illegal command line. \"-i\" "
                                        "option must be followed by a font "
                                        "info string.\n");
                    exit(1);
                }
                break;
            case 's':
                fileSuffix = argv[++argi];
                if ((fileSuffix[0] == '\0') || (fileSuffix[0] == '-')) {
                    fprintf(OUTPUTBUFF, "Error. Illegal command line. \"-s\" "
                                        "option must be followed by a string, "
                                        "and the string must not begin with "
                                        "'-'.\n");
                    exit(1);
                }
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
                        reportRetryCB = reportRetry;
                        AC_SetReportZonesCB(charZoneCB, stemZoneCB);
                        break;
                    case 's':
                        reportRetryCB = reportRetry;
                        AC_SetReportStemsCB(hstemCB, vstemCB, allStems);
                        break;
                    default:
                        fprintf(OUTPUTBUFF,
                                "Error. %s is an invalid parameter.\n",
                                current_arg);
                        badParam = true;
                        break;
                }
                break;
            case 'v':
                printVersions();
                exit(0);
                break;
                break;
            default:
                fprintf(OUTPUTBUFF, "Error. %s is an invalid parameter.\n",
                        current_arg);
                badParam = true;
                break;
        }
    }

    if (firstFileNameIndex == -1) {
        fprintf(OUTPUTBUFF,
                "Error. Illegal command line. Must provide bez file name.\n");
        badParam = true;
    }
    if (fontInfoFileName == NULL) {
        fprintf(
          OUTPUTBUFF,
          "Error. Illegal command line. Must provide font info file name.\n");
        badParam = true;
    }

    if (badParam)
        exit(NONFATALERROR);

    AC_SetReportCB(reportCB, verbose);
    argi = firstFileNameIndex - 1;
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

        if (!argumentIsBezData && (doAligns || doStems)) {
            openReportFile(bezName, fileSuffix);
        }

        result = AutoColorString(bezdata, fontinfo, output, (int*)&outputsize,
                                 allowEdit, allowHintSub, roundCoords, debug);
        if (result == AC_DestBuffOfloError) {
            if (reportFile != NULL) {
                closeReportFile();
                if (!argumentIsBezData && (doAligns || doStems)) {
                    openReportFile(bezName, fileSuffix);
                }
            }
            free(output);
            output = malloc(outputsize);
            /* printf("NOTE: trying again. Input size %d output size %d.\n",
             * strlen(bezdata), outputsize); */
            AC_SetReportCB(reportCB, false);
            result =
              AutoColorString(bezdata, fontinfo, output, (int*)&outputsize,
                              allowEdit, allowHintSub, roundCoords, debug);
            AC_SetReportCB(reportCB, verbose);
        }

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
        main_cleanup((result == AC_Success) ? OK : FATALERROR);
    }

    return 0;
}
/* end of main */
