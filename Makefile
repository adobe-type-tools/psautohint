.PHONY: build install check clean format

ROOT_DIR = .
SRC_DIR = $(ROOT_DIR)/source
TST_DIR = $(ROOT_DIR)/tests

PYTHON ?= python
# --user does not work from inside a virtual environment
# PIP_OPTIONS ?= --user
PIP_OPTIONS ?=

build:
	$(PYTHON) setup.py build

install:
	$(PYTHON) -m pip install -r requirements.txt -v $(PIP_OPTIONS) .

autohintexe:
	make -C $(SRC_DIR)

clean:
	$(PYTHON) setup.py clean --all
	make -C $(SRC_DIR) clean

check: install
	make -C $(TST_DIR)

format:
	clang-format -i `find $(SRC_DIR) -name '*.c'`
