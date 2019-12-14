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

#include "basic.hpp"
#include "cmdparser.hpp"
#include "oclobject.hpp"

using namespace std;

#pragma warning( push )

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4355)    // 'this': used in base member initializer list
#endif

// All command-line options for the sample
class CmdParserSO : public CmdParserCommon
{
public:
    size_t                  global_size;
    CmdOption<size_t>       task_size;
    CmdOptionWorkGroupSize  local_size;
    CmdOption<bool>         relaxed_math;
    CmdOption<bool>         use_host_ptr;
    CmdOption<bool>         ocl_profiling;
    CmdOption<bool>         warming;
    CmdOption<bool>         vector_kernel;
    CmdOption<int>          iterations;
    CmdOptionErrors         max_error_count;

    CmdParserSO (int argc, const char** argv) :
        CmdParserCommon(argc, argv),
        task_size(*this,        's',"task-size","<integer>",            "Number of processed floats",16*1024*1024),
        local_size(*this),
        relaxed_math(*this,      0 ,"relaxed-math", "",                 "Enable -cl-fast-relaxed-math option for comilation",false),
        use_host_ptr(*this,     'u', "use-host-ptr", "",                "Host pointers/buffer-mapping enabled",false),
        ocl_profiling(*this,    'f',"ocl-profiling","",                 "Enable OpenCL event profiling ",false),
        warming(*this,          'w',"warming",      "",                 "Additional \"warming\" kernel run enabled (useful for small task sizes)",false),
        vector_kernel(*this,    'v',"vector-kernel","",                 "Enable \"gather4\" kernel version to process 4 floats by one workitem",false),
        iterations(*this,       'i',"internal-iterations","<integer>",  "Number of iterations in kernel",1000),
        max_error_count(*this)
    {
        global_size = 0;
    }
    virtual void parse ()
    {
        CmdParserCommon::parse();

        // calculate global size
        global_size = task_size.getValue();
        if(vector_kernel.getValue())
        {
            global_size /= 4;
        }

        //check local and global size
        if(local_size.getValue() && (global_size%local_size.getValue()))
        {
            printf("task-size = %lu\n", task_size.getValue());
            printf("global-size = %lu\n", global_size);
            printf("local-size = %lu\n", local_size.getValue());
            printf("global-size/local-size remainder is = %lu. This value must be 0\n", (global_size%local_size.getValue()));
            throw CmdParser::Error("Task or Local work size is incorect");
        }
    }
};

#ifdef _MSC_VER
#pragma warning (pop)
#endif

void ExecuteNative(cl_float* p_input, cl_float* p_ref, const CmdParserSO& cmd)
{
    printf("Executing reference...");
    for (size_t i = 0; i < cmd.task_size.getValue() ; ++i)
    {
        p_ref[i] = sinf(fabs(p_input[i]));
    }
    printf("Done\n\n");
}

