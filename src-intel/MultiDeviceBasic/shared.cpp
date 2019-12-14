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
#include <iomanip>
#include <string>
#include <cassert>

#include <CL/cl.h>

#include "basic.hpp"
#include "cmdparser.hpp"
#include "oclobject.hpp"
#include "multidevice.hpp"


using namespace std;


void shared_context_scenario (
    cl_platform_id platform,
    cl_device_type device_type,
    size_t work_size
)
{
    //
    // To collect all devices of a specified type inside a single context,
    // consider the following methods:
    //    - Call clGetDeviceIDs, which lists the available devices. Call
    //      clCreateContext to create a context for the available devices.
    //    - Call clCreateContextFromType directly for platform and device
    //      type. Call clGetContextInfo to query the available devices.
    // Querying the list of devices is necessary in both methods as you
    // need to create a separate command queue for each device. OpenCL
    // standard does not support an API for creating an array of command
    // queues to simplify the process. This sample utilizes the method with
    // calling clCreateContextFromType.
    //

    cl_int err = 0;

    // First, create context.

    // Context properties select needed platform:
    cl_context_properties context_props[] = {
        CL_CONTEXT_PLATFORM,
        cl_context_properties(platform),
        0
    };

    // Now create a context with all devices of a given type available
    // for the selected platform.
    cl_context context = clCreateContextFromType(context_props, device_type, 0, 0, &err);
    SAMPLE_CHECK_ERRORS(err);

    cout << "Context was created successfully." << endl;

    // Create program with a simple kernel.
    cl_program program = create_program(context);

    cout << "Program was created successfully." << endl;

    // Build program once for all devices in the context.
    err = clBuildProgram(program, 0, 0, "", 0, 0);
    SAMPLE_CHECK_ERRORS(err);
    // Here one may need to look into build log in case of err != CL_SUCCESS,
    // but we skip this step for simplicity.

    cout << "Program was built successfully." << endl;

    cl_kernel kernel = clCreateKernel(program, "simple", &err);
    SAMPLE_CHECK_ERRORS(err);

    // Now you need to create command queues: one queue per each device in
    // the context. To do this, first, query the number of the used devices:

    cl_uint number_of_devices;
    err = clGetContextInfo(
        context,
        CL_CONTEXT_NUM_DEVICES,
        sizeof(number_of_devices),
        &number_of_devices,
        0
    );
    SAMPLE_CHECK_ERRORS(err);

    cout << "Number of devices in the context: " << number_of_devices << "." << endl;

    // Get the list of devices.
    vector<cl_device_id> devices(number_of_devices);
    err = clGetContextInfo(
        context,
        CL_CONTEXT_DEVICES,
        number_of_devices * sizeof(cl_device_id),
        &devices[0],
        0
    );
    SAMPLE_CHECK_ERRORS(err);

    // Create command queues for all of the devices.

    vector<cl_command_queue> queues(number_of_devices);

    for(cl_uint i = 0; i < number_of_devices; ++i)
    {
        queues[i] = clCreateCommandQueue(context, devices[i], 0, &err);
        SAMPLE_CHECK_ERRORS(err);
        cout << "Successfully created command queue for device " << i << "." << endl;
    }

    // Now you need to create and populate buffers with some
    // initial data. Then it will be split among all devices in the context
    // by means of sub-buffers and processed in parallel.

    // Create host memory areas. We are going to use CL_MEM_USE_HOST_PTR,
    // need to pay attension on memory alignment to avoid unnecessary copying.
    // To do this you need to iterate over all devices and get alignment
    // requirement for each of them.

    // Intel specific: from the beginning alignment is set to recommended value to enable
    // zero-copy behaviour on Intel Processor Graphics
    size_t alignment = 4096;

    for(cl_uint i = 0; i < number_of_devices; ++i)
    {
        cl_uint device_alignment_in_bits = 1;

        err = clGetDeviceInfo(
            devices[i],
            CL_DEVICE_MEM_BASE_ADDR_ALIGN,
            sizeof(device_alignment_in_bits),
            &device_alignment_in_bits,
            0
        );

        // Supposing that alignment can be power of 2 only,
        // we get maximum of all values for all devices to satisfy all of them.
        alignment = max(alignment, size_t(device_alignment_in_bits/8));
    }

    cout
        << "Detected minimal alignment requirement suitable for all devices: "
        << alignment << " bytes." << endl;

    size_t buffer_size = sizeof(float)*work_size;

    cout
        << "Required memory amount for each buffer: "
        << buffer_size << " bytes.\n";

    // Align size of a buffer to enable zero-copy behaviour on Intel Processor Graphics
    size_t aligned_buffer_size = buffer_size + (~buffer_size + 1) % 64;

    float
        *a_host = (float*)aligned_malloc(buffer_size, alignment),
        *b_host = (float*)aligned_malloc(buffer_size, alignment),
        *c_host = (float*)aligned_malloc(buffer_size, alignment)
    ;

    // Initializing a and b buffers with synthetic values.

    for(size_t i = 0; i < work_size; ++i)
    {
        float init_value = static_cast<float>(i);
        a_host[i] = init_value;
        b_host[i] = 2*init_value;
    }


    // Create buffers.

    // In shared-context scenario all buffers are shared between all devices.
    // Though to use the same buffer by multiple devices simultaneously
    // you need to create sub-buffer per each device. The advantage of
    // shared-context in comparison to multi-context is that you can
    // organize efficient load balancing among devices by dynamically choosing
    // sub-buffer size without recreation of original buffers; costs of
    // buffer creation is higher than costs of sub-buffer creation, so
    // this approach can be efficiently implemented with shared-context only.

    cl_mem a_buffer = clCreateBuffer(
        context,
        CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
        aligned_buffer_size,
        a_host,
        &err
    );
    SAMPLE_CHECK_ERRORS(err);

    cl_mem b_buffer = clCreateBuffer(
        context,
        CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
        aligned_buffer_size,
        b_host,
        &err
    );
    SAMPLE_CHECK_ERRORS(err);

    cl_mem c_buffer = clCreateBuffer(
        context,
        CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR,
        aligned_buffer_size,
        c_host,
        &err
    );
    SAMPLE_CHECK_ERRORS(err);

    cout << "Buffers were created successfully." << endl;

    // For each device, create sub-buffers to split work (almost) equally.
    // Here we use trivial math for work partitioning that is
    // suitable for big work_size values vs. number_of_devices.
    // We don't use dynamic load balancing between devices, dividing
    // work statically.

    // In your application, if there is no possibility to divide data with
    // small granularity you need to follow a bit different (but a simple one
    // also) math to distribute the last piece of work among several devices
    // for better load balancing.

    // Note, in calculations we operate with bytes instead of work items.

    // First define unaligned sub-buffer size
    size_t normal_piece_size = buffer_size / number_of_devices;

    // Then to be able touching buffers simultaniously from several devices
    // we force each sub-buffer to be properly aligned by modifying the size
    normal_piece_size = (normal_piece_size / alignment) * alignment;

    if(normal_piece_size == 0)
    {
        throw Error(
            "Not enough work items to load all devices with our "
            "simple partitioning schema."
        );
    }

    // Last, calculate the rest of work as an addition to work for the last device.
    size_t additional_piece_size = buffer_size - normal_piece_size*number_of_devices;

    // OK, preparation is done, let's go and create all sub-buffers

    vector<cl_mem> a_sub_buffers(number_of_devices);
    vector<cl_mem> b_sub_buffers(number_of_devices);
    vector<cl_mem> c_sub_buffers(number_of_devices);

    for(cl_uint i = 0; i < number_of_devices; ++i)
    {
        // Calculate the number of bytes in one buffer dedicated for i-th device.
        size_t piece_size =
            (i == number_of_devices-1) ?
            normal_piece_size + additional_piece_size : // for the last device
            normal_piece_size;  // for all devices except the last one

        cl_buffer_region region = { i*normal_piece_size, piece_size };

        // Please note that sub-buffer creation for simultaneously using the same
        // buffer by multiple devices are strongly needed (by the Spec) for those
        // buffers which are written by kernels (buffer c in this sampel). Here
        // we create sub-buffers for input buffers (read only by kernel) too to
        // simplify addressing in the kernel and make it the same for all buffers.

        a_sub_buffers[i] = clCreateSubBuffer(
            a_buffer,
            0,
            CL_BUFFER_CREATE_TYPE_REGION,
            &region,
            &err
        );
        SAMPLE_CHECK_ERRORS(err);

        b_sub_buffers[i] = clCreateSubBuffer(
            b_buffer,
            0,
            CL_BUFFER_CREATE_TYPE_REGION,
            &region,
            &err
        );
        SAMPLE_CHECK_ERRORS(err);

        c_sub_buffers[i] = clCreateSubBuffer(
            c_buffer,
            0,
            CL_BUFFER_CREATE_TYPE_REGION,
            &region,
            &err
        );
        SAMPLE_CHECK_ERRORS(err);

        cout << "Sub-buffers for device " << i << " were created successfully." << endl;
    }

    vector<cl_event> events(number_of_devices);

    // Setup kernel arguments and enqueue kernel for each device.

    for(cl_uint i = 0; i < number_of_devices; ++i)
    {
        err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &a_sub_buffers[i]);
        SAMPLE_CHECK_ERRORS(err);

        err = clSetKernelArg(kernel, 1, sizeof(cl_mem), &b_sub_buffers[i]);
        SAMPLE_CHECK_ERRORS(err);

        err = clSetKernelArg(kernel, 2, sizeof(cl_mem), &c_sub_buffers[i]);
        SAMPLE_CHECK_ERRORS(err);

        // Defining global_size by sub-buffer size for i-th device
        size_t global_size =
            (i == number_of_devices-1) ?
            (normal_piece_size + additional_piece_size)/sizeof(float) : // for the last device
            normal_piece_size/sizeof(float);  // for all devices except the last one

        err = clEnqueueNDRangeKernel(queues[i], kernel, 1, 0, &global_size, 0, 0, 0, &events[i]);
        SAMPLE_CHECK_ERRORS(err);

        cout << "Kernel for device " << i << " was enqueued successfully." << endl;

        // It is important not to have clFinish or clWaitForEvents here for
        // enqueued commands. Otherwise we serialize execution among the devices.
        // clFinish will be called for all queues in the next loop.

        // But, to ensure that multiple devices work simultaniously
        // you need to flush command queue to submit work to target device.

        err = clFlush(queues[i]);
        SAMPLE_CHECK_ERRORS(err);

        // If you don't do that, it may happen the device won't start
        // working until final clFinish or other barrier-like command is called later,
        // and this will also results in execution serialization.

        // Notice that if you use events for commands syncronization between devices,
        // it is also necessary to call clFlush after enqueuing such commands to be
        // able to wait on them according to the OpenCL specification.
    }

    // Wait untill all devices finish their work.
    err = clWaitForEvents(number_of_devices, &events[0]);
    SAMPLE_CHECK_ERRORS(err);

    // Read the resulting values back to host as entire c_buffer
    // right after all devices finish their work.

    // Here there are several options on how to transfer the resulting
    // data back to host:
    //      1. Mapping of entire c_buffer in one of the queues -- doesn't matter
    //         which particular queue is used (we follow this method here scheduling it
    //         in the 0-th queue).
    //      2. Mapping each of sub-buffers in its device queue (similar to multi-context
    //         scenario, except that here you use sub-buffers instead of buffers in multi-
    //         context scenario). This approach isn't implemented here.
    //

    void* ptr = clEnqueueMapBuffer(
        queues[0],  // it doesn't matter which queue is used here
        c_buffer,
        CL_TRUE,
        CL_MAP_READ,    // it is imprortant to use minimal required access for mapping
        0, buffer_size,
        0, 0,   // you have already waited in clWaitForEvents, don't need to sync here
        0,
        &err
    );
    SAMPLE_CHECK_ERRORS(err);

    // In real application here you will evaluate the resulting values
    // referenced by ptr pointer, but in our sample we don't need it
    // and just skip to the unmapping the region.

    err = clEnqueueUnmapMemObject(queues[0], c_buffer, ptr, 0, 0, 0);
    SAMPLE_CHECK_ERRORS(err);
    err = clFinish(queues[0]);
    SAMPLE_CHECK_ERRORS(err);

    cout << "All devices finished execution." << endl;

    // OK, the shared context scenario is over, release all OpenCL
    // resources and quit.

    for(cl_uint i = 0; i < number_of_devices; ++i)
    {
        err = clReleaseMemObject(a_sub_buffers[i]);
        SAMPLE_CHECK_ERRORS(err);

        err = clReleaseMemObject(b_sub_buffers[i]);
        SAMPLE_CHECK_ERRORS(err);

        err = clReleaseMemObject(c_sub_buffers[i]);
        SAMPLE_CHECK_ERRORS(err);

        clReleaseEvent(events[i]);
    }

    err = clReleaseMemObject(a_buffer);
    SAMPLE_CHECK_ERRORS(err);
    err = clReleaseMemObject(b_buffer);
    SAMPLE_CHECK_ERRORS(err);
    err = clReleaseMemObject(c_buffer);
    SAMPLE_CHECK_ERRORS(err);

    aligned_free(a_host);
    aligned_free(b_host);
    aligned_free(c_host);

    for(cl_uint i = 0; i < number_of_devices; ++i)
    {
        err = clReleaseCommandQueue(queues[i]);
        SAMPLE_CHECK_ERRORS(err);
    }

    err = clReleaseKernel(kernel);
    SAMPLE_CHECK_ERRORS(err);
    err = clReleaseProgram(program);
    SAMPLE_CHECK_ERRORS(err);
    err = clReleaseContext(context);
    SAMPLE_CHECK_ERRORS(err);
}
