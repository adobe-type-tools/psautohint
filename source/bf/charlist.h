/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/

extern void AddCharListEntry(
char *, char *, int32_t, int32_t, bool, bool, bool, bool
);

extern bool CheckCharListEntry(
char *, char *, bool, bool, bool, bool, indx
);

extern bool NameInCharList (
char *
);

extern indx FindCharListEntry(
char *
);

/*
extern void makecharlist(
bool, bool, char *, indx
);*/

extern void sortcharlist(
bool
);

extern bool CompareCharListToBez (
char *, bool, indx, bool
);
/*
extern void CopyCharListToTable (
void
);*/

extern void CheckAllListInTable (
bool
);

extern void FreeCharList (
void
);

extern void ResetCharListBools(
bool, indx
);

