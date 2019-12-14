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


#include <CL/cl.h>

#include "basic.hpp"
#include "multidevice.hpp"

using namespace std;


cl_program create_program (cl_context context)
{
    // Create a synthetic kernel.
    const char* source =
        "   kernel void simple (                "
        "       global const float* a,          "
        "       global const float* b,          "
        "       global float* c                 "
        "   )                                   "
        "   {                                   "
        "       int i = get_global_id(0);       "
        "       float tmp = 0;                  "
        "       for(int j = 0; j < 100000; ++j) "
        "           tmp += a[i] + b[i];         "
        "       c[i] = tmp;                     "
        "   }                                   "
    ;

    cl_int err = 0;
    cl_program program = clCreateProgramWithSource(context, 1, &source, 0, &err);
    SAMPLE_CHECK_ERRORS(err);
    return program;
}
