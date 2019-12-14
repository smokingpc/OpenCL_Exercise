export CPLUS_INCLUDE_PATH=/opt/intel/opencl/include
export LIBRARY_PATH=/opt/intel/opencl

HEADERS=../common/basic.hpp ../common/cmdparser.hpp ../common/oclobject.hpp multidevice.hpp
SOURCES=kernel.cpp multi.cpp shared.cpp system.cpp multidevice.cpp ../common/basic.cpp ../common/cmdparser.cpp ../common/oclobject.cpp

ifeq ($(CONFIG),debug)
	OPT =-O0 -g
else
	OPT =
endif

all: multidevice

multidevice: $(HEADERS) $(SOURCES) Makefile
	g++ $(SOURCES) -I../common -lOpenCL -oMultiDeviceBasic -std=gnu++0x $(OPT)

clean:
	rm -f MultiDeviceBasic

