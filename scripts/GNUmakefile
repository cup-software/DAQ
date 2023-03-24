# This GNUmakefile tells SoftRelTools to consider everything but itself
# and the CVS directory to be a script to install.

skip_files := GNUmakefile CVS
BINS := $(filter-out $(skip_files), $(wildcard *))
SCRIPTS := $(BINS)

# The standard phony list includes depend, which causes the depend script
# to be installed every time.
override SRT_PHONY=all checkdirs include lib bin clean test

include SoftRelTools/standard.mk
