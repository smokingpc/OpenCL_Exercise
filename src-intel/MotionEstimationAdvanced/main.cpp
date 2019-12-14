// Copyright (c) 2009-2014 Intel Corporation
// All rights reserved.
//
// WARRANTY DISCLAIMER
//
// THESE MATERIALS ARE PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR ITS
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THESE
// MATERIALS, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Intel Corporation is the author of the Materials, and requests that all
// problem reports or change requests be submitted to it directly

#define __CL_ENABLE_EXCEPTIONS

#include <iostream>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>

#include <CL/cl.hpp>
#include <CL/cl_ext.h>

#include "yuv_utils.h"
#include "cmdparser.hpp"
#include "oclobject.hpp"

#define CL_EXT_DECLARE(name) static name##_fn pfn_##name = 0;

#define CL_EXT_INIT_WITH_PLATFORM(platform, name) { \
    pfn_##name = (name##_fn) clGetExtensionFunctionAddressForPlatform(platform, #name); \
    if (! pfn_##name ) \
        { \
        std::cout<<"ERROR: can't get handle to function pointer " <<#name<< ", wrong driver version?\n"; \
        } \
};


CL_EXT_DECLARE( clCreateAcceleratorINTEL );
CL_EXT_DECLARE( clReleaseAcceleratorINTEL );

using namespace YUVUtils;

// these values define dimensions of input pixel blocks (which are fixed in hardware)
// so, do not change these values to avoid errors
#define SRC_BLOCK_WIDTH 16
#define SRC_BLOCK_HEIGHT 16

typedef cl_short2 MotionVector;

#ifndef CL_ME_COST_PENALTY_NONE_INTEL
#define CL_ME_COST_PENALTY_NONE_INTEL                   0x0
#define CL_ME_COST_PENALTY_LOW_INTEL                    0x1
#define CL_ME_COST_PENALTY_NORMAL_INTEL                 0x2
#define CL_ME_COST_PENALTY_HIGH_INTEL                   0x3
#endif

#ifndef CL_ME_COST_PRECISION_QPEL_INTEL
#define CL_ME_COST_PRECISION_QPEL_INTEL                 0x0
#define CL_ME_COST_PRECISION_HPEL_INTEL                 0x1
#define CL_ME_COST_PRECISION_PEL_INTEL                  0x2
#define CL_ME_COST_PRECISION_DPEL_INTEL                 0x3
#endif

#ifndef CL_DEVICE_ME_VERSION_INTEL
#define CL_DEVICE_ME_VERSION_INTEL                      0x407E
#define CL_ME_VERSION_ADVANCED_VER_1_INTEL              0x1
#endif

#ifndef CL_ME_CHROMA_INTRA_PREDICT_ENABLED_INTEL
#define CL_ME_CHROMA_INTRA_PREDICT_ENABLED_INTEL        0x1
#define CL_ME_LUMA_INTRA_PREDICT_ENABLED_INTEL          0x2
#endif


// Specifies number of motion vectors per source pixel block (the value of CL_ME_MB_TYPE_16x16_INTEL specifies  just a single vector per block )
static cl_uint kMBBlockType = CL_ME_MB_TYPE_8x8_INTEL;
static cl_uint kCostPenalty = CL_ME_COST_PENALTY_HIGH_INTEL;
static cl_uint kCostPrecision = CL_ME_COST_PRECISION_HPEL_INTEL;
static cl_uint kPredictors = 8;

static const cl_short kPredictorX0 = 96;
static const cl_short kPredictorY0 = 80;
static const cl_short kPredictorX1 = 0;
static const cl_short kPredictorY1 = 0;

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4355)    // 'this': used in base member initializer list
#endif

// All command-line options for the sample
class CmdParserMV : public CmdParser
{
public:
    CmdOption<std::string>  fileName;
    CmdOption<std::string>  overlayFileName;
    CmdOption<int>  width;
    CmdOption<int>  height;
    CmdOption<bool> help;
    CmdOption<bool> noOutputToBMP;
    CmdOption<std::string>  blockType;
    CmdEnum<std::string>    blockType4x4;
    CmdEnum<std::string>    blockType8x8;
    CmdEnum<std::string>    blockType16x16;
    CmdOption<std::string>  costPenalty;
    CmdEnum<std::string>    costPenaltyNone;
    CmdEnum<std::string>    costPenaltyLow;
    CmdEnum<std::string>    costPenaltyNormal;
    CmdEnum<std::string>    costPenaltyHigh;
    CmdOption<std::string>  costPrecision;
    CmdEnum<std::string>    costPrecisionQuarter;
    CmdEnum<std::string>    costPrecisionHalf;
    CmdEnum<std::string>    costPrecisionFull;
    CmdEnum<std::string>    costPrecisionDouble;
    CmdOption<int>  numberPredictors;


    CmdParserMV  (int argc, const char** argv) :
        CmdParser(argc, argv),
        noOutputToBMP(*this,    0,"nobmp","","Do not output frames to the sequence of bmp files (in addition to the yuv file), by default the output is on."),
        help(*this,            'h',"help","","Show this help text and exit."),
        fileName(*this,         0,"input", "string", "Input video sequence filename (.yuv file format).","mea_video_1920x1080_5frames.yuv"),
        overlayFileName(*this,  0,"output","string", "Output video sequence with overlaid motion vectors filename. ","output.yuv"),
        width(*this,            0, "width", "<integer>", "Frame width for the input file.", 1920),
        height(*this,           0, "height","<integer>", "Frame height for the input file.", 1080),
        blockType(*this,        0,"block", "", "Macro block type.","8x8"),
        blockType4x4(blockType, "4x4"),
        blockType8x8(blockType, "8x8"),
        blockType16x16(blockType, "16x16"),
        costPenalty(*this,      0,"penalty", "", "Cost penalty.","high"),
        costPenaltyNone(costPenalty, "none"),
        costPenaltyLow(costPenalty, "low"),
        costPenaltyNormal(costPenalty, "normal"),
        costPenaltyHigh(costPenalty, "high"),
        costPrecision(*this,    0,"precision", "", "Cost precision.","half"),
        costPrecisionQuarter(costPrecision, "quarter"),
        costPrecisionHalf(costPrecision, "half"),
        costPrecisionFull(costPrecision, "full"),
        costPrecisionDouble(costPrecision, "double"),
        numberPredictors(*this, 0,"predictors", "0..8", "Number of predictor motion vectors.",8)
    {
    }
    virtual void parse ()
    {
        CmdParser::parse();
        if(help.isSet())
        {
            printUsage(std::cout);
        }
        if(numberPredictors.getValue() > 8 || numberPredictors.getValue() < 0)
        {
            throw CmdParser::Error("Invalid number of predictor motion vectors. Should be 0..8.");
        }
    }
};
#ifdef _MSC_VER
#pragma warning (pop)
#endif

inline void ComputeNumMVs( cl_uint nMBType,
                          int nPicWidth, int nPicHeight,
                          int & nMVSurfWidth, int & nMVSurfHeight,
                          int & nMBSurfWidth, int & nMBSurfHeight )
{
    // Size of the input frame in pixel blocks (SRC_BLOCK_WIDTH x SRC_BLOCK_HEIGHT each)
    int nPicWidthInBlk  = (nPicWidth + SRC_BLOCK_WIDTH - 1) / SRC_BLOCK_WIDTH;
    int nPicHeightInBlk = (nPicHeight + SRC_BLOCK_HEIGHT - 1) / SRC_BLOCK_HEIGHT;

    if (CL_ME_MB_TYPE_4x4_INTEL == nMBType) {         // Each Src block has 4x4 MVs
        nMVSurfWidth = nPicWidthInBlk * 4;
        nMVSurfHeight = nPicHeightInBlk * 4;
    }
    else if (CL_ME_MB_TYPE_8x8_INTEL == nMBType) {    // Each Src block has 2x2 MVs
        nMVSurfWidth = nPicWidthInBlk * 2;
        nMVSurfHeight = nPicHeightInBlk * 2;
    }
    else if (CL_ME_MB_TYPE_16x16_INTEL == nMBType) {  // Each Src block has 1 MV
        nMVSurfWidth = nPicWidthInBlk;
        nMVSurfHeight = nPicHeightInBlk;
    }
    else
    {
        throw std::runtime_error("Unknown macroblock type");
    }

    nMBSurfWidth = nPicWidthInBlk;
    nMBSurfHeight = nPicHeightInBlk;
}

inline unsigned int ComputeSubBlockSize( cl_uint nMBType )
{
    switch (nMBType)
    {
    case CL_ME_MB_TYPE_4x4_INTEL: return 4;
    case CL_ME_MB_TYPE_8x8_INTEL: return 8;
    case CL_ME_MB_TYPE_16x16_INTEL: return 16;
    default:
        throw std::runtime_error("Unknown macroblock type");
    }
}

