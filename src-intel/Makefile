CAPSBASIC=\
	CapsBasic/capsbasic.cpp\
	CapsBasic/Makefile

MULTIDEVICEBASIC=\
	MultiDeviceBasic/multidevice.cpp\
	MultiDeviceBasic/multidevice.hpp\
	MultiDeviceBasic/kernel.cpp\
	MultiDeviceBasic/system.cpp\
	MultiDeviceBasic/multi.cpp\
	MultiDeviceBasic/shared.cpp\
	MultiDeviceBasic/Makefile

GEMM=\
	GEMM/cmdoptions.hpp\
	GEMM/cmdoptions.cpp\
	GEMM/gemm.cpp\
	GEMM/gemm.cl\
	GEMM/Makefile

MONTECARLO=\
	MonteCarlo/cmdoptions.hpp\
	MonteCarlo/cmdoptions.cpp\
	MonteCarlo/montecarlo.cpp\
	MonteCarlo/montecarlo.cl\
	MonteCarlo/Makefile

BITONICSORT=\
	BitonicSort/BitonicSort.cpp\
	BitonicSort/BitonicSort.cl\
	BitonicSort/Makefile

GODRAYS=\
	GodRays/GodRays.cpp\
	GodRays/GodRaysNative.cpp\
	GodRays/GodRays.cl\
	GodRays/Makefile

MEDIANFILTER=\
	MedianFilter/MedianFilter.cpp\
	MedianFilter/MedianFilter.cl\
	MedianFilter/Makefile

MOTIONESTIMATION=\
	MotionEstimation/main.cpp\
	MotionEstimation/Makefile

MOTIONESTIMATIONADVANCED=\
	MotionEstimationAdvanced/main.cpp\
	MotionEstimationAdvanced/Makefile

SIMPLEOPTIMIZATIONS=\
	SimpleOptimizations/SimpleOptimizations.cpp\
	SimpleOptimizations/SimpleOptimizations.cl\
	SimpleOptimizations/Makefile

TONEMAPPING=\
	ToneMapping/ToneMapping.cpp\
	ToneMapping/ToneMappingNative.cpp\
	ToneMapping/ToneMapping.cl\
	ToneMapping/stdafx.h\
	ToneMapping/Makefile

TONEMAPPINGMULTIDEVICE=\
	ToneMappingMultiDevice/ToneMappingMultiDevice.cpp\
	ToneMappingMultiDevice/ToneMappingNative.cpp\
	ToneMappingMultiDevice/ToneMappingMultiDevice.cl\
	ToneMappingMultiDevice/stdafx.h\
	ToneMappingMultiDevice/Makefile

COMMON=\
	common/basic.hpp\
	common/basic.cpp\
	common/oclobject.hpp\
	common/oclobject.cpp\
	common/cmdparser.hpp\
	common/cmdparser.cpp\
	common/utils.h\
	common/utils.cpp\
	common/yuv_utils.h\
	common/yuv_utils.cpp

all: samples

samples: capsbasic multidevicebasic gemm montecarlo bitonic_sort godrays median_filter motion_estimation motion_estimation_advanced simple_optimizations tone_mapping tone_mapping_multi_device

capsbasic: $(CAPSBASIC) Makefile
	cd CapsBasic; make

multidevicebasic: $(MULTIDEVICEBASIC) Makefile
	cd MultiDeviceBasic; make

gemm: $(COMMON) $(GEMM) Makefile
	cd GEMM; make

montecarlo: $(COMMON) $(MONTECARLO) Makefile
	cd MonteCarlo; make

bitonic_sort: $(COMMON) $(BITONICSORT) Makefile
	cd BitonicSort; make

godrays: $(COMMON) $(GODRAYS) Makefile
	cd GodRays; make

median_filter: $(COMMON) $(MEDIANFILTER) Makefile
	cd MedianFilter; make

motion_estimation: $(COMMON) $(MOTIONESTIMATION) Makefile
	cd MotionEstimation; make

motion_estimation_advanced: $(COMMON) $(MOTIONESTIMATIONADVANCED) Makefile
	cd MotionEstimationAdvanced; make

simple_optimizations: $(COMMON) $(SIMPLEOPTIMIZATIONS) Makefile
	cd SimpleOptimizations; make

tone_mapping: $(COMMON) $(TONEMAPPING) Makefile
	cd ToneMapping; make

tone_mapping_multi_device: $(COMMON) $(TONEMAPPINGMULTIDEVICE) Makefile
	cd ToneMappingMultiDevice; make

clean:
	cd CapsBasic; make clean
	cd MultiDeviceBasic; make clean
	cd GEMM; make clean
	cd MonteCarlo; make clean
	cd BitonicSort; make clean
	cd GodRays; make clean
	cd MedianFilter; make clean
	cd MotionEstimation; make clean
	cd MotionEstimationAdvanced; make clean
	cd SimpleOptimizations; make clean
	cd ToneMapping; make clean
	cd ToneMappingMultiDevice; make clean