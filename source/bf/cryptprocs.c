/* Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
This software is licensed as OpenSource, under the Apache License, Version 2.0. This license is available at: http://opensource.org/licenses/Apache-2.0. */
/***********************************************************************/

#include "buildfont.h"
#include "cryptdefs.h"
#include "machinedep.h"

#define C1e 1069928045
#define C2e  226908351
#define C1o  902381661
#define C2o  341529579
#define keye	121201
#define keyo	 11586
#define keyf  85294471
#define keyc   6492394

#define MAXLENIV 4

/* Globals used outside this source file */
int16_t lenIV = 4;

/* Globals used by encryption routines. */
static int32_t C1, C2, key, lineLength, lineCount;
static uint32_t innerrndnum, outerrndnum;
static char hexmap[] = "0123456789abcdef";
static int outerlenIV = 4;

/* Globals used by decryption routines. */
static int32_t dec1, dec2, inchars;
static uint32_t randno;
static int16_t decrypttype;

/* Globals used by both encryption and decryption routines. */
static int32_t format;

/* Prototypes */

static char *InputEnc(
    char *, bool, uint32_t *
);

static void InitEncrypt(
    FILE *, bool
);

static uint32_t DoDecrypt(
FILE *, char *, bool, int32_t, bool, uint32_t, int16_t, int32_t, bool
);

static void SetCryptGlobs(
    int, int32_t, bool, int32_t *, int32_t *, int32_t *
);

#define InitRnum(r,s) {r = (uint32_t) s;}
#define Rnum8(r)\
  ((uint32_t) (r = (((r*C1)+C2) & 0x3fffffff)>>8) & 0xFF)
#define Encrypt(r, clear, cipher)\
  r = ((uint32_t) ((cipher = ((clear)^(r>>8)) & 0xFF) + r)*C1 + C2)

#define DoublyEncrypt(r0, r1, clear, cipher) \
  r0 = ((uint32_t) ((cipher = ((clear)^(r0 >>8)) & 0xFF) + r0)*C1 + C2);\
  r1 = ((uint32_t) ((cipher = ((cipher)^(r1 >>8)) & 0xFF) + r1)*C1 + C2);

#define Decrypt(r, clear, cipher)\
  clear = (unsigned char) ((cipher)^(r>>8)) & 0xFF; r = (uint32_t) ((cipher) + r)*dec1 + dec2

#define OutputEnc(ch, stm)\
  if (format == BIN) (void) putc(ch, stm);\
  else {\
    (void) putc(hexmap[(ch>>4)&0xF], stm); (void) putc(hexmap[ch&0xF], stm);\
    if (--lineCount <= 0) {(void) putc('\n', stm); lineCount = lineLength/2;}}

#define get_char(ch, stream, filein)\
  if (filein) ch = (int32_t) getc((FILE *) stream);\
  else ch = (int32_t) *stm++;\
  inchars++;

static void SetCryptGlobs(int crypttype, int32_t formattype, bool chardata, int32_t *c1, int32_t *c2, int32_t *k)
{
  format = formattype;
  switch (crypttype)
  {
  case EEXEC:
    *c1 = C1e;
    *c2 = C2e;
    *k = (chardata ? keyc : keye);
    return;
    break;
  case OTHER:
    *c1 = C1o;
    *c2 = C2o;
    *k = keyo;
    return;
    break;
  case FONTPASSWORD:
    *c1 = C1o;
    *c2 = C2o;
    *k = keyf;
    return;
    break;
  }
  LogMsg("Invalid crypt params.\n", LOGERROR, NONFATALERROR, true);
}

static char *InputEnc(char * stm, bool fileinput, uint32_t *cipher)
{
  register int32_t c1, c2;

  get_char(c1, stm, fileinput);
  if (format == BIN)
  {
    *cipher = c1;
    return (stm);
  }
  while (c1 <= ' ')
  {
    if (c1 == EOF)
    {
      *cipher = EOF;
      return stm;
    }
    if (decrypttype == OTHER)
    {
      get_char(c1, stm, fileinput);
    }
    else
      break;
  }
  get_char(c2, stm, fileinput);
  while (c2 <= ' ')
  {
    if (c2 == EOF)
    {
      *cipher = EOF;
      return stm;
    }
    if (decrypttype == OTHER)
    {
      get_char(c2, stm, fileinput);
    }
    else
      break;
  }
  *cipher = ((c1 <= '9' ? c1 : c1 + 9) & 0xF) << 4 | ((c2 <= '9' ? c2 : c2 + 9) & 0xF);
  return (stm);
}

static char *InputPlain(stm, fileinput, clear)
char *stm;
bool fileinput;
uint32_t *clear;
{
  register int32_t c1;

  get_char(c1, stm, fileinput);
  *clear = c1;
  return (stm);
}

void SetLenIV (int16_t len)
{
  lenIV = len;
}

/* innerrndnum is a global and represents the random seed for the inner level
   of double encryption.  outerrndnum is the random seed for the outer level
   or single encryption.  This procedure generates <n> random bytes
   and ensures that the first random byte is not a whitespace character
   and that at least one of the random bytes is not one of the
   ASCII hexadecimal character codes.
   The PS interpreter depends on this when decrypting and uses
   this info to decide whether data is binary or hex encrypted. */
