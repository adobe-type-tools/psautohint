#########################################################################
#                                                                       #
# Copyright 1997-2014 Adobe Systems Incorporated.                       #
# All rights reserved.                                                  #
#                                                                       #
#########################################################################

ROOT_DIR = .
SRC_DIR = $(ROOT_DIR)/source

default:
	make -C $(SRC_DIR)

clean:
	make -C $(SRC_DIR) clean