void ExtractMotionVectorsFullFrameWithOpenCL(
    Capture * pCapture,
    std::vector<MotionVector> &MVs,
    std::vector<cl_ushort> &SADs,
    const CmdParserMV& cmd)
{

    // OpenCL initialization
    OpenCLBasic init("Intel", "GPU");
    //OpenCLBasic creates the platform/context and device for us, so all we need is to get an ownership (via incrementing ref counters with clRetainXXX)

    cl::Context context = cl::Context(init.context); clRetainContext(init.context);
    cl::Device device  = cl::Device(init.device);   clRetainDevice(init.device);
    cl::CommandQueue queue = cl::CommandQueue(init.queue);clRetainCommandQueue(init.queue);

    std::string ext = device.getInfo< CL_DEVICE_EXTENSIONS >();
    if (string::npos == ext.find("cl_intel_accelerator") || 
        string::npos == ext.find("cl_intel_advanced_motion_estimation"))
    {
        throw Error("Error, the selected device doesn't support advanced motion estimation or accelerator extensions!");
    }

    // Create a built-in VME kernel
    cl_int err = 0;
    const cl_device_id & d = device();
    cl_uint vmeVersion = 0;
    cl_uint error = clGetDeviceInfo(d, CL_DEVICE_ME_VERSION_INTEL, sizeof(vmeVersion), &vmeVersion, 0);
    if (error != CL_SUCCESS || vmeVersion < CL_ME_VERSION_ADVANCED_VER_1_INTEL)
    {
        throw Error("Error, the selected device doesn't support advanced motion estimation version 1!");
    }

    cl::Program p (clCreateProgramWithBuiltInKernels( context(), 1, &d, "block_advanced_motion_estimate_check_intel", &err ));
    if (err != CL_SUCCESS)
    {
        throw cl::Error(err, "Failed creating builtin kernel(s)");
    }

    CL_EXT_INIT_WITH_PLATFORM( init.platform, clCreateAcceleratorINTEL );
    CL_EXT_INIT_WITH_PLATFORM( init.platform, clReleaseAcceleratorINTEL );


    cl::Kernel kernel(p, "block_advanced_motion_estimate_check_intel");

    // VME API configuration knobs
    cl_motion_estimation_desc_intel desc = {
        kMBBlockType,                                    // Number of motion vectors per source pixel block (the value of CL_ME_MB_TYPE_16x16_INTEL specifies  just a single vector per block )
        CL_ME_SUBPIXEL_MODE_QPEL_INTEL,                  // Motion vector precision
        CL_ME_SAD_ADJUST_MODE_NONE_INTEL,                // SAD Adjust (none/Haar transform) for the residuals
        CL_ME_SEARCH_PATH_RADIUS_16_12_INTEL             // Search window radius 
    };

    // Create an accelerator object (abstraction of the motion estimation acceleration engine)
    cl_accelerator_intel accelerator = pfn_clCreateAcceleratorINTEL(context(), CL_ACCELERATOR_TYPE_MOTION_ESTIMATION_INTEL,
        sizeof(cl_motion_estimation_desc_intel), &desc, &err);
    if (err != CL_SUCCESS)
    {
        throw cl::Error(err, "Error creating motion estimation accelerator object.");
    }

    int numPics = pCapture->GetNumFrames();
    int width = cmd.width.getValue();
    int height = cmd.height.getValue();
    int mvImageWidth, mvImageHeight;
    int mbImageWidth, mbImageHeight;
    ComputeNumMVs(desc.mb_block_type, width, height, mvImageWidth, mvImageHeight, mbImageWidth, mbImageHeight);

    MVs.resize(numPics * mvImageWidth * mvImageHeight);
    SADs.resize(numPics * mvImageWidth * mvImageHeight);

    // Set up OpenCL surfaces
    cl::ImageFormat imageFormat(CL_R, CL_UNORM_INT8);
    cl::Image2D refImage(context, CL_MEM_READ_ONLY, imageFormat, width, height, 0, 0);
    cl::Image2D srcImage(context, CL_MEM_READ_ONLY, imageFormat, width, height, 0, 0);

    cl_short2 *countMem = new cl_short2[ mbImageWidth * mbImageHeight ];
    for( int i = 0; i < mbImageWidth * mbImageHeight; i++ )
    {
        countMem[ i ].s[ 0 ] = kPredictors; //[0,8] number of predictor motion vectors
        countMem[ i ].s[ 1 ] = 0; //for skip motion vectors
    }

    //Initialize predictors
    cl_short2 *predMem = new cl_short2[ mbImageWidth * mbImageHeight * 8 ];
    for( int i = 0; i < mbImageWidth * mbImageHeight; i++ )
    {
        for( int j = 0; j < 1; j++ )
        {
            predMem[ i * 8 + j ].s[ 0 ] = 0;
            predMem[ i * 8 + j ].s[ 1 ] = 0;
        }
        for( int j = 1; j < 2; j++ )
        {
            predMem[ i * 8 + j ].s[ 0 ] = kPredictorX0;
            predMem[ i * 8 + j ].s[ 1 ] = kPredictorY0;
        }
        for( int j = 2; j < 3; j++ )
        {
            predMem[ i * 8 + j ].s[ 0 ] = -kPredictorX0;
            predMem[ i * 8 + j ].s[ 1 ] = kPredictorY0;
        }
        for( int j = 3; j < 4; j++ )
        {
            predMem[ i * 8 + j ].s[ 0 ] = kPredictorX0;
            predMem[ i * 8 + j ].s[ 1 ] = -kPredictorY0;
        }	
        for( int j = 4; j < 5; j++ )
        {
            predMem[ i * 8 + j ].s[ 0 ] = -kPredictorX0;
            predMem[ i * 8 + j ].s[ 1 ] = -kPredictorY0;
        }
        for( int j = 5; j < 8; j++ )
        {
            predMem[ i * 8 + j ].s[ 0 ] = kPredictorX1;
            predMem[ i * 8 + j ].s[ 1 ] = kPredictorY1;
        }
    }

    cl::Buffer countBuffer(
        context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 
        mbImageWidth * mbImageHeight * sizeof(cl_short2), countMem, NULL);
    cl::Buffer predBuffer(
        context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 
        mbImageWidth * mbImageHeight * 8 * sizeof(cl_short2), predMem, NULL);

    cl::Buffer mvBuffer(
        context, CL_MEM_WRITE_ONLY,
        mvImageWidth * mvImageHeight * sizeof(MotionVector));
    cl::Buffer residualBuffer(
        context, CL_MEM_WRITE_ONLY,
        mvImageWidth * mvImageHeight * sizeof(cl_ushort));

    // Bootstrap video sequence reading
    PlanarImage * currImage = CreatePlanarImage(width, height);

    pCapture->GetSample(0, currImage);

    cl::size_t<3> origin;
    origin[0] = 0;
    origin[1] = 0;
    origin[2] = 0;
    cl::size_t<3> region;
    region[0] = width;
    region[1] = height;
    region[2] = 1;
    // Copy to tiled image memory - this copy (and its overhead) is not necessary in a full GPU pipeline
    queue.enqueueWriteImage(srcImage, CL_TRUE, origin, region, currImage->PitchY, 0, currImage->Y);

    // Process all frames
    double ioStat = 0;//file i/o
    double meStat = 0;//motion estimation itself

    unsigned flags = 0; //no optional modes or behaviors used in computing motion estimation
    unsigned skipBlockType = 0;
    unsigned costPenalty = kCostPenalty;
    unsigned costPrecision = kCostPrecision;

    double overallStart  = time_stamp();
    // First frame is already in srcImg, so we start with the second frame
    for (int i = 1; i < numPics; i++)
    {
        // Phase (1)

        double ioStart = time_stamp();

        // Load next picture
        pCapture->GetSample(i, currImage);

        std::swap(refImage, srcImage);
        // Copy to tiled image memory - this copy (and its overhead) is not necessary in a full GPU pipeline
        queue.enqueueWriteImage(srcImage, CL_TRUE, origin, region, currImage->PitchY, 0, currImage->Y);
        ioStat += (time_stamp() -ioStart);

        double meStart = time_stamp();
        // Schedule full-frame motion estimation
        kernel.setArg(0, accelerator);
        kernel.setArg(1, srcImage);
        kernel.setArg(2, refImage);
        kernel.setArg(3, sizeof(unsigned), &flags);         // optional modes or behaviors flags
        kernel.setArg(4, sizeof(unsigned), &skipBlockType); // skip block type
        kernel.setArg(5, sizeof(unsigned), &costPenalty);   // cost penalty
        kernel.setArg(6, sizeof(unsigned), &costPrecision); // cost precision
        kernel.setArg(7, countBuffer);
        kernel.setArg(8, predBuffer);

        kernel.setArg(9, sizeof(cl_mem), NULL);             // no skip checks
        kernel.setArg(10, mvBuffer);                        // search mvs
        kernel.setArg(11, sizeof(cl_mem), NULL);            // no intra
        kernel.setArg(12, residualBuffer );                 // search residuals
        kernel.setArg(13, sizeof(cl_mem), NULL);            // no skip residuals
        kernel.setArg(14, sizeof(cl_mem), NULL);            // no intra residuals

        queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(width, height), cl::NullRange);
        queue.finish();
        meStat += (time_stamp() - meStart);

        ioStart = time_stamp();
        // Read back resulting motion vectors (in a sync way)
        void * pMVs = &MVs[i * mvImageWidth * mvImageHeight];
        queue.enqueueReadBuffer(mvBuffer,CL_TRUE,0,sizeof(MotionVector) * mvImageWidth * mvImageHeight,pMVs,0,0);

        void * pSADs = &SADs[i * mvImageWidth * mvImageHeight];
        queue.enqueueReadBuffer(residualBuffer,CL_TRUE,0,sizeof(cl_ushort) * mvImageWidth * mvImageHeight,pSADs,0,0);

        ioStat += (time_stamp() -ioStart);
    }
    double overallStat  = time_stamp() - overallStart;
    std::cout << std::setiosflags(std::ios_base::fixed) << std::setprecision(3);
    std::cout << "Overall time for " << numPics << " frames " << overallStat << " sec\n" ;
    std::cout << "Average frame file I/O time per frame " << 1000*ioStat/numPics << " ms\n";
    std::cout << "Average Motion Estimation time per frame is " << 1000*meStat/numPics << " ms\n";

    pfn_clReleaseAcceleratorINTEL(accelerator);
    ReleaseImage(currImage);
}

