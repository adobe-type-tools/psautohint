#########################################################################
#                                                                       #
# Copyright 1997-2014 Adobe Systems Incorporated.                       #
# All rights reserved.                                                  #
#                                                                       #
#########################################################################

.PHONY: build install check clean format

ROOT_DIR = .
SRC_DIR = $(ROOT_DIR)/source
TST_DIR = $(ROOT_DIR)/tests

build:
	@python setup.py build

install:
	@python setup.py install --user

autohintexe:
	make -C $(SRC_DIR)

clean:
	python setup.py clean --all
	make -C $(SRC_DIR) clean

check: install
	make -C $(TST_DIR)

format:
	clang-format -i `find $(SRC_DIR) -name '*.c'`
