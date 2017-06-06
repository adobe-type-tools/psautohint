.PHONY: all FORCE

ROOT_DIR = ..
SRC_DIR = $(ROOT_DIR)/source
PYT_DIR = $(ROOT_DIR)/python

PYTHON ?= python

FONTS:=$(wildcard data/*/*/font.ufo)
TESTS:=$(FONTS:%.ufo=%.tst)

all: $(TESTS)

# diff options
# http://www.gnu.org/software/diffutils/manual/html_node/diff-Options.html
# -N If one file is missing, treat it as present but empty
# -a Treat all files as text and compare them line-by-line
# -u Use the unified output format, showing three lines of context
# -r When comparing directories, recursively compare any subdirectories found
# -x When comparing directories, ignore files and subdirectories whose basenames
#    match the pattern

%.tst: %.ufo FORCE
	@echo "	Testing $<"
	@rm -rf $@
	@$(PYTHON) -m psautohint.autohint -all -qq -o $@ $<
	@diff -Naur -x '.DS_Store' $< $@
	@rm -f $@/.DS_Store
	@rm -rf $@

FORCE:
