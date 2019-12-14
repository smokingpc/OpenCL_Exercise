// Copyright (c) 2009-2013 Intel Corporation
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

// Specifies number of motion vectors per source pixel block (the value of CL_ME_MB_TYPE_16x16_INTEL specifies  just a single vector per block )
static const cl_uint kMBBlockType = CL_ME_MB_TYPE_16x16_INTEL;

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4355)    // 'this': used in base member initializer list
#endif


// All command-line options for the sample
class CmdParserMV : public CmdParser
{
public:
    CmdOption<std::string>         fileName;
    CmdOption<std::string>         overlayFileName;
    CmdOption<int>		width;
    CmdOption<int>      height;
    CmdOption<bool>		help;
    CmdOption<bool>		no_output_to_bmp;

    CmdParserMV  (int argc, const char** argv) :
    CmdParser(argc, argv),
        no_output_to_bmp(*this, 0,"nobmp","","Do not output frames to the sequence of bmp files (in addition to the yuv file), by default the output is on"),
        help(*this,            'h',"help","","Show this help text and exit."),
        fileName(*this,         0,"input", "<string>", "Input video sequence filename (.yuv file format)","video_1920x1080_5frames.yuv"),
        overlayFileName(*this,  0,"output","<string>", "Output video sequence with overlaid motion vectors filename ","output.yuv"),
        width(*this,            0, "width",	"<integer>", "Frame width for the input file", 1920),
        height(*this,           0, "height","<integer>", "Frame height for the input file",1080)
    {
    }
    virtual void parse ()
    {
        CmdParser::parse();
        if(help.isSet())
        {
            printUsage(std::cout);
        }
    }
};
#ifdef _MSC_VER
#pragma warning (pop)
#endif

inline void ComputeNumMVs( cl_uint nMBType, int nPicWidth, int nPicHeight, int & nMVSurfWidth, int & nMVSurfHeight )
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

void OverlayVectors(unsigned int subBlockSize, const MotionVector* pMV, PlanarImage* srcImage, const int& mvImageWidth, const int& mvImageHeight, const int& width, const int& height);

