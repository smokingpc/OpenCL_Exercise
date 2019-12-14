export CPLUS_INCLUDE_PATH=/opt/intel/opencl/include
export LIBRARY_PATH=/opt/intel/opencl

HEADERS=cmdoptions.hpp ../common/basic.hpp ../common/cmdparser.hpp ../common/oclobject.hpp
SOURCES=cmdoptions.cpp montecarlo.cpp ../common/basic.cpp ../common/cmdparser.cpp ../common/oclobject.cpp

ifeq ($(CONFIG),debug)
	OPT =-O0 -g
else
	OPT =
endif

all: montecarlo

montecarlo: $(HEADERS) $(SOURCES) Makefile
	g++ $(SOURCES) -I../common -lOpenCL -oMonteCarlo -std=gnu++0x $(OPT)

clean:
	rm -f MonteCarlo

