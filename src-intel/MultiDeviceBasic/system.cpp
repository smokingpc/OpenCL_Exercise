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

#include <iostream>

#include <CL/cl.h>

#include "basic.hpp"
#include "multidevice.hpp"

using namespace std;


void system_level_scenario (
    cl_platform_id platform,
    cl_device_type device_type,
    size_t work_size,
    int instance_count,
    int instance_index
)
{
    // In this scenario multi-device parallelism are implemented out of
    // host application -- on system level. This application should be run
    // multiple times simultaneously in the same system.

    // The idea behind this scenario: if you already have an OpenCL-enabled
    // application with ability to partition work between multiple instances,
    // for example, through MPI, then you don't need to modify this application
    // to use multiple devices. Just run one instance per each device.

    // It should be extremely useful if you have an MPI application for a cluster
    // and want to utilize multi-card Xeon Phi machine -- in this case you
    // will not need to do any adjustments in work partitioning because
    // all cards have the same compute power and simple approach used here
    // (with evenly devided work among all devices) should work well.

    // For this sample scenario, user should limit number of devices for each
    // application instance externally. It can be:
    //     - setting -t command line option with different values depending on
    //       application instance, so different application instances will use
    //       different types of devices (e.g. CPU+ACC, CPU+GPU);
    //     - environment variable OFFLOAD_DEVICES (Xeon Phi device only) in
    //       multi-card Xeon Phi environment;
    //     - the combination of two above (e.g. CPU + several accelerators).

    // Here we just pick the first device available of specified type and
    // run regular non-multi-device scenario on it. The only thing that
    // reminds that we still in multi-device environment is data partitioning --
    // you need to pick dedicated part of the whole work only.

    cl_int err = 0;
    cl_device_id device = 0;

    err = clGetDeviceIDs(
        platform,
        device_type,
        1,
        &device,
        0
    );

    // If there is no device of a given type, the next statement will throw
    // an exception:
    SAMPLE_CHECK_ERRORS(err);

    size_t buffer_size = sizeof(float)*work_size;

    cout
        << "Required memory amount for each buffer: "
        << buffer_size << " bytes." << endl;

    // As we are going to use CL_MEM_USE_HOST_PTR, need to pay attension
    // on memory alignment to avoid unnecessary copying.

    // This alignment value will be used for host memory allocation only.
    // And in contrast it will not be used for restricting granularity of
    // work partitioning among devices because here you cannot know
    // about alignment for other devices.

    size_t alignment = 1;
    err = clGetDeviceInfo(
        device,
        CL_DEVICE_MEM_BASE_ADDR_ALIGN,
        sizeof(alignment),
        &alignment,
        0
    );

    alignment /= 8; // in bytes

    // To enable zero-copy behaviour on Intel Processor Graphics,
    // additional alignement rules should be held
    if(alignment < 4096)
    {
        alignment = 4096;
    }

    cout
        << "Detected alignment requirement: "
        << alignment << " bytes." << endl;

    // For each device, create buffers to split work (almost) equally.
    // All buffers are created with CL_MEM_USE_HOST_PTR and use
    // commot host area defined below as a_host, b_host and c_host.

    // Here we use trivial math for work partitioning that is
    // suitable for big work_size values vs. number_of_devices.
    // If there is no possibility to divide data with small granularity
    // you need to follow a bit different (but a simple one also)
    // math to distribute the last piece of work among several devices
    // for better load balance.

    // In math we operate with bytes instead of work items.

    // First define unaligned buffer size for each device.
    size_t normal_piece_size = buffer_size / instance_count;

    // Then, we apply predefined granularity for all devices.
    // The value of granularity must be device independent in our case,
    // because you cannot observe all devices and their capabilities
    // from within this application instance and should rely
    // on some globally and uniquely defined value. Here we choose
    // sizeof(cl_float16) as this value.
    // As opposed to this code, in a real application, it is possible
    // to use communications between application instances to negotiate
    // appropriate granularity if it is needed.

    size_t granularity = 4096;  // page granularity satisfies all the requirements

    normal_piece_size = (normal_piece_size / granularity) * granularity;

    if(normal_piece_size == 0)
    {
        throw Error(
            "Not enough work items to load all devices with our "
            "simple partitioning schema and choosen granularity."
        );
    }

    // Last, calculate the rest of work as an addition to work for the last device.
    size_t additional_piece_size = buffer_size - normal_piece_size*instance_count;

    // Calculate the number of bytes in one buffer dedicated for i-th device.
    size_t piece_size = normal_piece_size;  // for all devices except the last one

    // for the last device, adjustment is needed since work may not be dividable evenly
    if(instance_index == instance_count-1)
    {
        // Add non dividable remainder to this last piece of work
        // Note: there are more convinient ways to distribute remainder work;
        // this one is the easiest for illustrative implementation here
        piece_size += additional_piece_size;

        // For this last piece, it is necessary to ensure alignment of buffer size
        // to enable zero-copy behaviour on Intel Processor Graphics
        piece_size = piece_size + (~piece_size + 1) % 64;
    }

    float
        *a_host = (float*)aligned_malloc(piece_size, alignment),
        *b_host = (float*)aligned_malloc(piece_size, alignment),
        *c_host = (float*)aligned_malloc(piece_size, alignment)
    ;

    // Initializing a and b buffers with synthetic values.
    // Please note that you need to initialize only a part
    // of data that is dedicated for processing by the current
    // device (application instance). So we are doing
    // necessary adjustments.

    cout << "Initializing input buffers...";

    size_t first = (normal_piece_size/sizeof(float))*instance_index;

    for(size_t i = 0; i < piece_size/4; ++i)
    {
        float init_value = static_cast<float>(i + first);
        a_host[i] = init_value;
        b_host[i] = 2*init_value;
    }

    cout << "done." << endl;

    // Now create context with a single device

    // Form common context properties which just select needed platform:
    cl_context_properties context_props[] = {
        CL_CONTEXT_PLATFORM,
        cl_context_properties(platform),
        0
    };

    cl_context context = clCreateContext(
        context_props,
        1,
        &device,
        0,
        0,
        &err
    );
    SAMPLE_CHECK_ERRORS(err);

    cout << "Context was created successfully." << endl;

    // Create program with a simple kernel
    cl_program program = create_program(context);

    cout << "Program was created successfully." << endl;

    // Build program for (single) device in the context.
    err = clBuildProgram(program, 0, 0, "", 0, 0);
    SAMPLE_CHECK_ERRORS(err);
    // Here one may need to look into build log in case of err != CL_SUCCESS,
    // but we skip all this stuff for the sake of simplicity.

    cout << "Program was built successfully." << endl;

    cl_kernel kernel = clCreateKernel(program, "simple", &err);
    SAMPLE_CHECK_ERRORS(err);

    // Now you need to create command queue.

    cl_command_queue queue = clCreateCommandQueue(context, device, 0, &err);
    SAMPLE_CHECK_ERRORS(err);
    cout << "Successfully created command queue." << endl;

    // Create buffers

    cl_mem a_buffer = clCreateBuffer(
        context,
        CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
        piece_size,
        a_host,
        &err
    );
    SAMPLE_CHECK_ERRORS(err);

    cl_mem b_buffer = clCreateBuffer(
        context,
        CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
        piece_size,
        b_host,
        &err
    );
    SAMPLE_CHECK_ERRORS(err);

    cl_mem c_buffer = clCreateBuffer(
        context,
        CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR,
        piece_size,
        c_host,
        &err
    );
    SAMPLE_CHECK_ERRORS(err);

    cout << "Buffers were created successfully.\n" << endl;

    // Setup kernel arguments and enqueue kernel for each device.

    err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &a_buffer);
    SAMPLE_CHECK_ERRORS(err);

    err = clSetKernelArg(kernel, 1, sizeof(cl_mem), &b_buffer);
    SAMPLE_CHECK_ERRORS(err);

    err = clSetKernelArg(kernel, 2, sizeof(cl_mem), &c_buffer);
    SAMPLE_CHECK_ERRORS(err);

    size_t global_size = piece_size/sizeof(float);

    // Enqueue kernel
    err = clEnqueueNDRangeKernel(queue, kernel, 1, 0, &global_size, 0, 0, 0, 0);
    SAMPLE_CHECK_ERRORS(err);

    // And immediately enqueue reading/mapping the resulting values
    // back to host.
    clEnqueueMapBuffer(
        queue,
        c_buffer,
        false,
        CL_MAP_READ,
        0, global_size*sizeof(float),
        0, 0, 0,
        &err
    );

    cout
        << "Kernel and map commands were enqueued successfully." << endl;

    err = clFinish(queue);
    SAMPLE_CHECK_ERRORS(err);

    cout << "Execution completed." << endl;

    // In real application here you will evaluate the resulting values
    // referenced by c_host, because all devices writes/maps their results
    // to corresponding parts of c_host memory area at this point.
    // But in our sample we don't actually use the resulting values and
    // just skip to the unmapping the regions.

    err = clEnqueueUnmapMemObject(queue, c_buffer, c_host, 0, 0, 0);
    SAMPLE_CHECK_ERRORS(err);

    err = clFinish(queue);
    SAMPLE_CHECK_ERRORS(err);

    err = clReleaseMemObject(a_buffer);
    SAMPLE_CHECK_ERRORS(err);
    err = clReleaseMemObject(b_buffer);
    SAMPLE_CHECK_ERRORS(err);
    err = clReleaseMemObject(c_buffer);
    SAMPLE_CHECK_ERRORS(err);

    err = clReleaseCommandQueue(queue);
    SAMPLE_CHECK_ERRORS(err);

    err = clReleaseKernel(kernel);
    SAMPLE_CHECK_ERRORS(err);
    err = clReleaseProgram(program);
    SAMPLE_CHECK_ERRORS(err);
    err = clReleaseContext(context);
    SAMPLE_CHECK_ERRORS(err);

    aligned_free(a_host);
    aligned_free(b_host);
    aligned_free(c_host);
}