static void InitEncrypt(FILE *outstream, bool dblenc)
{
  register indx j;
  bool ok = false;
  uint32_t randomseed, clear, cipher, origouterrndnum;
  time_t tm;
  int16_t keylen = (dblenc?lenIV:outerlenIV);
  char initVec[MAXLENIV];

  get_time(&tm);
  InitRnum(randomseed, (uint32_t) tm);
  origouterrndnum = outerrndnum;
  while (!ok)
  {
    InitRnum(innerrndnum, key);
    if (!dblenc)
    { /* {}'s needed for enclosing macro call. */
      InitRnum(outerrndnum, key);
    }
    else
      outerrndnum = origouterrndnum;
    for (j = 0; j < keylen; j++)
    {
      clear = Rnum8(randomseed);
      if (dblenc)
        Encrypt(innerrndnum, clear, cipher);
      else
        Encrypt(outerrndnum, clear, cipher);
      if (j == 0 && (cipher == ' ' || cipher == '\t' || cipher == '\n'
        || cipher == '\r'))
         break;
      if ((cipher < '0' || cipher > '9') && (cipher < 'A' || cipher > 'F')
        && (cipher < 'a' || cipher > 'f'))
          ok = true;
      if (dblenc)
        Encrypt(outerrndnum, cipher, cipher);
      initVec[j] = (char)(cipher & 0xFF);
    }
  }
  lineCount = lineLength / 2;
  for (j = 0; j < keylen; j++)
    OutputEnc(initVec[j], outstream);
}				/* InitEncrypt */

/* innerrndnum and outerrndnum are globals */
extern int32_t ContEncrypt(char *indata, FILE  *outstream, bool fileinput, int32_t incount, bool dblenc)
{
  uint32_t clear;
  int32_t encchars = 0;

  while (true)
  {
    if (fileinput)
    {
      clear = (uint32_t) getc((FILE *) indata);
      if (clear == (uint32_t)EOF)
	break;
    }
    else
    {
      if (encchars == incount)
	break;
      clear = *indata++;
    }
    encchars++;
#ifdef ENCRYPTOUTPUT
    if (dblenc)
    { 
	uint32_t cipher;
		/* {}'s needed for enclosing macro call. */
      DoublyEncrypt(innerrndnum, outerrndnum, clear, cipher);
    }
    else
      Encrypt(outerrndnum, clear, cipher);
    OutputEnc(((char) cipher), outstream);
#else
	putc((char) clear, outstream);
#endif
}
  if (fileerror(outstream))
    return (-1);
  return (encchars);
}				/* ContEncrypt */

int16_t DoInitEncrypt (FILE *outstream, int16_t enctype, int32_t formattype, int32_t linelen, bool chardata)
{
  lineLength = linelen;
  SetCryptGlobs(enctype, formattype, chardata, &C1, &C2, &key);
  InitEncrypt(outstream, false);
  return (0);
}				/* DoInitEncrypt */

/* returns the number of characters encrypted and written */
extern int32_t DoContEncrypt(char * indata, FILE *outstream, bool fileinput, int32_t incount)
{
  uint32_t enccount;
  FILE *instream = (FILE *) indata;

  if (fileinput)
    clearerr(instream);
  else if (incount == INLEN)
    incount = (int32_t)strlen(indata);
  clearerr(outstream);
  enccount = ContEncrypt(indata, outstream, fileinput, incount, false);
  if ((fileinput && fileerror(instream)) || fileerror(outstream))
    return (-1);
  else
    return (enccount);
}				/* DoContEncrypt */

/* Returns the number of characters encrypted and written.  Returns -1 if
 * an error occurred while reading or writing. The incount argument is used
 * only if we are encrypting characters from memory.
 */
int32_t DoEncrypt
  (char *indata, FILE *outstream, bool fileinput, int32_t incount, int16_t enctype, int32_t formattype, int32_t linelen,
    bool chardata, bool dblenc)
{
	uint32_t enccount;
	FILE *instream = (FILE *) indata;
  lineLength = linelen;
  SetCryptGlobs(enctype, formattype, chardata, &C1, &C2, &key);
  if (fileinput)
    clearerr(instream);
  clearerr(outstream);
  InitEncrypt(outstream, dblenc);
  enccount = ContEncrypt(indata, outstream, fileinput, incount, dblenc);
  /* if (format==HEX && lineLength!=MAXINT && lineCount<lineLength/2) (void)
     putc('\n', outstream); */
  if ((fileinput && fileerror(instream)) || fileerror(outstream))
    return (-1);
  else
    return (enccount);
}				/* DoEncrypt */

/* Returns the number of characters written to outdata or if there was
 * an error while reading the input file, returns -1.  Also returns -1
 * if we are writing to a file and an error occurred while writing.
 * GLOBALS MODIFIED by DoDecrypt: format, dec1, dec2
 * DoDecrypt must be called before ContDecrypt since it sets
 * the format and decoding types and the globals dec1 and dec2.
 */

