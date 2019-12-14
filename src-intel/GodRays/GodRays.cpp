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

#include <iostream>

#ifndef __linux__
#include "stdafx.h"
#else
#include "math.h"
#endif

#include "utils.h"
#include "basic.hpp"
#include "cmdparser.hpp"
#include "oclobject.hpp"
#include "basic.hpp"


using namespace std;

#pragma warning( push )

#define BLOCK_DIM 64
#define GOD_RAYS_BUNCH_SIZE 1


// allocate buffer for image data and read these data from file to allocated buffer
cl_float* readInput(cl_uint* width, cl_uint* height, cl_uint dev_alignment)
{
    // Load from HDR-image

    //!Variables
    int x = 0;
    int y = 0;
    int iMemSize = 0;
    int iResultMemSize = 0;
    float fTmpVal = 0.0f;
    int iWidth = 0;
    int iHeight = 0;
    cl_float* p_input = 0;

#ifdef __linux__
    std::string tmp = wstringToString(L"GodRays.rgb");
    FILE* pRGBAFile = fopen(FULL_PATH_A(tmp.c_str()),"rb");
#else
    FILE* pRGBAFile = _wfopen(FULL_PATH_W("GodRays.rgb"),L"rb");
#endif
    if(!pRGBAFile)
        throw Error("Failed to create input data Buffer!");

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
    p_input = (cl_float*)aligned_malloc(iMemSize, dev_alignment);
    if(!p_input)
    {
        fclose(pRGBAFile);
        throw Error("Failed to allocate memory for input HDR image!");
    }

    //! Read data from the input file to memory.
    fread((void*)p_input, 1, iMemSize, pRGBAFile);

    // HDR-image hight & weight
    *width = iWidth;
    *height = iHeight;

    fclose(pRGBAFile);

    return p_input;
}

// declaration of native function
void EvaluateRay(
                 float* inBuf,
                 int iw,
                 int ih,
                 int blend,
                 float* outBuf,
                 int in_RayNum,
                 int god_rays_b_size
                 );

void ExecuteGodRaysReference(cl_float* p_input, cl_float* p_output, cl_uint width, cl_uint height, cl_uint blend, size_t global_work_size)
{
    // rays bunch loop
    for(cl_uint j = 0; j < global_work_size;j++)
    {
        EvaluateRay(p_input, width, height, blend, p_output, j, GOD_RAYS_BUNCH_SIZE);
    }
}

float ExecuteGodRaysKernel(cl_float* p_input, cl_float* p_output, cl_uint width, cl_uint height, cl_uint blend, size_t* p_global_work_size, OpenCLBasic &oclobjects, OpenCLProgramOneKernel &executable)
{
    cl_int   err = CL_SUCCESS;
    double   perf_start;
    double   perf_stop;

    // create OCL buffers
    cl_mem cl_input_buffer =
        clCreateBuffer
        (
            oclobjects.context,
            CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
            zeroCopySizeAlignment(sizeof(cl_float) * 4 * width * height),
            p_input,
            &err
        );
    SAMPLE_CHECK_ERRORS(err);
    if (cl_input_buffer == (cl_mem)0)
        throw Error("Failed to create Input Buffer!");

    cl_mem cl_output_buffer = clCreateBuffer(oclobjects.context, CL_MEM_WRITE_ONLY, sizeof(cl_float) * 4 * width * height, NULL, &err);
    SAMPLE_CHECK_ERRORS(err);
    if (cl_output_buffer == (cl_mem)0)
        throw Error("Failed to create Output Buffer!");

    // set kernel arguments
    err  = clSetKernelArg(executable.kernel, 0, sizeof(cl_mem), (void *) &cl_input_buffer);
    SAMPLE_CHECK_ERRORS(err);
    err  = clSetKernelArg(executable.kernel, 1, sizeof(cl_mem), (void *) &cl_output_buffer);
    SAMPLE_CHECK_ERRORS(err);
    err  = clSetKernelArg(executable.kernel, 2, sizeof(cl_int), (void *) &width);
    SAMPLE_CHECK_ERRORS(err);
    err  = clSetKernelArg(executable.kernel, 3, sizeof(cl_int), (void *) &height);
    SAMPLE_CHECK_ERRORS(err);
    err  = clSetKernelArg(executable.kernel, 4, sizeof(cl_int), (void *) &blend);
    SAMPLE_CHECK_ERRORS(err);

    size_t p_local_work_size[1] = {BLOCK_DIM};
    printf("Original global work size %d\n", p_global_work_size[0]);
    printf("Original local work size %d\n", p_local_work_size[0]);
    p_global_work_size[0] = (p_global_work_size[0] + (p_local_work_size[0]-1)) & ~(p_local_work_size[0]-1);
    printf("Corrected global work size %d\n", p_global_work_size[0]);

    // execute kernel
    perf_start=time_stamp();
    err = clEnqueueNDRangeKernel(oclobjects.queue, executable.kernel, 1, NULL, p_global_work_size, p_local_work_size, 0, NULL, NULL);
    SAMPLE_CHECK_ERRORS(err);
    err = clFinish(oclobjects.queue);
    SAMPLE_CHECK_ERRORS(err);
    perf_stop=time_stamp();

    // read data back to the HOST
    err = clEnqueueReadBuffer(oclobjects.queue, cl_output_buffer, CL_TRUE, 0, sizeof(cl_float) * 4 * width * height, p_output, 0, NULL, NULL);
    SAMPLE_CHECK_ERRORS(err);
    err = clFinish(oclobjects.queue);
    SAMPLE_CHECK_ERRORS(err);

    // release OCL buffers
    err = clReleaseMemObject(cl_input_buffer);
    SAMPLE_CHECK_ERRORS(err);
    err = clReleaseMemObject(cl_output_buffer);
    SAMPLE_CHECK_ERRORS(err);

    // retrieve perf. counter frequency
    return (float)(perf_stop - perf_start);
}


// main execution routine - perform God Rays post-processing on float4 vectors
int main (int argc, const char** argv)
{
    int ret = EXIT_SUCCESS; //return code
    // pointer to the HOST buffers
    cl_float*   p_input = NULL;
    cl_float*   p_output = NULL;
    cl_float*   p_ref = NULL;
    try
    {
        cl_uint     width;
        cl_uint     height;
        cl_uint     blend=1;
        size_t      global_work_size = 0; // global work size will be calculated from input image size and local size

        // Define and parse command-line arguments.
        CmdParserCommon cmd(argc, argv);
        cmd.device_type.setValuePlaceholder("cpu | gpu");
        cmd.device_type.setDefaultValue("cpu|gpu");
        CmdOptionErrors param_max_error_count(cmd);
        cmd.parse();

        // Immediatly exit if user wanted to see the usage information only.
        if(cmd.help.isSet())
        {
            return EXIT_SUCCESS;
        }

        cl_device_type  dev_type = parseDeviceType(cmd.device_type.getValue());
        if(dev_type & ~(CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_CPU) )
        {
            printf("ERROR: Device type '%s' is not supported by this sample\n",cmd.device_type.getValue().c_str());
            return EXIT_FAILURE;
        }

        // Create the necessary OpenCL objects up to device queue.
        OpenCLBasic oclobjects(
            cmd.platform.getValue(),
            cmd.device_type.getValue(),
            cmd.device.getValue()
        );

        // Build kernel
        OpenCLProgramOneKernel executable(oclobjects,L"GodRays.cl","","GodRays");

        // read input image
        cl_uint     dev_alignment = zeroCopyPtrAlignment(oclobjects.device);
        p_input = readInput(&width, &height,dev_alignment);
        size_t aligned_size = zeroCopySizeAlignment(sizeof(cl_float) * 4 * width * height, oclobjects.device);
        printf("Input size is %d X %d\n", width, height);
        p_output = (cl_float*)aligned_malloc(aligned_size, dev_alignment);
        p_ref = (cl_float*)aligned_malloc(aligned_size, dev_alignment);

        SaveImageAsBMP_32FC4(p_input,255.0f,width,height,"GodRaysInput.bmp");

        //! Calculate global work size
        global_work_size = 2*(width + height-2)/GOD_RAYS_BUNCH_SIZE+1;

        // do god rays
        printf("Executing OpenCL kernel...\n");
        float ocl_time = ExecuteGodRaysKernel(p_input, p_output, width, height, blend, &global_work_size, oclobjects,executable);

        printf("Executing reference...\n");
        ExecuteGodRaysReference(p_input, p_ref, width, height, blend, global_work_size);

        SaveImageAsBMP_32FC4(p_output,255.0f,width,height,"GodRaysOutput.bmp");
        SaveImageAsBMP_32FC4(p_ref,255.0f,width,height,"GodRaysOutputReference.bmp");

        // Do verification
        printf("Performing verification...\n");
        int error_count = 0;
        for(cl_uint i = 0; i < width*height*4; i++)
        {
            // Compare the data
            if( fabsf(p_output[i] - p_ref[i]) > 0.01 )
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

        printf("NDRange perf. counter time %f ms.\n",1000.0f*ocl_time);

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
