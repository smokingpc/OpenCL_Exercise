export CPLUS_INCLUDE_PATH=/opt/intel/opencl/include
export LIBRARY_PATH=/opt/intel/opencl

ifeq ($(CONFIG),debug)
	OPT =-O0 -g
else
	OPT =
endif

all: capsbasic

capsbasic: capsbasic.cpp Makefile
	g++ capsbasic.cpp -lOpenCL -oCapsBasic $(OPT)

clean:
	rm -f CapsBasic