//Skip test routine for 8x8 and 16x16 sub-blocks with cost penalty switched off (CL_ME_COST_PENALTY_NONE_INTEL).
void ComputeCheckMotionVectorsFullFrameWithOpenCL(
    Capture * pCapture, std::vector<MotionVector> & searchMVs,
    std::vector<MotionVector> &skipMVs,
    std::vector<cl_ushort> &searchSADs,
    std::vector<cl_ushort> &skipSADs,
    const CmdParserMV& cmd)
{

    // OpenCL initialization
    OpenCLBasic init("Intel", "GPU");
    //OpenCLBasic creates the platform/context and device for us, so all we need is to get an ownership (via incrementing ref counters with clRetainXXX)

    cl::Context context = cl::Context(init.context); clRetainContext(init.context);
    cl::Device device  = cl::Device(init.device);   clRetainDevice(init.device);
    cl::CommandQueue queue = cl::CommandQueue(init.queue);clRetainCommandQueue(init.queue);

    std::string ext = device.getInfo< CL_DEVICE_EXTENSIONS >();
    if (string::npos == ext.find("cl_intel_accelerator") ||
        string::npos == ext.find("cl_intel_advanced_motion_estimation"))
    {
        throw Error("Error, the selected device doesn't support motion estimation or accelerator extensions!");
    }

    // Create a built-in VME kernel
    cl_int err = 0;
    const cl_device_id & d = device();
    cl::Program p (
        clCreateProgramWithBuiltInKernels(
        context(), 1, &d, "block_advanced_motion_estimate_check_intel", &err ));
    if (err != CL_SUCCESS)
    {
        throw cl::Error(err, "Failed creating builtin kernel(s)");
    }

    CL_EXT_INIT_WITH_PLATFORM( init.platform, clCreateAcceleratorINTEL );
    CL_EXT_INIT_WITH_PLATFORM( init.platform, clReleaseAcceleratorINTEL );

    if (err != CL_SUCCESS)
    {
        throw cl::Error(err, "Failed building vme program(s)");
    }

    cl::Kernel kernel(p, "block_advanced_motion_estimate_check_intel");

    // VME API configuration knobs
    cl_motion_estimation_desc_intel desc =
    {
        kMBBlockType,                                  // Number of motion vectors per source pixel block (the value of CL_ME_MB_TYPE_16x16_INTEL specifies  just a single vector per block )
        CL_ME_SUBPIXEL_MODE_QPEL_INTEL,                // Motion vector precision
        CL_ME_SAD_ADJUST_MODE_NONE_INTEL,              // SAD Adjust (none/Haar transform) for the residuals
        CL_ME_SEARCH_PATH_RADIUS_16_12_INTEL           // Search window radius 
    };

    // Create an accelerator object (abstraction of the motion estimation acceleration engine)
    cl_accelerator_intel accelerator = pfn_clCreateAcceleratorINTEL(context(), CL_ACCELERATOR_TYPE_MOTION_ESTIMATION_INTEL,
        sizeof(cl_motion_estimation_desc_intel), &desc, &err);
    if (err != CL_SUCCESS)
    {
        throw cl::Error(err, "Error creating motion estimation accelerator object.");
    }

    int numPics = pCapture->GetNumFrames();
    int width = cmd.width.getValue();
    int height = cmd.height.getValue();
    int mvImageWidth, mvImageHeight;
    int mbImageWidth, mbImageHeight;
    ComputeNumMVs(desc.mb_block_type,
        width, height,
        mvImageWidth, mvImageHeight,
        mbImageWidth, mbImageHeight);

    searchMVs.resize(numPics * mvImageWidth * mvImageHeight);
    skipSADs.resize(numPics * mvImageWidth * mvImageHeight * 8);
    searchSADs.resize(numPics * mvImageWidth * mvImageHeight);

    // Set up OpenCL surfaces
    cl::ImageFormat imageFormat(CL_R, CL_UNORM_INT8);
    cl::Image2D refImage(context, CL_MEM_READ_ONLY, imageFormat, width, height, 0, 0);
    cl::Image2D srcImage(context, CL_MEM_READ_ONLY, imageFormat, width, height, 0, 0);

    cl_short2 *countSkipMem = new cl_short2[ mbImageWidth * mbImageHeight ];
    for( int i = 0; i < mbImageWidth * mbImageHeight; i++ )
    {
        countSkipMem[ i ].s[ 0 ] = kPredictors; //[0,8] number of predictor motion vectors
        countSkipMem[ i ].s[ 1 ] = kPredictors; //for skip motion vectors
    }

    cl::Buffer countSkipBuffer(
        context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        mbImageWidth * mbImageHeight * sizeof(cl_short2), countSkipMem, NULL);

    //Initialize predictors
    cl_short2 *predMem = new cl_short2[ mbImageWidth * mbImageHeight * 8 ];
    for( int i = 0; i < mbImageWidth * mbImageHeight; i++ )
    {
        for( int j = 0; j < 1; j++ )
        {
            predMem[ i * 8 + j ].s[ 0 ] = 0;
            predMem[ i * 8 + j ].s[ 1 ] = 0;
        }
        for( int j = 1; j < 2; j++ )
        {
            predMem[ i * 8 + j ].s[ 0 ] = kPredictorX0;
            predMem[ i * 8 + j ].s[ 1 ] = kPredictorY0;
        }
        for( int j = 2; j < 3; j++ )
        {
            predMem[ i * 8 + j ].s[ 0 ] = -kPredictorX0;
            predMem[ i * 8 + j ].s[ 1 ] = kPredictorY0;
        }
        for( int j = 3; j < 4; j++ )
        {
            predMem[ i * 8 + j ].s[ 0 ] = kPredictorX0;
            predMem[ i * 8 + j ].s[ 1 ] = -kPredictorY0;
        }
        for( int j = 4; j < 5; j++ )
        {
            predMem[ i * 8 + j ].s[ 0 ] = -kPredictorX0;
            predMem[ i * 8 + j ].s[ 1 ] = -kPredictorY0;
        }
        for( int j = 5; j < 8; j++ )
        {
            predMem[ i * 8 + j ].s[ 0 ] = kPredictorX1;
            predMem[ i * 8 + j ].s[ 1 ] = kPredictorY1;
        }
    }

    cl::Buffer predBuffer(
        context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 
        mbImageWidth * mbImageHeight * 8 * sizeof(cl_short2), predMem, NULL);
    cl::Buffer searchMVBuffer(
        context, CL_MEM_WRITE_ONLY, 
        mvImageWidth * mvImageHeight * sizeof(MotionVector));
    cl::Buffer searchResidualBuffer(
        context, CL_MEM_WRITE_ONLY,
        mvImageWidth * mvImageHeight * sizeof(cl_ushort));

    cl::Buffer skipResidualBuffer(
        context, CL_MEM_WRITE_ONLY,
        mvImageWidth * mvImageHeight * 8 * sizeof(cl_ushort));

    cl_short2 *skipMVMem = new cl_short2[ mvImageWidth * mvImageHeight * 8 ];

    // Bootstrap video sequence reading
    PlanarImage * currImage = CreatePlanarImage(width, height);

    pCapture->GetSample(0, currImage);

    cl::size_t<3> origin;
    origin[0] = 0;
    origin[1] = 0;
    origin[2] = 0;
    cl::size_t<3> region;
    region[0] = width;
    region[1] = height;
    region[2] = 1;
    // Copy to tiled image memory - this copy (and its overhead) is not necessary in a full GPU pipeline
    queue.enqueueWriteImage(srcImage, CL_TRUE, origin, region, currImage->PitchY, 0, currImage->Y);

    // Process all frames
    double ioStat = 0;//file i/o
    double meStat = 0;//motion estimation itself

    unsigned flags = 0; //no optional modes or behaviors used in computing motion estimation
    unsigned skipBlockType = kMBBlockType;
    unsigned costPenalty = kCostPenalty;
    unsigned costPrecision = kCostPrecision;

    double overallStart  = time_stamp();
    // First frame is already in srcImg, so we start with the second frame
    for (int i = 1; i < numPics; i++)
    {
        for( int j = 0; j < mbImageWidth * mbImageHeight; j++ )
        {
            unsigned offset = mvImageWidth * mvImageHeight;
            for( int k = 0; k < 8; k++ )
            {
                int numComponents = kMBBlockType ? 4 : 1;
                for( int l = 0; l < numComponents; l++ )
                {
                    skipMVMem[ j * 8 * numComponents + k * numComponents + l ].s[ 0 ] =
                        skipMVs[ i * offset + j * numComponents + l ].s[ 0 ] + 0;
                    skipMVMem[ j * 8 * numComponents + k * numComponents + l ].s[ 1 ] =
                        skipMVs[ i * offset + j * numComponents + l ].s[ 1 ] + 0;
                }
            }
        }

        cl::Buffer skipMVBuffer(
            context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            mvImageWidth * mvImageHeight * 8 * sizeof(cl_short2), skipMVMem, NULL);

        // Phase (1)

        double ioStart = time_stamp();
        // Load next picture

        pCapture->GetSample(i, currImage);

        std::swap(refImage, srcImage);
        // Copy to tiled image memory - this copy (and its overhead) is not necessary in a full GPU pipeline
        queue.enqueueWriteImage(srcImage, CL_TRUE, origin, region, currImage->PitchY, 0, currImage->Y);

        ioStat += (time_stamp() -ioStart);

        double meStart = time_stamp();
        // Schedule full-frame motion estimation
        kernel.setArg(0, accelerator);
        kernel.setArg(1, srcImage);
        kernel.setArg(2, refImage);
        kernel.setArg(3, sizeof(unsigned), &flags);         // optional modes or behaviors flags
        kernel.setArg(4, sizeof(unsigned), &skipBlockType); // skip block type
        kernel.setArg(5, sizeof(unsigned), &costPenalty);   // cost penalty
        kernel.setArg(6, sizeof(unsigned), &costPrecision); // cost precision
        kernel.setArg(7, countSkipBuffer );
        kernel.setArg(8, predBuffer);                       // predictors

        kernel.setArg(9, skipMVBuffer);                     // use fbr result as skip check i/p
        kernel.setArg(10, searchMVBuffer);                  // search mvs
        kernel.setArg(11, sizeof(cl_mem), NULL);            // no intra
        kernel.setArg(12, searchResidualBuffer );           // search residuals
        kernel.setArg(13, skipResidualBuffer);              // skip residuals
        kernel.setArg(14, sizeof(cl_mem), NULL);            // no intra residuals

        queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(width, height), cl::NullRange);

        queue.finish();
        meStat += (time_stamp() - meStart);

        ioStart = time_stamp();

        // Read back resulting MVs (in a sync way)
        void * pSearchMVs = &searchMVs[i * mvImageWidth * mvImageHeight];
        queue.enqueueReadBuffer(searchMVBuffer,CL_TRUE,0,sizeof(MotionVector) * mvImageWidth * mvImageHeight,pSearchMVs,0,0);

        // Read back resulting SADs (in a sync way)
        void * pSearchSADs = &searchSADs[i * mvImageWidth * mvImageHeight];
        queue.enqueueReadBuffer(searchResidualBuffer,CL_TRUE,0,sizeof(cl_ushort) * mvImageWidth * mvImageHeight,pSearchSADs,0,0);

        // Read back resulting SADs (in a sync way)
        void * pSkipSADs = &skipSADs[i * mvImageWidth * mvImageHeight * 8];
        queue.enqueueReadBuffer(skipResidualBuffer,CL_TRUE,0,sizeof(cl_ushort) * mvImageWidth * mvImageHeight * 8,pSkipSADs,0,0);

        ioStat += (time_stamp() -ioStart);
    }
    double overallStat  = time_stamp() - overallStart;
    std::cout << std::setiosflags(std::ios_base::fixed) << std::setprecision(3);
    std::cout << "Overall time for " << numPics << " frames " << overallStat << " sec\n" ;
    std::cout << "Average frame file I/O time per frame " << 1000*ioStat/numPics << " ms\n";
    std::cout << "Average Motion Estimation time per frame is " << 1000*meStat/numPics << " ms\n";

    pfn_clReleaseAcceleratorINTEL(accelerator);
    ReleaseImage(currImage);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// Overlay routines