void ExecuteKernel(
    cl_float* p_input,
    cl_float* p_output,
    OpenCLBasic& ocl,
    OpenCLProgramOneKernel& executable,
    CmdParserSO& cmd,
    float* p_time_device,
    float* p_time_host,
    float* p_time_read)
{
    double   perf_ndrange_start;
    double   perf_ndrange_stop;
    double   perf_read_start;
    double   perf_read_stop;
    cl_event        cl_perf_event = NULL;
    cl_int          err;



    // allocate buffers
    const cl_mem_flags  flag = cmd.use_host_ptr.getValue()?CL_MEM_USE_HOST_PTR: CL_MEM_COPY_HOST_PTR;
    size_t              size = sizeof(cl_float)*cmd.task_size.getValue();
    size_t              alignedSize = zeroCopySizeAlignment(size, ocl.device);

    cl_mem cl_input_buffer = clCreateBuffer(ocl.context, CL_MEM_READ_ONLY|flag, alignedSize, p_input, &err);
    SAMPLE_CHECK_ERRORS(err);
    if (cl_input_buffer == (cl_mem)0)
        throw Error("Failed to create Input Buffer!");
    cl_mem cl_output_buffer = clCreateBuffer(ocl.context, CL_MEM_WRITE_ONLY|flag, alignedSize, p_output, &err);
    SAMPLE_CHECK_ERRORS(err);
    if (cl_output_buffer == (cl_mem)0)
        throw Error("Failed to create Output Buffer!");

    size_t global_size =cmd.global_size;
    size_t local_size = cmd.local_size.getValue();

    // Set kernel arguments
    err = clSetKernelArg(executable.kernel, 0, sizeof(cl_mem), (void *) &cl_input_buffer);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 1, sizeof(cl_mem), (void *) &cl_output_buffer);
    SAMPLE_CHECK_ERRORS(err);

    printf("Global work size %lu\n", global_size);
    if(local_size)
    {
        printf("Local work size %lu\n", local_size);
    }
    else
    {
        printf("Run-time determines optimal local size\n\n");
    }

    {// get maximum workgroup size
        size_t local_size_max;
        err = clGetKernelWorkGroupInfo(executable.kernel, ocl.device, CL_KERNEL_WORK_GROUP_SIZE, sizeof(size_t), (void *)&local_size_max, NULL);
        SAMPLE_CHECK_ERRORS(err);
        printf("Maximum workgroup size for this kernel  %lu\n\n",local_size_max );
    }

    if(cmd.warming.getValue())
    {
        printf("Warming up OpenCL execution...");
        err= clEnqueueNDRangeKernel(ocl.queue, executable.kernel, 1, NULL, &global_size, local_size? &local_size:NULL, 0, NULL, NULL);
        SAMPLE_CHECK_ERRORS(err);
        err = clFinish(ocl.queue);
        SAMPLE_CHECK_ERRORS(err);
        printf("Done\n");
    }


    printf("Executing %s OpenCL kernel...",cmd.vector_kernel.getValue()?"vector":"scalar");
    perf_ndrange_start=time_stamp();
    // execute kernel, pls notice g_bAutoGroupSize
    err= clEnqueueNDRangeKernel(ocl.queue, executable.kernel, 1, NULL, &global_size, local_size? &local_size:NULL, 0, NULL, &cl_perf_event);
    SAMPLE_CHECK_ERRORS(err);
    err = clWaitForEvents(1, &cl_perf_event);
    SAMPLE_CHECK_ERRORS(err);
    perf_ndrange_stop=time_stamp();
    p_time_host[0] = (float)(perf_ndrange_stop - perf_ndrange_start);

    printf("Done\n");

    if(cmd.ocl_profiling.getValue())
    {
        cl_ulong start = 0;
        cl_ulong end = 0;

        // notice that pure HW execution time is END-START
        err = clGetEventProfilingInfo(cl_perf_event, CL_PROFILING_COMMAND_START, sizeof(cl_ulong), &start, NULL);
        SAMPLE_CHECK_ERRORS(err);
        err = clGetEventProfilingInfo(cl_perf_event, CL_PROFILING_COMMAND_END, sizeof(cl_ulong), &end, NULL);
        SAMPLE_CHECK_ERRORS(err);
        p_time_device[0] = (float)(end - start)*1e-9f;
    }

    if(cmd.use_host_ptr.getValue())
    {
        perf_read_start=time_stamp();
        void* tmp_ptr = clEnqueueMapBuffer(ocl.queue, cl_output_buffer, true, CL_MAP_READ, 0, size , 0, NULL, NULL, &err);
        SAMPLE_CHECK_ERRORS(err);
        if(tmp_ptr!=p_output)
        {// the pointer have to be same because CL_MEM_USE_HOST_PTR option was used in clCreateBuffer
            throw Error("clEnqueueMapBuffer failed to return original pointer");
        }
        err=clFinish(ocl.queue);
        SAMPLE_CHECK_ERRORS(err);
        perf_read_stop=time_stamp();

        err = clEnqueueUnmapMemObject(ocl.queue, cl_output_buffer, tmp_ptr, 0, NULL, NULL);
        SAMPLE_CHECK_ERRORS(err);
    }
    else
    {
        perf_read_start=time_stamp();
        err = clEnqueueReadBuffer(ocl.queue, cl_output_buffer, CL_TRUE, 0, size , p_output, 0, NULL, NULL);
        SAMPLE_CHECK_ERRORS(err);
        err=clFinish(ocl.queue);
        SAMPLE_CHECK_ERRORS(err);
        perf_read_stop=time_stamp();
    }
    p_time_read[0] = (float)(perf_read_stop - perf_read_start);



    err = clReleaseMemObject(cl_output_buffer);
    SAMPLE_CHECK_ERRORS(err);
    err = clReleaseMemObject(cl_input_buffer);
    SAMPLE_CHECK_ERRORS(err);
    err = clReleaseEvent(cl_perf_event);
    SAMPLE_CHECK_ERRORS(err);
}