void ExtractMotionVectorsFullFrameWithOpenCL( Capture * pCapture, const CmdParserMV& cmd)
{

    // OpenCL initialization
    OpenCLBasic init("Intel", "GPU");
    //OpenCLBasic creates the platform/context and device for us, so all we need is to get an ownership (via incrementing ref counters with clRetainXXX)

    cl::Context context = cl::Context(init.context); clRetainContext(init.context);
    cl::Device device  = cl::Device(init.device);   clRetainDevice(init.device);
    cl::CommandQueue queue = cl::CommandQueue(init.queue);clRetainCommandQueue(init.queue);

    std::string ext = device.getInfo< CL_DEVICE_EXTENSIONS >();
    if (string::npos == ext.find("cl_intel_accelerator") || string::npos == ext.find("cl_intel_motion_estimation"))
    {
        throw Error("Error, the selected device doesn't support motion estimation or accelerator extensions!");
    }

    CL_EXT_INIT_WITH_PLATFORM( init.platform, clCreateAcceleratorINTEL );
    CL_EXT_INIT_WITH_PLATFORM( init.platform, clReleaseAcceleratorINTEL );

    // Create a built-in VME kernel
    cl_int err = 0;
    const cl_device_id & d = device();
    cl::Program p (clCreateProgramWithBuiltInKernels( context(), 1, &d, "block_motion_estimate_intel", &err ));
    if (err != CL_SUCCESS)
    {
        throw cl::Error(err, "Failed creating builtin kernel(s)");
    }
    cl::Kernel kernel(p, "block_motion_estimate_intel");

    // VME API configuration knobs
    cl_motion_estimation_desc_intel desc = {
        kMBBlockType,                                     // Number of motion vectors per source pixel block (the value of CL_ME_MB_TYPE_16x16_INTEL specifies  just a single vector per block )
        CL_ME_SUBPIXEL_MODE_INTEGER_INTEL,                // Motion vector precision
        CL_ME_SAD_ADJUST_MODE_NONE_INTEL,                 // SAD Adjust (none/Haar transform) for the residuals, but we don't compute them in this tutorial anyway
        CL_ME_SEARCH_PATH_RADIUS_16_12_INTEL              // Search window radius
    };

    // Create an accelerator object (abstraction of the motion estimation acceleration engine)
    cl_accelerator_intel accelerator = pfn_clCreateAcceleratorINTEL(context(), CL_ACCELERATOR_TYPE_MOTION_ESTIMATION_INTEL,
        sizeof(cl_motion_estimation_desc_intel), &desc, &err);
    if (err != CL_SUCCESS)
    {
        throw cl::Error(err, "Error creating motion estimation accelerator object.");
    }

    const int numPics = pCapture->GetNumFrames();
    const int width = cmd.width.getValue();
    const int height = cmd.height.getValue();
    FrameWriter * pWriter= FrameWriter::CreateFrameWriter(width, height, !cmd.no_output_to_bmp.getValue());
    pWriter->WriteToFile(cmd.overlayFileName.getValue().c_str());

    int mvImageWidth, mvImageHeight;
    ComputeNumMVs(kMBBlockType, width, height, mvImageWidth, mvImageHeight);
    unsigned int subBlockSize = ComputeSubBlockSize(kMBBlockType);
    ComputeNumMVs(desc.mb_block_type, width, height, mvImageWidth, mvImageHeight);
    std::vector<MotionVector> MVs;
    MVs.resize(mvImageWidth*mvImageHeight);
    // Set up OpenCL surfaces
    cl::ImageFormat imageFormat(CL_R, CL_UNORM_INT8);
    cl::Image2D refImage(context, CL_MEM_READ_ONLY, imageFormat, width, height, 0, 0);
    cl::Image2D srcImage(context, CL_MEM_READ_ONLY, imageFormat, width, height, 0, 0);
    cl::Buffer mvBuffer(context, CL_MEM_WRITE_ONLY, mvImageWidth * mvImageHeight * sizeof(MotionVector));

    // Bootstrap video sequence reading
    PlanarImage * currImage = CreatePlanarImage(width, height);
    pCapture->GetSample(0, currImage);
    // Write the (unmodified) first frame to the output stream
    pWriter->AppendFrame(currImage);
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

    // Generate sequence with overlaid motion vectors
    double overallStart  = time_stamp();
    // First frame is already in srcImg, so we start with the second frame
    for (int i = 1; i < numPics; i++)
    {
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
        kernel.setArg(3, sizeof(cl_mem), NULL);//in this simple tutorial we have no "prediction" vectors for the input (often, the motion vectors from downscaled image or from the prev. frame are used)
        kernel.setArg(4, mvBuffer);
        kernel.setArg(5, sizeof(cl_mem), NULL); //in this simple tutorial we don't want to compute residuals
        queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(width, height), cl::NullRange);
        queue.finish();
        meStat += (time_stamp() - meStart);
        ioStart = time_stamp();
        // Read back resulting motion vectors (in a sync way)
        queue.enqueueReadBuffer(mvBuffer,CL_TRUE,0,sizeof(MotionVector) * MVs.size(), &MVs[0],0,0);
        // Overlay MVs on Src picture
        OverlayVectors(subBlockSize, &MVs[0], currImage, mvImageWidth, mvImageHeight, width, height);
        pWriter->AppendFrame(currImage);
        ioStat += (time_stamp() -ioStart);
    }
    std::cout << std::endl << "Writing " << pCapture->GetNumFrames() << " frames to " << cmd.overlayFileName.getValue() << " finished!" << std::endl<< std::endl;
    double overallStat  = time_stamp() - overallStart;
    std::cout << std::setiosflags(std::ios_base::fixed) << std::setprecision(3);
    std::cout << "Overall time for " << numPics << " frames is " << overallStat << " sec\n" ;
    std::cout << "      Average frame file I/O time per frame is " << 1000*ioStat/numPics << " ms\n";
    std::cout << "      Average Motion Estimation time per frame is " << 1000*meStat/numPics << " ms\n";

    pfn_clReleaseAcceleratorINTEL(accelerator);
    ReleaseImage(currImage);
    FrameWriter::Release(pWriter);
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

void OverlayVectors(unsigned int subBlockSize, const MotionVector* pMV, PlanarImage* srcImage, const int& mvImageWidth, const int& mvImageHeight, const int& width, const int& height)
{
    const int nHalfBlkSize = subBlockSize/2;
    for (int i = 0; i < mvImageHeight; i++)
    {
        for (int j = 0; j < mvImageWidth; j++)
        {
            DrawLine (j*subBlockSize + nHalfBlkSize, i*subBlockSize + nHalfBlkSize,
                (pMV[j+i*mvImageWidth].s[0] + 2) >> 2, (pMV[j+i*mvImageWidth].s[1]+ 2) >> 2,
                srcImage->Y, width, height, 200);
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
        // Open input sequence
        Capture * pCapture = Capture::CreateFileCapture(FULL_PATH_A(cmd.fileName.getValue()), width, height);
        if (!pCapture)
        {
            throw Error("Failed opening video input sequence...");
        }

        // Process sequence
        std::cout << "Processing " << pCapture->GetNumFrames() << " frames ..." << std::endl;
        ExtractMotionVectorsFullFrameWithOpenCL(pCapture, cmd);
        Capture::Release(pCapture);
    }
    catch(const CmdParser::Error& error)
    {
        std::cout
            << "[ ERROR ] In command line: " << error.what() << "\n"
            << "Run " << argv[0] << " -h for usage info.\n";
        return 1;
    }
    catch(const Error& error)
    {
        std::cout << "[ ERROR ] Sample application specific error: " << error.what() << "\n";
        return 1;
    }
    catch(const std::exception& error)
    {
        std::cout << "[ ERROR ] " << error.what() << "\n";
        return 1;
    }
    catch(...)
    {
        std::cout << "[ ERROR ] Unknown/internal error happened.\n";
        return 1;
    }

    std::cout << "Done!" << std::endl;
    return 0;
}
