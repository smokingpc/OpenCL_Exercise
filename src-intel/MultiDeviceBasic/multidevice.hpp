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


using namespace std;

// This sample demonstrates three basic usages of multi-device systems.
// Primarily target for these scenarios is a system with multiple
// Intel(R) Xeon Phi(tm) coprocessors, but all information is also applicable
// to any multi-device system (CPU+MIC, CPU+GPU etc.).

// Sample demonstrates minimal sequence of steps to keep all devices busy
// simultaneously. It consists of a simple synthetic kernel operating in 1D
// iteration space, and simple work partitioning strategy -- it just divides
// all work among devices evenly regardless of their compute capabilities.

// The scenarios are:

//    - System-level scenario, with devices mapped to different
//      application instances. Each instance gets its index and knows
//      how many application instances run simultaneously to correctly
//      divide the work and identify the current piece of it.
void system_level_scenario (
    cl_platform_id platform,
    cl_device_type device_type,
    size_t work_size,
    int instance_count,
    int instance_index
);

//    - Multi-context scenario, with one application instance using all
//      devices, and each device having its own context.
void multi_context_scenario (
    cl_platform_id platform,
    cl_device_type device_type,
    size_t work_size
);

//    - Shared-context scenario, with all devices placed in the same
//      shared context, and share input and output buffers by means of sub-buffers.
void shared_context_scenario (
    cl_platform_id platform,
    cl_device_type device_type,
    size_t work_size
);


enum Scenario {
    SCENARIO_SYSTEM_LEVEL,
    SCENARIO_MULTI_CONTEXT,
    SCENARIO_SHARED_CONTEXT
};


// Creates a simple synthetic kernel c[i] = f(a[i], b[i]).
// Used by all scenarios.
cl_program create_program (cl_context context);