// main execution routine - perform simple math on float vectors
int main (int argc, const char** argv)
{
    // pointer to the HOST buffers
    cl_float*   p_input = NULL;
    cl_float*   p_output = NULL;
    cl_float*   p_ref = NULL;
    //return code
    int         ret = EXIT_SUCCESS;
    try
    {
        // Define and parse command-line arguments.
        CmdParserSO cmd(argc, argv);
        cmd.parse();

        // Immediatly exit if user wanted to see the usage information only.
        if(cmd.help.isSet())
        {
            return EXIT_SUCCESS;
        }

        // Create the necessary OpenCL objects up to device queue.
        OpenCLBasic oclobjects(
            cmd.platform.getValue(),
            cmd.device_type.getValue(),
            cmd.device.getValue(),
            cmd.ocl_profiling.getValue()?CL_QUEUE_PROFILING_ENABLE: 0
        );


        // Build kernel
        string build_options;
        build_options += "-D ITER_NUM="+to_str(cmd.iterations.getValue());
        if(cmd.relaxed_math.getValue())
            build_options += " -cl-fast-relaxed-math";

        OpenCLProgramOneKernel executable(
            oclobjects,
            L"SimpleOptimizations.cl",
            "",
            cmd.vector_kernel.getValue()?"SimpleKernel4":"SimpleKernel",
            build_options);

        // allocate buffers
        cl_uint     dev_alignment = zeroCopyPtrAlignment(oclobjects.device);
        size_t      size = sizeof(cl_float) * cmd.task_size.getValue();
        size_t      alignedSize = zeroCopySizeAlignment(size, oclobjects.device);
        p_input = (cl_float*)aligned_malloc(alignedSize, dev_alignment);
        p_output = (cl_float*)aligned_malloc(alignedSize, dev_alignment);
        p_ref = (cl_float*)aligned_malloc(alignedSize, dev_alignment);

        if(!(p_input && p_output && p_ref))
        {
            throw Error("Could not allocate buffers on the HOST!");
        }

        // set input array to random legal values
        srand(2011);
        for (size_t i = 0; i < cmd.task_size.getValue() ; i++)
        {
            p_input[i] = rand_uniform_01<cl_float>()*512.0f - 256.0f;
        }

        // do simple math
        float ocl_time_device = 0;
        float ocl_time_host = 0;
        float ocl_time_read = 0;

        ExecuteKernel(
            p_input,p_output,
            oclobjects,executable,
            cmd,
            &ocl_time_device,
            &ocl_time_host,
            &ocl_time_read);

        ExecuteNative(p_input,p_ref,cmd);


        printf("NDRange perf. counter time %f ms.\n", 1000.0f*ocl_time_host);

        if(cmd.ocl_profiling.getValue())
        {
            printf("NDRange event profiling time %f ms.\n", 1000.0f*ocl_time_device);
        }

        printf("%s buffer perf. counter time %f ms.\n\n", cmd.use_host_ptr.getValue()?"Map":"Read", 1000.0f*ocl_time_read);


        // Do verification
        printf("Performing verification...\n");
        int     error_count = 0;
        for(size_t i = 0; i < cmd.task_size.getValue() ; i++)
        {
            // Compare the data
            if( fabsf(p_output[i] - p_ref[i]) > 0.01f )
            {
                printf("Error at location %d,  outputArray = %f, refArray = %f \n", i, p_output[i], p_ref[i]);
                error_count++;
                ret = EXIT_FAILURE;
                if(cmd.max_error_count.getValue()>0 && error_count >= cmd.max_error_count.getValue())
                {
                    break;
                }
            }
        }
        printf("%s", (error_count>0)?"ERROR: Verification failed.\n":"Verification succeeded.\n");
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