//////////////////////////////////////////////////////////////////////////////////////////////////////////
// Draw a pixel on Y picture
typedef uint8_t U8;
void DrawPixel(int x, int y, U8 *pPic, int nPicWidth, int nPicHeight, U8 u8Pixel)
{
    int nPixPos;

    if (x<0 || x>=nPicWidth || y<0 || y>=nPicHeight)
        return;         // Don't draw out of bound pixels
    nPixPos = y * nPicWidth + x;
    *(pPic+nPixPos) = u8Pixel;
}
// Bresenham's line algorithm
void DrawLine(int x0, int y0, int dx, int dy, U8 *pPic, int nPicWidth, int nPicHeight, U8 u8Pixel)
{
    using std::swap;

    int x1 = x0 + dx;
    int y1 = y0 + dy;
    bool bSteep = abs(dy) > abs(dx);
    if (bSteep)
    {
        swap(x0, y0);
        swap(x1, y1);
    }
    if (x0 > x1)
    {
        swap(x0, x1);
        swap(y0, y1);
    }
    int nDeltaX = x1 - x0;
    int nDeltaY = abs(y1 - y0);
    int nError = nDeltaX / 2;
    int nYStep;
    if (y0 < y1)
        nYStep = 1;
    else
        nYStep = -1;

    for (x0; x0 <= x1; x0++)
    {
        if (bSteep)
            DrawPixel(y0, x0, pPic, nPicWidth, nPicHeight, u8Pixel);
        else
            DrawPixel(x0, y0, pPic, nPicWidth, nPicHeight, u8Pixel);

        nError -= nDeltaY;
        if (nError < 0)
        {
            y0 += nYStep;
            nError += nDeltaX;
        }
    }
}

