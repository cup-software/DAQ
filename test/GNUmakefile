#############################################################
# example Makefile for linking 
# uses SoftRelTools/standard.mk
#############################################################

BINS = tcbdaq tcb daq merger nulltcb nulldaq#stddaq usbreset

SIMPLEBINS = $(BINS) 

#framework general 

override LOADLIBES += -lOnlObjs
override LOADLIBES += -lRawObjs
override LOADLIBES += -lHDF5Utils
override LOADLIBES += -lNotice
override LOADLIBES += -lDAQConfig
override LOADLIBES += -lDAQSystem
override LOADLIBES += -lDAQUtils
override LOADLIBES += -lDAQTrigger
override LOADLIBES += -lOnlHistogramer
override LOADLIBES += -lDAQ

############################################################
include SoftRelTools/standard.mk
include SoftRelTools/arch_spec_root.mk
include SoftRelTools/arch_spec_hdf5.mk
include SoftRelTools/arch_spec_libusb1.mk
