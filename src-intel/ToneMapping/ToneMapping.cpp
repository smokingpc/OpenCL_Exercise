// Copyright (c) 2009-2011 Intel Corporation
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

#include "stdafx.h"

#include <iostream>
#include "basic.hpp"
#include "cmdparser.hpp"
#include "oclobject.hpp"
#include "utils.h"
#include "basic.hpp"


using namespace std;

#pragma warning( push )


// to enable kernel version with per pixel processing
#define PER_PIXEL

// allocate buffer for image data and read these data from file to allocated buffer
cl_float* readInput(cl_uint* p_width, cl_uint* p_height, cl_uint dev_alignment)
{
    // Load from HDR-image
    int iMemSize = 0;
    int iWidth = 0;
    int iHeight = 0;

    cl_float* p_input = 0;

#ifdef __linux__
    std::string tmp = wstringToString(L"ToneMapping.rgb");
    FILE* pRGBAFile = fopen(FULL_PATH_A(tmp.c_str()),"rb");
#else
    FILE* pRGBAFile = _wfopen(FULL_PATH_W("ToneMapping.rgb"),L"rb");
#endif    
    
    
    if(!pRGBAFile)
        throw Error(string("Failed to open input ") + "ToneMapping.rgb" + " image!");

    fread((void*)&iWidth, sizeof(int), 1, pRGBAFile);
    fread((void*)&iHeight, sizeof(int), 1, pRGBAFile);
    printf("width = %d\n", iWidth);
    printf("height = %d\n", iHeight);

    if(iWidth<=0 || iHeight<=0 || iWidth > 1000000 || iHeight > 1000000)
    {
        fclose(pRGBAFile);
        throw Error("Width or height values are invalid in the data file!");
    }

    //! The image size in memory (bytes).
    iMemSize = iWidth*iHeight*4*sizeof(cl_float);

    //! Allocate memory.
    p_input = (cl_float*)aligned_malloc(zeroCopySizeAlignment(iMemSize), dev_alignment);
    if(!p_input)
    {
        fclose(pRGBAFile);
        throw Error("Failed to allocate memory for input HDR image!");
    }

    //! Read data from the input file to memory.
    fread((void*)p_input, 1, iMemSize, pRGBAFile);

    // HDR-image hight & weight
    *p_width = iWidth;
    *p_height = iHeight;

    fclose(pRGBAFile);

    return p_input;
}

// declaration of native function
void EvaluateRaw(float* p_input, float* p_output, CHDRData *pData, int width, int iRow);

void ExecuteToneMappingReference(cl_float* p_input, cl_float* p_output, CHDRData *pData, cl_uint width, cl_uint height)
{
    // image height loop
    for(cl_uint j = 0; j < height;j++)
    {
        EvaluateRaw(p_input, p_output, pData, width, j);
    }
}

float ExecuteToneMappingKernel(cl_float* p_input, cl_float* p_output, CHDRData HDRData, cl_uint width, cl_uint height, OpenCLBasic& ocl, OpenCLProgramOneKernel& exec)
{
    cl_int          err = CL_SUCCESS;
    double   perf_start;
    double   perf_stop;

    // create OCL buffers
    cl_mem cl_input_buffer =
        clCreateBuffer
        (
            ocl.context,
            CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
            zeroCopySizeAlignment(sizeof(cl_float) * 4 * width * height, ocl.device),
            p_input,
            &err
        );
    SAMPLE_CHECK_ERRORS(err);
    if (cl_input_buffer == (cl_mem)0)
        throw Error("Failed to create Input Buffer!");

    cl_mem cl_output_buffer =
        clCreateBuffer
        (
            ocl.context,
            CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR,
            zeroCopySizeAlignment(sizeof(cl_float) * 4 * width * height, ocl.device),
            p_output,
            &err
        );
    SAMPLE_CHECK_ERRORS(err);
    if (cl_output_buffer == (cl_mem)0)
        throw Error("Failed to create Output Buffer!");

    err  = clSetKernelArg(exec.kernel, 0, sizeof(cl_mem), (void *) &cl_input_buffer);
    SAMPLE_CHECK_ERRORS(err);
    err  = clSetKernelArg(exec.kernel, 1, sizeof(cl_mem), (void *) &cl_output_buffer);
    SAMPLE_CHECK_ERRORS(err);
    err  = clSetKernelArg(exec.kernel, 2, sizeof(CHDRData), (void *)&HDRData);
    SAMPLE_CHECK_ERRORS(err);
    err  = clSetKernelArg(exec.kernel, 3, sizeof(cl_int), (void *) &width);
    SAMPLE_CHECK_ERRORS(err);

#ifdef PER_PIXEL
    size_t global_size[2] = {width,height};
    printf("Global work size %dx%d\n", global_size[0],global_size[1]);
#else
    size_t global_size[1] = {height};
    printf("Global work size %d\n", global_size[0]);
#endif


    // execute kernel
    perf_start = time_stamp();
    err = clEnqueueNDRangeKernel(ocl.queue, exec.kernel, sizeof(global_size)/sizeof(size_t), NULL, global_size, NULL, 0, NULL, NULL);
    SAMPLE_CHECK_ERRORS(err);
    err = clFinish(ocl.queue);
    SAMPLE_CHECK_ERRORS(err);
    perf_stop = time_stamp();

    //map data back to the host
    void* tmp_ptr = clEnqueueMapBuffer(ocl.queue, cl_output_buffer, true, CL_MAP_READ, 0, sizeof(cl_float4) * width * height, 0, NULL, NULL, &err);
    SAMPLE_CHECK_ERRORS(err);
    if(tmp_ptr!=p_output)
    {
        throw Error("clEnqueueMapBuffer failed to return original pointer");
    }
    err = clEnqueueUnmapMemObject(ocl.queue, cl_output_buffer, tmp_ptr, 0, NULL, NULL);
    SAMPLE_CHECK_ERRORS(err);

    err = clReleaseMemObject(cl_input_buffer);
    SAMPLE_CHECK_ERRORS(err);
    err = clReleaseMemObject(cl_output_buffer);
    SAMPLE_CHECK_ERRORS(err);

    // retrieve perf. counter frequency
    return (float)(perf_stop - perf_start);
}


// calculate FStops value parameter from the arguments
float resetFStopsParameter( float powKLow, float kHigh )
{
    float curveBoxWidth = pow( 2.0f, kHigh ) - powKLow;
    float curveBoxHeight = pow( 2.0f, 3.5f )  - powKLow;

    // Initial boundary values
    float fFStopsLow = 0.0f;
    float fFStopsHigh = 100.0f;
    int iterations = 23; //interval bisection iterations

    // Interval bisection to find the final knee function fStops parameter
    for ( int i = 0; i < iterations; i++ )
    {
        float fFStopsMiddle = ( fFStopsLow + fFStopsHigh ) * 0.5f;
        if ( ( curveBoxWidth * fFStopsMiddle + 1.0f ) < exp( curveBoxHeight * fFStopsMiddle ) )
        {
            fFStopsHigh = fFStopsMiddle;
        }
        else
        {
            fFStopsLow = fFStopsMiddle;
        }
    }

    return ( fFStopsLow + fFStopsHigh ) * 0.5f;
}

// main execution routine - perform Tone Mapping post-processing on float4 vectors
int main (int argc, const char** argv)
{
    int ret = EXIT_SUCCESS; //return code
    // pointer to the HOST buffers
    cl_float* p_input = NULL;
    cl_float* p_output = NULL;
    cl_float* p_ref = NULL;
    try
    {
        // init HDR parameters
        float kLow = -3.0f;
        float kHigh = 7.5f;
        float exposure = 3.0f;
        float gamma = 1.0f;
        float defog = 0.0f;

        cl_uint width;
        cl_uint height;

        // Define and parse command-line arguments.
        CmdParserCommon cmd(argc, argv);
        CmdOptionErrors param_max_error_count(cmd);
        cmd.parse();

        // Immediatly exit if user wanted to see the usage information only.
        if(cmd.help.isSet())
        {
            return 0;
        }

        // Create the necessary OpenCL objects up to device queue.
        OpenCLBasic ocl(
            cmd.platform.getValue(),
            cmd.device_type.getValue(),
            cmd.device.getValue()
        );

        // Build kernel
#ifdef PER_PIXEL
        OpenCLProgramOneKernel exec(ocl,L"ToneMapping.cl","","ToneMappingPerPixel","-cl-fast-relaxed-math -cl-denorms-are-zero");
#else
        OpenCLProgramOneKernel exec(ocl,L"ToneMapping.cl","","ToneMappingLine","-cl-fast-relaxed-math -cl-denorms-are-zero");
#endif

        // read input image
        cl_uint     dev_alignment = zeroCopyPtrAlignment(ocl.device);
        p_input = readInput(&width, &height,dev_alignment);
        // Extended dynamic range
        for(cl_uint i = 0; i < width*height*4; i++)
        {
            p_input[i] = 5.0f*p_input[i];
        }
        SaveImageAsBMP_32FC4( p_input, 255.0f, width, height, "ToneMappingInput.bmp");
        printf("Input size is %d X %d\n", width, height);
        size_t      aligned_size = zeroCopySizeAlignment(sizeof(cl_float) * 4 * width * height, ocl.device);
        p_output = (cl_float*)aligned_malloc(aligned_size, dev_alignment);
        p_ref = (cl_float*)aligned_malloc(aligned_size, dev_alignment);


        // fill HDR parameters structure
        CHDRData HDRData;
        HDRData.fGamma = gamma;
        HDRData.fPowGamma = pow(2.0f, -3.5f*gamma);
        HDRData.fDefog = defog;

        HDRData.fPowKLow = pow( 2.0f, kLow );
        HDRData.fPowKHigh = pow( 2.0f, kHigh );
        HDRData.fPow35 = pow(2.0f, 3.5f);
        HDRData.fPowExposure = pow( 2.0f, exposure +  2.47393f );

        // calculate FStops
        HDRData.fFStops = resetFStopsParameter(HDRData.fPowKLow, kHigh);
        printf("resetFStopsParameter result = %f\n", HDRData.fFStops);

        HDRData.fFStopsInv = 1.0f/HDRData.fFStops;

        // do tone mapping
        printf("Executing OpenCL kernel...\n");
        float ocl_time = ExecuteToneMappingKernel(p_input, p_output, HDRData, width, height, ocl, exec);

        printf("Executing reference...\n");
        ExecuteToneMappingReference(p_input, p_ref, &HDRData, width, height);

        // save results in bitmap files

        SaveImageAsBMP_32FC4( p_output, 1.0f, width, height, "ToneMappingOutput.bmp");
        SaveImageAsBMP_32FC4( p_ref, 1.0f, width, height, "ToneMappingOutputReference.bmp");

        // Do verification
        printf("Performing verification...\n");
        int error_count = 0;
        for(cl_uint i = 0; i < width*height*4; i++)
        {
            // Compare the data
            if( fabsf(p_output[i] - p_ref[i]) > 0.1 )
            {
                printf("Error at location %d,  p_output = %f, p_ref = %f \n", i, p_output[i], p_ref[i]);
                error_count++;
                if(param_max_error_count.getValue()>0 && error_count>=param_max_error_count.getValue())
                    break;
            }
        }
        if(error_count)
        {
            printf("ERROR: Verification failed.\n");
            ret = EXIT_FAILURE;
        }
        else
        {
            printf("Verification succeeded.\n");
        }

        printf("NDRange perf. counter time %f ms.\n", ocl_time*1000.f);
    }
    catch(const CmdParser::Error& error)
    {
        cerr
            << "[ ERROR ] In command line: " << error.what() << "\n"
            << "Run " << argv[0] << " -h for usage info.\n";
        ret = EXIT_FAILURE;
    }
    catch(const Error& error)
    {
        cerr << "[ ERROR ] Sample application specific error: " << error.what() << "\n";
        ret = EXIT_FAILURE;
    }
    catch(const exception& error)
    {
        cerr << "[ ERROR ] " << error.what() << "\n";
        ret = EXIT_FAILURE;
    }
    catch(...)
    {
        cerr << "[ ERROR ] Unknown/internal error happened.\n";
        ret = EXIT_FAILURE;
    }

    aligned_free( p_ref );
    aligned_free( p_input );
    aligned_free( p_output );

    return ret;
}

#pragma warning( pop )