void OverlayVectors(
    unsigned int subBlockSize,
    const MotionVector* pMV, PlanarImage* srcImage,
    int mbImageWidth, int mbImageHeight,
    int width, int height)
{
    const int nHalfBlkSize = subBlockSize/2;

    int subBlockHeight = 16/subBlockSize;
    int subBlockWidth = 16/subBlockSize;

    for (int i = 0; i < mbImageHeight; i++)
    {
        for (int j = 0; j < mbImageWidth; j++)
        {
            for (int l = 0; l < subBlockHeight; l++)
            {
                for (int m = 0; m < subBlockWidth; m++)
                {
                    DrawLine (
                        j*16+m*subBlockSize+nHalfBlkSize, 
                        i*16+l*subBlockSize+nHalfBlkSize,
                        (pMV[(j+i*mbImageWidth)*subBlockWidth*subBlockHeight+l*subBlockWidth+m].s[0]+2)>>2,
                        (pMV[(j+i*mbImageWidth)*subBlockWidth*subBlockHeight+l*subBlockWidth+m].s[1]+2)>>2,
                        srcImage->Y, width, height, 200);
                }
            }
        }
    }
}

int main( int argc, const char** argv )
{
    try
    {
        CmdParserMV cmd(argc, argv);
        cmd.parse();

        // Immediatly exit if user wanted to see the usage information only.
        if(cmd.help.isSet())
        {
            return 0;
        }

        const int width = cmd.width.getValue();
        const int height = cmd.height.getValue();

        kPredictors = cmd.numberPredictors.getValue();

        //Macro block type setup
        if(cmd.blockType4x4.isSet())
        {
            kMBBlockType = CL_ME_MB_TYPE_4x4_INTEL;
        }
        else if(cmd.blockType8x8.isSet())
        {
            kMBBlockType = CL_ME_MB_TYPE_8x8_INTEL;
        }
        else if(cmd.blockType16x16.isSet())
        {
            kMBBlockType = CL_ME_MB_TYPE_16x16_INTEL;
        }

        //Cost penalty setup
        if(cmd.costPenaltyNone.isSet())
        {
            kCostPenalty = CL_ME_COST_PENALTY_NONE_INTEL;
        }
        else if(cmd.costPenaltyLow.isSet())
        {
            kCostPenalty = CL_ME_COST_PENALTY_LOW_INTEL;
        }
        else if(cmd.costPenaltyNormal.isSet())
        {
            kCostPenalty = CL_ME_COST_PENALTY_NORMAL_INTEL;
        }
        else if(cmd.costPenaltyHigh.isSet())
        {
            kCostPenalty = CL_ME_COST_PENALTY_HIGH_INTEL;
        }

        //Cost precision setup
        if(cmd.costPrecisionQuarter.isSet())
        {
            kCostPrecision = CL_ME_COST_PRECISION_QPEL_INTEL;
        }
        else if(cmd.costPrecisionHalf.isSet())
        {
            kCostPrecision = CL_ME_COST_PRECISION_HPEL_INTEL;
        }
        else if(cmd.costPrecisionFull.isSet())
        {
            kCostPrecision = CL_ME_COST_PRECISION_PEL_INTEL;
        }
        else if(cmd.costPrecisionDouble.isSet())
        {
            kCostPrecision = CL_ME_COST_PRECISION_DPEL_INTEL;
        }

        // Open input sequence
        Capture * pCapture = Capture::CreateFileCapture(FULL_PATH_A(cmd.fileName.getValue()), width, height);
        if (!pCapture)
        {
            throw Error("Failed opening video input sequence...");
        }

        bool differs = false;

        // Process sequence
        std::cout << "Processing " << pCapture->GetNumFrames() << " frames ..." << std::endl;
        std::vector<MotionVector> MVs1;
        std::vector<cl_ushort> SADs;

        ExtractMotionVectorsFullFrameWithOpenCL(pCapture, MVs1, SADs, cmd);

        if( kMBBlockType < CL_ME_MB_TYPE_4x4_INTEL )
        {
            std::vector<MotionVector> MVs2;
            std::vector<cl_ushort> searchSADs;
            std::vector<cl_ushort> skipSADs;
            ComputeCheckMotionVectorsFullFrameWithOpenCL(pCapture, MVs2, MVs1, searchSADs, skipSADs, cmd);

            for( unsigned i = 0; i < MVs1.size(); i++ )
            {			
                differs = differs || MVs2[ i ].s[ 0 ] != MVs1[ i ].s[ 0 ];
                differs = differs || MVs2[ i ].s[ 1 ] != MVs1[ i ].s[ 1 ];
                differs = differs || SADs[ i ] != searchSADs[ i ];
            }

            unsigned mbCount = unsigned(SADs.size() / 4);
            for( unsigned i = 0; i < mbCount; i++ )
            {
                for( unsigned k = 0; k < 8; k++ )
                {
                    unsigned numComponents = kMBBlockType ? 4 : 1;
                    for ( unsigned l = 0; l < numComponents; l++ )
                    {
                        if( SADs[ i * numComponents + l ] || 
                            skipSADs[ i * 8 * numComponents + k * numComponents + l ] )
                        {
                            differs = 
                                differs || 
                                SADs[ i * numComponents + l ] != 
                                skipSADs[ i * 8 * numComponents + k * numComponents + l ];
                        }
                    }
                }
            }

            if( !differs )
            {
                std::cout << "Skip test PASSED!\n";
            }
            else if ( kCostPenalty )
            {
                std::cout << "Skip test results not verified due to cost penalty used in ME.\n";
            }
            else
            {
                std::cout << "Skip test FAILED!\n";
            }
        }

        // Generate sequence with overlaid motion vectors
        FrameWriter * pWriter = FrameWriter::CreateFrameWriter(width, height, !cmd.noOutputToBMP.getValue());
        pWriter->WriteToFile(cmd.overlayFileName.getValue().c_str());
        PlanarImage * srcImage = CreatePlanarImage(width, height);

        int mvImageWidth, mvImageHeight;
        int mbImageWidth, mbImageHeight;
        ComputeNumMVs(kMBBlockType,
            width, height,
            mvImageWidth, mvImageHeight,
            mbImageWidth, mbImageHeight);
        unsigned int subBlockSize = ComputeSubBlockSize(kMBBlockType);

        for (int k = 0; k < pCapture->GetNumFrames(); k++)
        {
            pCapture->GetSample(k, srcImage);
            // Overlay MVs on Src picture, except the very first one
            if(k>0)
                OverlayVectors(subBlockSize, &MVs1[k*mvImageWidth*mvImageHeight], srcImage, mbImageWidth, mbImageHeight, width, height);
            pWriter->AppendFrame(srcImage);
        }

        std::cout << "Writing " << pCapture->GetNumFrames() << " frames to " << cmd.overlayFileName.getValue() << "..." << std::endl;

        FrameWriter::Release(pWriter);
        Capture::Release(pCapture);
        ReleaseImage(srcImage);
    }
    catch (cl::Error & err)
    {
        std::cout << err.what() << "(" << err.err() << ")" << std::endl;
        return 1;
    }
    catch (std::exception & err)
    {
        std::cout << err.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cout << "Unknown exception! Exit...";
        return 1;
    }

    std::cout << "Done!" << std::endl;

    return 0;
}