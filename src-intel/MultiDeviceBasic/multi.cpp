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
#include "oclobject.hpp"
#include "multidevice.hpp"


using namespace std;


void multi_context_scenario (
    cl_platform_id platform,
    cl_device_type device_type,
    size_t work_size
)
{
    // In the multi-context scenario, one application instance uses
    // all devices. Each device has its own context, so all resources:
    // programs, kernels, and buffers are not shared, and should be created
    // individually. You can share host buffer only.

    cl_int err = 0;

    // First, get device list of the required type.

    cl_uint number_of_devices;
    err = clGetDeviceIDs(
        platform,
        device_type,
        0,
        0,
        &number_of_devices
    );
    SAMPLE_CHECK_ERRORS(err);

    cout << "Number of devices of required type: " << number_of_devices << "." << endl;

    // Now get the list of devices.
    vector<cl_device_id> devices(number_of_devices);
    err = clGetDeviceIDs(
        platform,
        device_type,
        number_of_devices,
        &devices[0],
        0
    );
    SAMPLE_CHECK_ERRORS(err);

    // Form common context properties which just select needed platform:
    cl_context_properties context_props[] = {
        CL_CONTEXT_PLATFORM,
        cl_context_properties(platform),
        0
    };

    // Create common host memory regions for all devices.
    // Each device uses its own piece of this region with
    // its own buffer, created on a particular part of this region
    // with CL_MEM_USE_HOST_PTR.

    // As you use CL_MEM_USE_HOST_PTR, pay attension
    // to memory alignment to avoid unnecessary copying.
    // To do this you need to iterate over all devices and get alignment
    // requirement for each of them.

    // Intel specific: alignment is set to recommended value to enable
    // zero-copy behaviour on Intel Processor Graphics.
    // This alignement will be used to allocate memory to be used in clCreateBuffer call
    // with CL_MEM_USE_HOST_PTR flag.
    // See the zero-copy tutorial or OpenCL optimization guide
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

        // Supposing that alignment can be power of two only,
        // you get maximum of all values for all devices to meet all the requirements.
        alignment = max(alignment, size_t(device_alignment_in_bits/8));
    }

    cout
        << "Detected minimal alignment requirement suitable for all devices: "
        << alignment << " bytes." << endl;

    size_t buffer_size = sizeof(float)*work_size;

    cout
        << "Required memory amount for each buffer: "
        << buffer_size << " bytes." << endl;

    // Align size of a buffer to enable zero-copy behaviour on Intel Processor Graphics
    size_t aligned_buffer_size = buffer_size + (~buffer_size + 1) % 64;

    float
        *a_host = (float*)aligned_malloc(aligned_buffer_size, alignment),
        *b_host = (float*)aligned_malloc(aligned_buffer_size, alignment),
        *c_host = (float*)aligned_malloc(aligned_buffer_size, alignment)
    ;

    // Initializing a and b buffers with synthetic values.

    for(size_t i = 0; i < work_size; ++i)
    {
        float init_value = static_cast<float>(i);
        a_host[i] = init_value;
        b_host[i] = 2*init_value;
    }


    // Create buffers for each device to split work (almost) equally.
    // All buffers are created with CL_MEM_USE_HOST_PTR and use
    // commot host area defined below as a_host, b_host and c_host.

    // Trivial math is used for work partitioning, which is
    // suitable for big work_size values vs. number_of_devices.
    // If you cannot divide data with small granularity,
    // follow a bit different (but a simple one also)
    // math to distribute the last piece of work among several devices
    // for better load balance.

    // In math we operate with bytes instead of work-items.

    // First define unaligned buffer size for each device.
    size_t normal_piece_size = buffer_size / number_of_devices;

    // Then, following the Optimization Guide to avoid unnecessary copying,
    // align each piece to be able to use it as a host pointer in
    // clCreateBuffer with CL_MEM_USE_HOST_PTR.
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

    // Next steps are individual for all devices in the collection.

    // So, the divergence starts already now, from the context creation in contrast to
    // the shared-context scenario, where more resources are shared among devices.
    // Due to this early separation, the multi-context scenario has less flexibility,
    // particularly in load balancing, and lack of tight synchronization between command
    // queues and needs higher host participation in inter-device scheduling.

    vector<cl_context> contexts(number_of_devices);
    vector<cl_program> programs(number_of_devices);
    vector<cl_kernel> kernels(number_of_devices);
    vector<cl_command_queue> queues(number_of_devices);

    vector<cl_mem> a_buffers(number_of_devices);
    vector<cl_mem> b_buffers(number_of_devices);
    vector<cl_mem> c_buffers(number_of_devices);

    cout << endl;

    for(cl_uint i = 0; i < number_of_devices; ++i)
    {
        cout << "Preparation context for device " << i << ":" << endl;

        // Now create context with all devices of a given type available for the selected platform
        contexts[i] = clCreateContext(
            context_props,
            1,
            &devices[i],
            0,
            0,
            &err
        );
        SAMPLE_CHECK_ERRORS(err);

        cout << "Context was created successfully." << endl;

        // Create program with a simple kernel
        programs[i] = create_program(contexts[i]);

        cout << "Program was created successfully." << endl;

        // Build program for (single) device in the context.
        err = clBuildProgram(programs[i], 0, 0, "", 0, 0);
        SAMPLE_CHECK_ERRORS(err);
        // Here one may need to look into build log in case of err != CL_SUCCESS,
        // but we skip all this stuff for the sake of simplicity.

        cout << "Program was built successfully." << endl;

        kernels[i] = clCreateKernel(programs[i], "simple", &err);
        SAMPLE_CHECK_ERRORS(err);

        // Now you need to create a command queue.

        queues[i] = clCreateCommandQueue(contexts[i], devices[i], 0, &err);
        SAMPLE_CHECK_ERRORS(err);
        cout << "Successfully created command queue." << endl;

        // Calculate the number of bytes in one buffer dedicated for i-th device.
        size_t piece_size = normal_piece_size;  // for all devices except the last one

        // for the last device, adjustment is needed since work may not be dividable evenly
        if(i == number_of_devices-1)
        {
            // Add non dividable remainder to this last piece of work
            // Note: there are more convinient ways to distribute remainder work;
            // this one is the easiest for illustrative implementation here
            piece_size += additional_piece_size;

            // For this last piece, it is necessary to ensure alignment of buffer size
            // to enable zero-copy behaviour on Intel Processor Graphics
            piece_size = piece_size + (~piece_size + 1) % 64;
        }

        // Create buffers

        a_buffers[i] = clCreateBuffer(
            contexts[i],
            CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
            piece_size,
            (char*)a_host + i*normal_piece_size,
            &err
        );
        SAMPLE_CHECK_ERRORS(err);

        b_buffers[i] = clCreateBuffer(
            contexts[i],
            CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
            piece_size,
            (char*)b_host + i*normal_piece_size,
            &err
        );
        SAMPLE_CHECK_ERRORS(err);

        c_buffers[i] = clCreateBuffer(
            contexts[i],
            CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR,
            piece_size,
            (char*)c_host + i*normal_piece_size,
            &err
        );
        SAMPLE_CHECK_ERRORS(err);

        cout << "Buffers were created successfully.\n" << endl;
    }

    // Setup kernel arguments and enqueue kernel for each device.

    // You don't neccessary need a break of the loop over devices here
    // but in this sample the preparation work and enqueueing of commands
    // to queues are split for education purposes: you may want to measure
    // time for initialization part and kernel execution/data transfer part.

    // You store pointers returned by clEnqueueMapBuffer for c_host to
    // this vector to be able to do unmapping easier. It is not necessary,
    // because you always can restore pointers by device index and c_host
    // pointer, but it needs doing the same math with pointers which
    // you did when created buffers. To avoid it, store
    // them in this vector.
    vector<void*> mapped_c_ptrs(number_of_devices);

    for(cl_uint i = 0; i < number_of_devices; ++i)
    {
        err = clSetKernelArg(kernels[i], 0, sizeof(cl_mem), &a_buffers[i]);
        SAMPLE_CHECK_ERRORS(err);

        err = clSetKernelArg(kernels[i], 1, sizeof(cl_mem), &b_buffers[i]);
        SAMPLE_CHECK_ERRORS(err);

        err = clSetKernelArg(kernels[i], 2, sizeof(cl_mem), &c_buffers[i]);
        SAMPLE_CHECK_ERRORS(err);

        size_t global_size =
            (i == number_of_devices-1) ?
            (normal_piece_size + additional_piece_size)/sizeof(float) : // for the last device
            normal_piece_size/sizeof(float);  // for all devices except the last one

        // Enqueue kernel
        err = clEnqueueNDRangeKernel(queues[i], kernels[i], 1, 0, &global_size, 0, 0, 0, 0);
        SAMPLE_CHECK_ERRORS(err);

        // And immediately enqueue reading/mapping the resulting values
        // back to host.
        mapped_c_ptrs[i] = clEnqueueMapBuffer(
            queues[i],
            c_buffers[i],
            false,
            CL_MAP_READ,
            0, global_size*sizeof(float),
            0, 0, 0,
            &err
        );
        SAMPLE_CHECK_ERRORS(err);

        cout
            << "Kernel and map commands for device " << i
            << " were enqueued successfully." << endl;

        // It is important not to have clFinish or clWaitForEvents here for
        // enqueued commands. Otherwise we serialize execution among the devices.
        // clFinish is called for all queues in the next loop.

        // To ensure that multiple devices work simultaniously
        // you need to flush command queue to submit work to target device.

        clFlush(queues[i]);

        // Otherwise the device might not start working until clFinish
        // is called in the next loop for dedicated queue, which
        // also results in execution serialization.
    }

    // All-device-wide barrier:
    for(cl_uint i = 0; i < number_of_devices; ++i)
    {
        err = clFinish(queues[i]);
        SAMPLE_CHECK_ERRORS(err);
    }

    cout << "All devices finished execution." << endl;

    // In a real application here you evaluate the resulting values
    // referenced by c_host, because all devices write/map their results
    // to corresponding parts of the c_host memory area at this point.
    // But this sample does not actually use the resulting values, and
    // just skips to the unmapping the regions.

    for(cl_uint i = 0; i < number_of_devices; ++i)
    {
        err = clEnqueueUnmapMemObject(queues[i], c_buffers[i], mapped_c_ptrs[i], 0, 0, 0);
        SAMPLE_CHECK_ERRORS(err);
    }

    for(cl_uint i = 0; i < number_of_devices; ++i)
    {
        err = clFinish(queues[i]);
        SAMPLE_CHECK_ERRORS(err);

        err = clReleaseMemObject(a_buffers[i]);
        SAMPLE_CHECK_ERRORS(err);
        err = clReleaseMemObject(b_buffers[i]);
        SAMPLE_CHECK_ERRORS(err);
        err = clReleaseMemObject(c_buffers[i]);
        SAMPLE_CHECK_ERRORS(err);

        err = clReleaseCommandQueue(queues[i]);
        SAMPLE_CHECK_ERRORS(err);

        err = clReleaseKernel(kernels[i]);
        SAMPLE_CHECK_ERRORS(err);
        err = clReleaseProgram(programs[i]);
        SAMPLE_CHECK_ERRORS(err);
        err = clReleaseContext(contexts[i]);
        SAMPLE_CHECK_ERRORS(err);
    }

    aligned_free(a_host);
    aligned_free(b_host);
    aligned_free(c_host);
}
