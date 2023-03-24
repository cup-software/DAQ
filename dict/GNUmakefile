ifdef LINK_SHARED
  LINK_SHARED_MODULES = yes
  SHAREDLIB = libDAQ.so
else
  sharedlib_o_dir = $(staticlib_o_dir)
  LIB       = libDAQ.a
endif

LIBCXXFILES = $(filter-out $(skip_files), $(wildcard $(sharedlib_o_dir)*_dict.cxx))

include SoftRelTools/standard.mk
include SoftRelTools/refresh.mk
include SoftRelTools/arch_spec_root.mk
include SoftRelTools/arch_spec_rootcint.mk
include SoftRelTools/arch_spec_hdf5.mk
