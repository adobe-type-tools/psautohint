#########################################################################
#                                                                       #
# Copyright 1997-2014 Adobe Systems Incorporated.                       #
# All rights reserved.                                                  #
#                                                                       #
#########################################################################

# Configuration
CONFIG = release
ROOT_DIR = .
SRC_DIR = $(ROOT_DIR)/source
OBJ_DIR = $(SRC_DIR)

CFLAGS = $(STD_OPTS) \
	-I$(SRC_DIR)/include/ac \
	-I$(SRC_DIR)/include/extras \
	-I$(SRC_DIR)/ac \
	-I$(SRC_DIR)/bf \
	-DEXECUTABLE=1

# Program
PRG_SRCS = $(SRC_DIR)/main.c
PRG_OBJS = $(OBJ_DIR)/main.o
PRG_LIBS = $(OBJ_DIR)/autohintlib.a
PRG_TARGET = autohintexe

STD_OPTS = -m32  \
	 $(SYS_INCLUDES)

default: $(PRG_TARGET)

$(PRG_TARGET): $(PRG_OBJS) $(PRG_LIBS) $(PRG_SRCS)  $(PRG_INCLUDES) 
	$(CC) $(CFLAGS) -o $@ $(PRG_OBJS) $(PRG_LIBS) -lm

$(PRG_LIBS):
	make -C $(SRC_DIR)

clean:
	make -C $(SRC_DIR) clean
	rm -f $(LIB_OBJS)
	rm -f $(PRG_OBJS)
	rm -f $(PRG_LIBS)
	rm -f $(PRG_TARGET)

