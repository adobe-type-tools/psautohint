#########################################################################
#                                                                       #
# Copyright 2014 Adobe Systems Incorporated.                       #
# All rights reserved.                                                  #
#                                                                       #
#########################################################################

# Configuration
OBJ_DIR = .
SRC_DIR = .
STD_OPTS = -m32

# Program
PRG_SRCS = $(SRC_DIR)/main.c
PRG_OBJS = $(OBJ_DIR)/main.o
PRG_TARGET = autohintexe$(EXE)
PRG_LIBS = $(LIB_TARGET)

# Library
LIB_TARGET = $(OBJ_DIR)/autohintlib.a

CFLAGS = $(STD_OPTS) \
	-I$(SRC_DIR)/include/ac \
	-I$(SRC_DIR)/include/extras \
	-I$(SRC_DIR)/ac \
	-I$(SRC_DIR)/bf \
	 -DAC_C_LIB=1 \
	 -DACLIB_EXPORTS \
	 -DIS_LIB

ifeq ($(OS),Windows_NT)
EXE = .exe
STD_OPTS += -DWIN32
CFLAGS += -I$(SRC_DIR)/include/winextras
else
PRG_LIBS += -lm
endif
					 
LIB_SRCS = \
	$(SRC_DIR)/ac/ac.c \
	$(SRC_DIR)/ac/ac_C_lib.c \
	$(SRC_DIR)/ac/acfixed.c \
	$(SRC_DIR)/ac/auto.c \
	$(SRC_DIR)/ac/bbox.c \
	$(SRC_DIR)/ac/bfstub.c \
	$(SRC_DIR)/ac/charprop.c \
	$(SRC_DIR)/ac/check.c \
	$(SRC_DIR)/ac/control.c \
	$(SRC_DIR)/ac/cswrite.c \
	$(SRC_DIR)/ac/enum.c \
	$(SRC_DIR)/ac/eval.c \
	$(SRC_DIR)/ac/fix.c \
	$(SRC_DIR)/ac/flat.c \
	$(SRC_DIR)/ac/fontinfo.c \
	$(SRC_DIR)/ac/gen.c \
	$(SRC_DIR)/ac/head.c \
	$(SRC_DIR)/ac/merge.c \
	$(SRC_DIR)/ac/misc.c \
	$(SRC_DIR)/ac/path.c \
	$(SRC_DIR)/ac/pick.c \
	$(SRC_DIR)/ac/read.c \
	$(SRC_DIR)/ac/report.c \
	$(SRC_DIR)/ac/shuffle.c \
	$(SRC_DIR)/ac/stub.c \
	$(SRC_DIR)/ac/write.c \
	$(SRC_DIR)/bf/charlistpriv.c \
	$(SRC_DIR)/bf/charpath.c \
	$(SRC_DIR)/bf/charpathpriv.c \
	$(SRC_DIR)/bf/cryptprocs.c \
	$(SRC_DIR)/bf/fileops.c \
	$(SRC_DIR)/bf/filookup.c \
	$(SRC_DIR)/bf/illtobez.c \
	$(SRC_DIR)/bf/machinedep.c \
	$(SRC_DIR)/bf/rawPStobez.c

LIB_OBJS = \
	$(OBJ_DIR)/ac.o \
	$(OBJ_DIR)/ac_C_lib.o \
	$(OBJ_DIR)/acfixed.o \
	$(OBJ_DIR)/auto.o \
	$(OBJ_DIR)/bbox.o \
	$(OBJ_DIR)/bfstub.o \
	$(OBJ_DIR)/charprop.o \
	$(OBJ_DIR)/check.o \
	$(OBJ_DIR)/control.o \
	$(OBJ_DIR)/cswrite.o \
	$(OBJ_DIR)/enum.o \
	$(OBJ_DIR)/eval.o \
	$(OBJ_DIR)/fix.o \
	$(OBJ_DIR)/flat.o \
	$(OBJ_DIR)/fontinfo.o \
	$(OBJ_DIR)/gen.o \
	$(OBJ_DIR)/head.o \
	$(OBJ_DIR)/merge.o \
	$(OBJ_DIR)/misc.o \
	$(OBJ_DIR)/path.o \
	$(OBJ_DIR)/pick.o \
	$(OBJ_DIR)/read.o \
	$(OBJ_DIR)/report.o \
	$(OBJ_DIR)/shuffle.o \
	$(OBJ_DIR)/stub.o \
	$(OBJ_DIR)/write.o \
	$(OBJ_DIR)/charlistpriv.o \
	$(OBJ_DIR)/charpath.o \
	$(OBJ_DIR)/charpathpriv.o \
	$(OBJ_DIR)/cryptprocs.o \
	$(OBJ_DIR)/fileops.o \
	$(OBJ_DIR)/filookup.o \
	$(OBJ_DIR)/illtobez.o \
	$(OBJ_DIR)/machinedep.o \
	$(OBJ_DIR)/rawPStobez.o

default: $(PRG_TARGET)

clean:
	rm -f $(PRG_OBJS)
	rm -f $(LIB_OBJS)
	rm -f $(PRG_TARGET)
	rm -f $(LIB_TARGET)

# Program
$(PRG_TARGET): $(PRG_OBJS) $(PRG_SRCS) $(LIB_TARGET)
	$(CC) $(CFLAGS) -o $@ $(PRG_OBJS) $(PRG_LIBS)

main.o:
	$(CC) $(CFLAGS) -c $(SRC_DIR)/main.c -o $@

# Library
$(LIB_TARGET): $(LIB_OBJS)
	$(AR) -rvs $@ $?

absfont.o:
	$(CC) $(CFLAGS) -c $(SRC_DIR)/absfont.c -o $@

ac.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/ac.c -o $@

ac_C_lib.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/ac_C_lib.c -o $@

acfixed.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/acfixed.c -o $@

auto.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/auto.c -o $@

bbox.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/bbox.c -o $@

bfstub.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/bfstub.c -o $@

charprop.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/charprop.c -o $@

check.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/check.c -o $@

control.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/control.c -o $@

cswrite.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/cswrite.c -o $@

enum.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/enum.c -o $@

eval.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/eval.c -o $@

fix.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/fix.c -o $@

flat.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/flat.c -o $@

fontinfo.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/fontinfo.c -o $@

gen.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/gen.c -o $@

head.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/head.c -o $@

merge.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/merge.c -o $@

misc.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/misc.c -o $@

path.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/path.c -o $@

pick.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/pick.c -o $@

read.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/read.c -o $@

report.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/report.c -o $@

shuffle.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/shuffle.c -o $@

stub.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/stub.c -o $@

write.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/ac/write.c -o $@

charlistpriv.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/bf/charlistpriv.c -o $@

charpath.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/bf/charpath.c -o $@

charpathpriv.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/bf/charpathpriv.c -o $@

cryptprocs.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/bf/cryptprocs.c -o $@

fileops.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/bf/fileops.c -o $@

filookup.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/bf/filookup.c -o $@

illtobez.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/bf/illtobez.c -o $@

machinedep.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/bf/machinedep.c -o $@

rawPStobez.o:
	$(CC) $(CFLAGS) -c 	$(SRC_DIR)/bf/rawPStobez.c -o $@