extern int32_t ContDecrypt(char *indata, char *outdata, bool fileinput, bool fileoutput, int32_t incount,
    uint32_t outcount)
{
  uint32_t cipher, clearchars = 0;
  unsigned char clear;
  FILE *instream = (FILE *) indata;
  FILE *outstream = (FILE *) outdata;

  inchars = 0;
  if (fileinput)
    clearerr(instream);
  if (fileoutput)
    clearerr(outstream);
  while (fileinput || (inchars < incount))
  {
    indata = InputEnc(indata, fileinput, &cipher);
    if (cipher == (uint32_t)EOF)
      break;
    Decrypt(randno, clear, cipher);
    if (fileoutput)
      (void) putc((char) clear, outstream);
    else
    {
      if (clearchars > outcount)
	return (-1);
      *outdata++ = clear;
    }
    clearchars++;
  }
  /* Check for possible file errors. */
  if ((fileoutput && fileerror(outstream)) ||
    (fileinput && fileerror(instream)))
    return (-1);
  return (clearchars);
}

static uint32_t DoDecrypt
  (FILE *instream, char *outdata, bool fileinput,		/* whether input is from file or memory */
	int32_t incount, bool fileoutput,		/* whether output is to file or memory */
	uint32_t outcount, int16_t dectype,			/* the type of decryption */
	int32_t formattype, bool chardata)

{
  uint32_t cipher, seed, clearchars = 0;
  unsigned char clear;
  FILE *outstream = (FILE *) outdata;
  char *currPtr = (char *) instream;
  int j;
  int16_t keylen = (chardata?lenIV:outerlenIV);
 
  inchars = 0;
  decrypttype = dectype;
  SetCryptGlobs(dectype, formattype, chardata, &dec1, &dec2, (int32_t *) &seed);
  if (fileinput)
    clearerr(instream);
  if (fileoutput)
    clearerr(outstream);
  InitRnum(randno, seed);
  for (j = 0; j < keylen; j++)
  {
    currPtr = InputEnc(currPtr, fileinput, &cipher);
    /* Check for empty file. */
    if (cipher == (uint32_t)EOF)
      return (0);
    Decrypt(randno, clear, cipher);
    
  }
  while (fileinput || (inchars < incount))
  {
    currPtr = InputEnc(currPtr, fileinput, &cipher);
    if (cipher == (uint32_t)EOF)
      break;
    
    Decrypt(randno, clear, cipher);
    if (fileoutput)
      (void) putc(clear, (FILE *) outdata);
    else
    {
      if (clearchars > outcount)
	return (-1);
      *outdata++ = clear;
    }
    clearchars++;
  }
  /* Since we are using getc and putc, check for file errors. */
  if (fileoutput)
    if (fileerror(((FILE *) outdata)))
      return (-1);
  if (fileinput)
    if (fileerror(instream))
      return (-1);
  return (clearchars);
}				/* DoDecrypt */

static uint32_t DoRead
  (FILE *instream, char *outdata, bool fileinput,		/* whether input is from file or memory */
	int32_t incount, bool fileoutput,		/* whether output is to file or memory */
	uint32_t outcount)

{
  uint32_t clear;
  uint32_t clearchars = 0;
  FILE *outstream = (FILE *) outdata;
  char *currPtr = (char *) instream;
 
  inchars = 0;
  if (fileinput)
    clearerr(instream);
  if (fileoutput)
    clearerr(outstream);
  
  while (fileinput || (inchars < incount))
  {
   	currPtr = InputPlain(currPtr, fileinput, &clear);
    if (clear == (uint32_t)EOF)
      break;
    
   if (fileoutput)
      (void) putc(clear, (FILE *) outdata);
    else
    {
      if (clearchars > outcount)
	return (-1);
      *outdata++ = (char)(clear & 0xFF);
    }
    clearchars++;
  }
  /* Since we are using getc and putc, check for file errors. */
  if (fileoutput)
    if (fileerror(((FILE *) outdata)))
      return (-1);
  if (fileinput)
    if (fileerror(instream))
      return (-1);
  return (clearchars);
}				/* DoRead */


 uint32_t ReadDecFile(FILE *instream, char *filename, char *outbuffer, 
			bool fileinput /* whether input is from file or memory */, int32_t incount, uint32_t outcount, 
			int16_t dectype /* the type of decryption */)
{
  uint32_t cc;
	bool decrypt=1;
	char c;
	
	if(fileinput)
	{
		c=getc(instream);
		ungetc(c, instream);
	}else
		c = *((char*)instream);
	
	if ( (c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F') )
		decrypt=1;
	else
		decrypt=0;
	
	if(decrypt)
		cc = DoDecrypt(instream, outbuffer, fileinput, incount, false, outcount, dectype, HEX, false);
	else
		cc= DoRead(instream, outbuffer, fileinput, incount, false, outcount);
		
  switch (cc)
  {
  case -1:
    sprintf(globmsg, "A file error occurred in the %s file.\n", filename);
    LogMsg(globmsg, LOGERROR, NONFATALERROR, true);
    break;
  }
  return (cc);
}
