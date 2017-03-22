.PHONY: all

ROOT_DIR = ..
SRC_DIR = $(ROOT_DIR)/source
PYT_DIR = $(ROOT_DIR)/python

ifeq ($(OS),Windows_NT)
EXE = .exe
endif

FONTS:=$(wildcard data/*/*/font.ufo)
TESTS:=$(FONTS:%.ufo=%.tst)

all: $(TESTS)

%.tst: export PATH:=$(SRC_DIR):$(PATH)
%.tst: %.ufo $(SRC_DIR)/autohintexe$(EXE) $(PYT_DIR)/autohint.py
	@echo "	Testing $<"
	@rm -rf $@
	@python2 $(PYT_DIR)/autohint.py -all -qq -o $@ $<
	@diff -Naur $< $@
