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


#include <limits>
#include <cmath>

#include "cmdoptions.hpp"

using namespace std;

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4355)    // 'this': used in base member initializer list
#endif

CmdParserMC::CmdParserMC (int argc, const char** argv) :
CmdParserCommon(argc, argv),
    iterations(
        *this,
        'i',
        "iterations",
        "<integer>",
        "Number of kernel invocations. For each invoction, "
        "performance information will be printed.",
        10
    ),
    options(
        *this,
        'o',
        "options",
        "<integer>",
        "Number of options to model.",
        65536
    ),
    work_group_size(*this),
    samples(
        *this,
        's',
        "samples",
        "<integer>",
        "Number of Monte Carlo samples.",
        262144
    ),
    arithmetic(
        *this,
        'a',
        "arithmetic",
        "",
        "Type of elements and all calculations.",
        "float"
    ),
    arithmetic_float
    (
        arithmetic,
        "float"
    ),
    arithmetic_double
    (
        arithmetic,
        "double"
    ),
    validation_threshold(
        *this,
        'e',
        "threshold",
        "<float>",
        "Validation threshold (if validation is enabled).",
        0.1f
    ),
    validation(
        *this,
        0,
        "validation",
        "",
        "Enables validation procedure on host (slow for big task sizes).",
        false
    )
{
}


#ifdef _MSC_VER
#pragma warning (pop)
#endif


void CmdParserMC::parse ()
{
    CmdParserCommon::parse();

    // Test a small part of parameters for consistency
    // in this function. The major part of checks is placed in the
    // validateParameters function. But to call it you need
    // further specialization on what OpenCL objects and their
    // capabilities are.

    if(arithmetic_float.isSet() && arithmetic_double.isSet())
    {
        throw CmdParser::Error(
            "Both float and double are chosen. "
            "Should be only one of them."
            );
    }

    if(!arithmetic_float.isSet() && !arithmetic_double.isSet())
    {
        throw CmdParser::Error(
            "Neither float nor double are chosen. "
            "One of them should be chosen."
            );
    }
}

size_t CmdParserMC::estimateMaxArraySize (
    OpenCLBasic& oclobjects,
    size_t size_of_element,
    size_t alignment
    )
{
    cl_ulong max_alloc_size = 0;
    cl_int err = clGetDeviceInfo(
        oclobjects.device,
        CL_DEVICE_MAX_MEM_ALLOC_SIZE,
        sizeof(max_alloc_size),
        &max_alloc_size,
        0
        );
    SAMPLE_CHECK_ERRORS(err);

    cl_ulong max_global_mem_size = 0;
    err = clGetDeviceInfo(
        oclobjects.device,
        CL_DEVICE_GLOBAL_MEM_SIZE,
        sizeof(max_global_mem_size),
        &max_global_mem_size,
        0
        );
    SAMPLE_CHECK_ERRORS(err);

    double max_array_size = min(
        double(numeric_limits<size_t>::max()),
        min(double(max_alloc_size), double(max_global_mem_size)/5)
        ) / size_of_element;

    assert(alignment%size_of_element == 0);

    // the following is the effect of a conservative
    // estimation of the overhead on a row alignment
    max_array_size -= alignment/size_of_element;

    assert(max_array_size < double(numeric_limits<size_t>::max()));

    // and finally we apply natural limit on int maximum value
    return static_cast<size_t>(max_array_size);
}


void CmdParserMC::validateParameters (
    OpenCLBasic& oclobjects,
    OpenCLProgramOneKernel& executable,
    size_t size_of_element,
    size_t alignment
    )
{
    validatePositiveness(options);
    validatePositiveness(samples);
    if(work_group_size.getValue()!=0) // 0 is valid value. 0 means NULL for NDRange. OpenCL selects the work-group size automatically.
        validatePositiveness(work_group_size);

    size_t max_array_size =
        estimateMaxArraySize(oclobjects, size_of_element, alignment);

    options.validate(
        options.getValue() <= max_array_size,
        "requested value is too big; should be <= " + to_str(max_array_size)
        );

    iterations.validate(
        iterations.getValue() >= 0,
        "negative value is provided; should be positive or zero"
        );

    if(work_group_size.getValue()!=0)
    {
        // check work-group size validity for manual work-group size setup
        size_t max_work_item_sizes[3] = {0};
        deviceMaxWorkItemSizes(oclobjects.device, max_work_item_sizes);


        options.validate(
            options.getValue() >= work_group_size.getValue(),
            "work group size should be less or equal to array size"
            );

        options.validate(
            options.getValue() % work_group_size.getValue() == 0,
            "work group size should divide array size without a remainder"
            );


        size_t max_device_work_group_size =
            deviceMaxWorkGroupSize(oclobjects.device);

        size_t max_kernel_work_group_size =
            kernelMaxWorkGroupSize(executable.kernel, oclobjects.device);

        size_t max_work_group_size =
            min(max_device_work_group_size, max_kernel_work_group_size);

        if(work_group_size.getValue() > max_kernel_work_group_size)
        {
            throw CmdParser::Error(
                "Work group size "
                "is greater than allowed for this kernel and/or device. "
                "Maximum possible value is " +
                to_str(max_kernel_work_group_size) + "."
                );
        }
    }
}
