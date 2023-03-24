# SoftRelTools GNUmakefile for MySim source directory 

# Build a library an executable.
LIB = libDAQ.a

# Compile all the .cc files in this directory 

LIBCCFILES = $(filter-out $(skip_files), $(wildcard *.cc))

################################
include SoftRelTools/standard.mk
include SoftRelTools/refresh.mk
include SoftRelTools/arch_spec_root.mk
include SoftRelTools/arch_spec_hdf5.mk
