export CPLUS_INCLUDE_PATH=/opt/intel/opencl/include
export LIBRARY_PATH=/opt/intel/opencl

HEADERS=../common/basic.hpp ../common/cmdparser.hpp ../common/oclobject.hpp ../common/yuv_utils.h ../common/utils.h
SOURCES=main.cpp ../common/basic.cpp ../common/cmdparser.cpp ../common/oclobject.cpp ../common/yuv_utils.cpp ../common/utils.cpp

ifeq ($(CONFIG),debug)
	OPT =-O0 -g
else
	OPT =
endif

all: MotionEstimationAdvanced

MotionEstimationAdvanced: $(HEADERS) $(SOURCES) Makefile
	g++ $(SOURCES) -I../common -lOpenCL -oMotionEstimationAdvanced -std=gnu++0x $(OPT)

clean:
	rm -f MotionEstimationAdvanced

