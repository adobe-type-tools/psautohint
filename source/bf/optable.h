/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/


#define ESCVAL 100
#define MAXOPLEN 5

extern void GetOperator(
  int16_t, char *
);

extern int16_t op_known(
    char *
);

extern void init_ops(
    void
);

extern void freeoptable (
    void
);
