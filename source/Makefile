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
PRG_TARGET = $(OBJ_DIR)/autohintexe$(EXE)
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
CFLAGS += -I$(SRC_DIR)/include/winextras
else
PRG_LIBS += -lm
endif
					 
LIB_OBJS = \
	$(OBJ_DIR)/ac/ac.o \
	$(OBJ_DIR)/ac/ac_C_lib.o \
	$(OBJ_DIR)/ac/acfixed.o \
	$(OBJ_DIR)/ac/auto.o \
	$(OBJ_DIR)/ac/bbox.o \
	$(OBJ_DIR)/ac/bfstub.o \
	$(OBJ_DIR)/ac/charprop.o \
	$(OBJ_DIR)/ac/check.o \
	$(OBJ_DIR)/ac/control.o \
	$(OBJ_DIR)/ac/cswrite.o \
	$(OBJ_DIR)/ac/enum.o \
	$(OBJ_DIR)/ac/eval.o \
	$(OBJ_DIR)/ac/fix.o \
	$(OBJ_DIR)/ac/flat.o \
	$(OBJ_DIR)/ac/fontinfo.o \
	$(OBJ_DIR)/ac/gen.o \
	$(OBJ_DIR)/ac/head.o \
	$(OBJ_DIR)/ac/merge.o \
	$(OBJ_DIR)/ac/misc.o \
	$(OBJ_DIR)/ac/path.o \
	$(OBJ_DIR)/ac/pick.o \
	$(OBJ_DIR)/ac/read.o \
	$(OBJ_DIR)/ac/report.o \
	$(OBJ_DIR)/ac/shuffle.o \
	$(OBJ_DIR)/ac/stub.o \
	$(OBJ_DIR)/ac/write.o \
	$(OBJ_DIR)/bf/charlistpriv.o \
	$(OBJ_DIR)/bf/charpath.o \
	$(OBJ_DIR)/bf/charpathpriv.o \
	$(OBJ_DIR)/bf/cryptprocs.o \
	$(OBJ_DIR)/bf/fileops.o \
	$(OBJ_DIR)/bf/filookup.o \
	$(OBJ_DIR)/bf/illtobez.o \
	$(OBJ_DIR)/bf/machinedep.o \
	$(OBJ_DIR)/bf/rawPStobez.o

default: $(PRG_TARGET)

clean:
	rm -f $(PRG_OBJS)
	rm -f $(LIB_OBJS)
	rm -f $(PRG_TARGET)
	rm -f $(LIB_TARGET)

# Program
$(PRG_TARGET): $(PRG_OBJS) $(PRG_SRCS) $(LIB_TARGET)
	$(CC) $(CFLAGS) -o $@ $(PRG_OBJS) $(PRG_LIBS)

$(OBJ_DIR)/main.o: $(SRC_DIR)/main.c
	$(CC) $(CFLAGS) -DEXECUTABLE=1 -c $< -o $@

# Library
$(LIB_TARGET): $(LIB_OBJS)
	$(AR) -rvs $@ $?

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@
