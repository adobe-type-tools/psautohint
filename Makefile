.PHONY: build install check clean format

ROOT_DIR = .
SRC_DIR = $(ROOT_DIR)/source
TST_DIR = $(ROOT_DIR)/tests

PYTHON ?= python

build:
	$(PYTHON) setup.py build

install:
	$(PYTHON) setup.py install --user

autohintexe:
	make -C $(SRC_DIR)

clean:
	$(PYTHON) setup.py clean --all
	make -C $(SRC_DIR) clean

check: install
	make -C $(TST_DIR)

format:
	clang-format -i `find $(SRC_DIR) -name '*.c'`
