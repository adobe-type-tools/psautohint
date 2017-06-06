.PHONY: build install check dist clean format

ROOT_DIR = .
SRC_DIR = $(ROOT_DIR)/libpsautohint
TST_DIR = $(ROOT_DIR)/tests

PYTHON ?= python
# --user does not work from inside a virtual environment
# PIP_OPTIONS ?= --user
PIP_OPTIONS ?=

# Get the platform/version-specific build/lib.* folder
define GET_BUILD_DIR
import os
import sys
import sysconfig
root = sys.argv[1]
plat_lib_name = "lib.{platform}-{version[0]}.{version[1]}".format(
    platform=sysconfig.get_platform(), version=sys.version_info)
build_dir = os.path.abspath(os.path.join(root, "build", plat_lib_name))
print(build_dir)
endef

BUILD_DIR := $(shell $(PYTHON) -c '$(GET_BUILD_DIR)' $(ROOT_DIR))

build:
	$(PYTHON) setup.py build

install:
	$(PYTHON) -m pip install -r requirements.txt -v $(PIP_OPTIONS) .

autohintexe:
	make -C $(SRC_DIR)

dist: clean
	$(PYTHON) setup.py sdist bdist_wheel

clean:
	$(PYTHON) setup.py clean --all
	make -C $(SRC_DIR) clean

check: build
	make -C $(TST_DIR) PYTHONPATH="$(BUILD_DIR)"

format:
	clang-format -i `find $(SRC_DIR) -name '*.c'`
