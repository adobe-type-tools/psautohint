#########################################################################
#                                                                       #
# Copyright 1997-2014 Adobe Systems Incorporated.                       #
# All rights reserved.                                                  #
#                                                                       #
#########################################################################

# Configuration
CONFIG = release
ROOT_DIR = .
OBJECT_DIR = .
SRC_DIR = $(ROOT_DIR)/source
LIB_DIR = $(ROOT_DIR)/autohintlib

CFLAGS = $(STD_OPTS) \
	-I$(LIB_DIR)/source/public/ac \
	-I$(LIB_DIR)/source/public/extras \
	-I$(LIB_DIR)/source/ac \
	-I$(LIB_DIR)/source/bf \
	-DEXECUTABLE=1

# Program
PRG_SRCS = $(SRC_DIR)/main.c
PRG_OBJS = main.o
PRG_LIBS = $(LIB_DIR)/autohintlib.a
PRG_TARGET = autohintexe

STD_OPTS = -m32  \
	-I$(ROOT_DIR)/public/lib/api \
	-I$(ROOT_DIR)/public/lib/resource \
	 $(SYS_INCLUDES)

default: $(PRG_TARGET)

main.o:
	$(CC) $(CFLAGS) -c $(SRC_DIR)/main.c -o $@

$(PRG_TARGET): $(PRG_OBJS) $(PRG_LIBS) $(PRG_SRCS)  $(PRG_INCLUDES) 
	$(CC) $(CFLAGS) -o $@ $(PRG_OBJS) $(PRG_LIBS) -lm

$(PRG_LIBS):
	make -C $(LIB_DIR)

clean:
	make -C $(LIB_DIR) clean
	rm -f $(LIB_OBJS)
	rm -f $(PRG_OBJS)
	rm -f $(PRG_LIBS)
	rm -f $(PRG_TARGET)

