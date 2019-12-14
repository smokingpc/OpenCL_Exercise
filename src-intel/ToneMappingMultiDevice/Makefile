export CPLUS_INCLUDE_PATH=/opt/intel/opencl/include
export LIBRARY_PATH=/opt/intel/opencl

HEADERS=../common/basic.hpp ../common/cmdparser.hpp ../common/oclobject.hpp ../common/utils.h stdafx.h
SOURCES=ToneMappingMultiDevice.cpp ToneMappingNative.cpp ../common/basic.cpp ../common/cmdparser.cpp ../common/oclobject.cpp ../common/utils.cpp

ifeq ($(CONFIG),debug)
	OPT =-O0 -g
else
	OPT =
endif

all: tone_mapping_multi_device

tone_mapping_multi_device: $(HEADERS) $(SOURCES) Makefile
	g++ $(SOURCES) -I../common -lOpenCL -oToneMappingMultiDevice -std=gnu++0x $(OPT)

clean:
	rm -f ToneMappingMultiDevice

