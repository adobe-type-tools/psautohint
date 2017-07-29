.PHONY: all FORCE

PYTHON ?= python

UFO:=$(wildcard data/*/*/font.ufo)
OTF:=$(wildcard data/*/*/font.otf)
TESTS:=$(UFO:%.ufo=%.tst) $(OTF:%.otf=%.pst)

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
	@$(PYTHON) -m psautohint -all -qq -o $@ $<
	@rm -f $@/.DS_Store
	@diff -Naur --strip-trailing-cr $< $@
	@rm -rf $@

%.pst: %.otf FORCE
	@echo "	Testing $<"
	@rm -rf $@
	@$(PYTHON) -m psautohint -qq -o $@ $<
	@ttx -q -t CFF -o $<.ttx $<
	@ttx -q -t CFF -o $@.ttx $@
	@diff -u $<.ttx $@.ttx
	@rm -rf $@ $<.ttx $@.ttx

FORCE:
