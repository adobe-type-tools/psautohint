.PHONY: all FORCE

PYTHON ?= python
COVERAGE = $(PYTHON) -m coverage run

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
	@$(COVERAGE) -m psautohint --all -o $@ $<
	@rm -f $@/.DS_Store
	@$(PYTHON) differ.py $< $@
	@rm -rf $@

%.pst: %.otf FORCE
	@echo "	Testing $<"
	@rm -rf $@
	@$(COVERAGE) -m psautohint -o $@ $<
	@ttx -q -t CFF -o $<.ttx $<
	@ttx -q -t CFF -o $@.ttx $@
	@$(PYTHON) differ.py $<.ttx $@.ttx
	@rm -rf $@ $<.ttx $@.ttx

FORCE:
