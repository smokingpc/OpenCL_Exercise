// Copyright (c) 2014 Intel Corporation
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

#include "basic.hpp"
#include "cmdparser.hpp"
#include "oclobject.hpp"
#include "utils.h"

using namespace std;

// constants for vertical and horizontal image padding
#define PAD_LINES 1
#define XPAD 16

// generate random 8-bit value for every pixel of the given image
void generateInput(cl_uchar* p_input, size_t width, size_t height)
{
    const float rnd_byte_norm = 255.0f/(float)RAND_MAX;
    srand(12345);

    for (cl_uint i = 0; i <  (width+2*XPAD) * (height+2*PAD_LINES); ++i)
    {
        p_input[i] = (cl_uchar) ((float)rand() * rnd_byte_norm);
    }
}

// calculate reference filtered image for verification of OpenCL code correctness
void ExecuteSobelReference(cl_uchar* p_input, cl_uchar* p_output, cl_int width, cl_uint height)
{
    // just to make sure no garbage left in the output
    memset(p_output, 0,   width * height);

    // do Sobel
    for(cl_uint y = 0; y < height; y++)        // rows loop
    {
        int iCurr = (y + PAD_LINES) * (width + XPAD * 2) + XPAD;
        int iPrev = iCurr - width - XPAD * 2;
        int iNext = iCurr + width + XPAD * 2;

        for(int x = 0; x < width; x++)        // columns loop
        {
            // get pixels within aperture
            float a = p_input[iPrev + x - 1];
            float b = p_input[iPrev + x];
            float c = p_input[iPrev + x + 1];

            float d = p_input[iCurr + x - 1];
            float f = p_input[iCurr + x + 1];

            float g = p_input[iNext + x - 1];
            float h = p_input[iNext + x];
            float i = p_input[iNext + x + 1];

            float xVal = (c - a) + 2*(f - d) + (i - g);     // horizontal derivative
            float yVal = (g - a) + 2*(h - b) + (i - c);     // vertical derivative

            // compute gradient, convert and copy to output
            float val = sqrt(xVal*xVal + yVal*yVal);
            p_output[y * width + x] = val > 255. ? 255 : (cl_uchar)val;
        }
    }
}

// compare resulting 8-bit image to the reference on, pixel by pixel
// in case of pixel difference is more than one brightness level from 255,
// report verification failure
bool verify(cl_uchar* p_result, cl_uchar* p_ref, cl_int width, cl_uint height)
{
    for(cl_uint i = 0; i < height*width; i++)
    {
        cl_uchar ref = p_ref[i];
        cl_uchar actual = p_result[i];
        cl_uchar diff = ref > actual ? ref-actual : actual-ref;

        if(diff > 1)
        {
            printf("y = %d, x = %d, ref = 0x%x , actual = 0x%x\n", i/width, i%width, ref, actual);
            return false;
        }
    }
    return true;
}

// set up kernel arguments,
// execute the given kernel
// and get back the output buffer for result verification
double ExecuteSobelKernel(cl_mem  cl_input_buffer,
                           cl_mem  cl_output_buffer,
                           cl_uchar* p_output,
                           size_t   bufSize,    // required for getting the result back
                           size_t*  global_work_size,
                           size_t*  local_work_size,
                           OpenCLBasic& ocl, cl_kernel kernel)
{
    cl_int err = CL_SUCCESS;

    err = clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *) &cl_input_buffer);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *) &cl_output_buffer);
    SAMPLE_CHECK_ERRORS(err);

    // execute kernel
    double start = time_stamp();
    err = clEnqueueNDRangeKernel(ocl.queue, kernel, 2, NULL, global_work_size, local_work_size, 0, NULL, NULL);
    SAMPLE_CHECK_ERRORS(err);
    err = clFinish(ocl.queue);
    SAMPLE_CHECK_ERRORS(err);
    double end = time_stamp();

    void* tmp_ptr = NULL;
    tmp_ptr = clEnqueueMapBuffer(ocl.queue, cl_output_buffer, true, CL_MAP_READ, 0, bufSize, 0, NULL, NULL, &err);
    SAMPLE_CHECK_ERRORS(err);
    if(tmp_ptr!=p_output)
    {
        throw Error("clEnqueueMapBuffer failed to return original pointer");
    }

    err = clFinish(ocl.queue);
    SAMPLE_CHECK_ERRORS(err);

    err = clEnqueueUnmapMemObject(ocl.queue, cl_output_buffer, tmp_ptr, 0, NULL, NULL);
    SAMPLE_CHECK_ERRORS(err);

    return end - start;
}

// main execution routine - performs Sobel filtering with 3x3 kernel size
int main (int argc, const char** argv)
{
    // return code
    int ret = EXIT_SUCCESS;
    // pointer to the HOST buffers
    cl_uchar* p_input = NULL;
    cl_uchar* p_output = NULL;
    cl_uchar* p_ref = NULL;

    cl_mem cl_input_buffer = NULL;
    cl_mem cl_output_buffer = NULL;

    cl_int err = CL_SUCCESS;

    try
    {
        // Define and parse command-line arguments.
        CmdParserCommon cmdparser(argc, argv);

        CmdOption<int> param_width(cmdparser,'W',"width","<integer>","width of processed image",2048);
        CmdOption<int> param_height(cmdparser,'H',"height","<integer>","height of processed image",2048);

        cmdparser.parse();

        // Immediately exit if user wanted to see the usage information only.
        if(cmdparser.help.isSet())
        {
            return EXIT_SUCCESS;
        }

        int width = param_width.getValue();
        int height = param_height.getValue();
        // validate user input parameters
        {
            if(width < 128 || height < 128 || width > 8192 || height > 8192 )
            {
                throw Error("Input size in each dimension should be in the range [64, 8192]!");
            }

            if((width & 0xF) || (height & 0xF))
            {
                throw Error("Input dimensions should be a multiple of 16!");
            }
        }

        printf("Input size is %d X %d\n", width, height);

        // Create the necessary OpenCL objects up to device queue.
        OpenCLBasic oclobjects(
            cmdparser.platform.getValue(),
            cmdparser.device_type.getValue(),
            cmdparser.device.getValue()
        );

        // Build kernels
        // first is a copy of reference Sobel
        // second processes image by uchar4 vectors
        // third processes image by 16x16 tiles and does all the math using floats
        // for detailed explanations on optimizations, see sample documentation
        OpenCLProgramMultipleKernels executable(oclobjects, L"ProcGraphicsOpt.cl", "");
        cl_kernel naiveKernel = executable["Sobel_uchar"];
        cl_kernel uchar4Kernel = executable["Sobel_uchar4"];
        cl_kernel advancedKernel = executable["Sobel_uchar16_to_float16_vload_16"];

        // allocate memory for input, output and reference 8-bit images
        // with some padding (to avoid boundaries checking) where needed
        // and provide necessary alignment to ensure zero-copy behaviour on Intel Processor Graphics
        cl_uint dev_alignment = zeroCopyPtrAlignment(oclobjects.device);
        size_t aligned_input_size = zeroCopySizeAlignment((width +XPAD*2) * (height+2*PAD_LINES), oclobjects.device);
        size_t output_size = width * height;
        size_t aligned_output_size = zeroCopySizeAlignment(output_size, oclobjects.device);

        printf("OpenCL data alignment is %d bytes.\n", dev_alignment);
        p_input = (cl_uchar*)aligned_malloc(aligned_input_size, dev_alignment);
        p_output = (cl_uchar*)aligned_malloc(aligned_output_size, dev_alignment);
        p_ref = (cl_uchar*)malloc(output_size);

        if(!(p_input && p_output && p_ref))
        {
            throw Error("Could not allocate buffers on the HOST!");
        }

        // create random input
        generateInput(p_input, width, height);

        // compute reference filtered image
        printf("Executing reference...\n");
        ExecuteSobelReference(p_input, p_ref, width, height);

        // create input buffer object
        cl_input_buffer =
            clCreateBuffer
            (
                oclobjects.context,
                CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
                aligned_input_size,
                p_input,
                &err
            );
        SAMPLE_CHECK_ERRORS(err);
        if (cl_input_buffer == (cl_mem)0)
            throw Error("Failed to create Input Buffer!");

        // create output buffer object
        cl_output_buffer =
            clCreateBuffer
            (
                oclobjects.context,
                CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR,
                aligned_output_size,
                p_output,
                &err
            );
        SAMPLE_CHECK_ERRORS(err);
        if (cl_output_buffer == (cl_mem)0)
            throw Error("Failed to create Output Buffer!");

        // ---Main part - test naive/optimized kernels and report execution times---
        // naive kernel
        size_t global_work_size[2] = { (size_t)width, (size_t)height };
        size_t outBufSize = (size_t)(width * height);

        double naive_time = ExecuteSobelKernel(cl_input_buffer, cl_output_buffer, p_output, outBufSize, global_work_size, NULL, oclobjects, naiveKernel);
        if(!verify(p_output, p_ref, width, height))
            throw Error("Naive kernel verification failed.");

        cout << fixed << setprecision(2);

        cout << "Naive kernel verification succeeded," << endl <<
            "run time is " << naive_time * 1000.0 << " ms." << endl;

        // Image is processed by 4 pixel chunks
        // Thread launching overhead reduced by ~4x
        // Bandwidth is better utilized by using uchar4 loads and stores
        global_work_size[0] = width/4;
        double time = ExecuteSobelKernel(cl_input_buffer, cl_output_buffer, p_output, outBufSize, global_work_size, NULL, oclobjects, uchar4Kernel);
        if(!verify(p_output, p_ref, width, height))
            throw Error("uchar4 kernel verification failed.");

        cout << "uchar4 kernel verification succeeded," << endl <<
            "run time is " << time * 1000.0 << " ms, speedup " << naive_time / time << endl;

        // Further increase chunks to 16x16 blocks and do all math in floats
        // Calculations are performed using 16-way vectors
        // Extra load operations saved by reusing data from the previous lines
        // Thread launching overhead reduced by ~256x
        // Convolution calculation is sped up by using FP operations
        global_work_size[0] = width/16;
        global_work_size[1] = height/16;
        time = ExecuteSobelKernel(cl_input_buffer, cl_output_buffer, p_output, outBufSize, global_work_size, NULL, oclobjects, advancedKernel);
        if(!verify(p_output, p_ref, width, height))
            throw Error("16x16 kernel verification failed.");

        cout << "16x16 kernel verification succeeded," << endl <<
            "run time is " << time * 1000.0 << " ms, speedup " << naive_time / time << endl;
        // -------------Main part end-----------------------------------------------
    }
    catch(const CmdParser::Error& error)
    {
        cerr
            << "[ ERROR ] In command line: " << error.what() << endl
            << "Run " << argv[0] << " -h for usage info.\n";
        ret = EXIT_FAILURE;
    }
    catch(const Error& error)
    {
        cerr << "[ ERROR ] Sample application specific error: " << error.what() << endl;
        ret = EXIT_FAILURE;
    }
    catch(const exception& error)
    {
        cerr << "[ ERROR ] " << error.what() << endl;
        ret = EXIT_FAILURE;
    }
    catch(...)
    {
        cerr << "[ ERROR ] Unknown/internal error happened." << endl;
        ret = EXIT_FAILURE;
    }

    if(cl_input_buffer)
    {
        err = clReleaseMemObject(cl_input_buffer);
        SAMPLE_CHECK_ERRORS(err);
    }

    if(cl_output_buffer)
    {
        err = clReleaseMemObject(cl_output_buffer);
        SAMPLE_CHECK_ERRORS(err);
    }

    free( p_ref );
    aligned_free( p_input );
    aligned_free( p_output );

    return ret;
}

