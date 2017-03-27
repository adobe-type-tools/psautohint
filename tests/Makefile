.PHONY: all

ROOT_DIR = ..
SRC_DIR = $(ROOT_DIR)/source
PYT_DIR = $(ROOT_DIR)/python

PYTHON ?= python

FONTS:=$(wildcard data/*/*/font.ufo)
TESTS:=$(FONTS:%.ufo=%.tst)

all: $(TESTS)

%.tst: %.ufo
	@echo "	Testing $<"
	@rm -rf $@
	@$(PYTHON) $(PYT_DIR)/autohint.py -all -qq -o $@ $<
	@diff -Naur $< $@
