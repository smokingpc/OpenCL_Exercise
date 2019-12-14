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
#include <ctime>
#include <limits>
#include <cmath>

#include <CL/cl.h>

#include "basic.hpp"
#include "cmdoptions.hpp"
#include "oclobject.hpp"

using namespace std;


namespace
{
    /* Based on C++ implementation from
    http://www.johndcook.com/cpp_phi.html
    */
    template <typename T>
    T cdf(T x)
    {
        // constants
        T a1 =  0.254829592f;
        T a2 = -0.284496736f;
        T a3 =  1.421413741f;
        T a4 = -1.453152027f;
        T a5 =  1.061405429f;
        T p  =  0.3275911f;
        // Save the sign of x
        int sign = 1;
        if (x < 0)
            sign = -1;
        x = abs(x)/sqrt((T)2.0f);

        // A&S formula 7.1.26
        // A&S refers to Handbook of Mathematical Functions by Abramowitz and Stegun
        T t = 1.0f/(1.0f + p*x);
        T y = 1.0f - (((((a5*t + a4)*t) + a3)*t + a2)*t + a1)*t*exp(-x*x);

        return 0.5f*(1.0f + sign*y);
    }
}

// Reference Black Scholes formula implementation (vcall_ref, vput_ref) and comparison with option prices calculated
// using Monte Carlo based options pricing method implementation in OpenCL (vcall, vput)
template <class T>
bool checkValidity (size_t nopt,
    T r, T sig, T *s0, T *x,
    T *t, T *vcall, T *vput, T *vcall_ref, T *vput_ref, T threshold)
{
    T a, b, c, y, z, e, d1, d2, w1, w2;
    using namespace std;
    const T HALF = 0.5f;

    for (size_t i = 0; i < nopt; i++ )
    {
        a = log( s0[i] / x[i] );
        b = t[i] * r;
        z = t[i]*sig*sig;

        c = HALF * z;
        e = exp ( -b );
        y = T(1)/sqrt(z);

        w1 = ( a + b + c ) * y;
        w2 = ( a + b - c ) * y;

        d1 = 2.0f*cdf( w1 )-1.0f;
        d2 = 2.0f*cdf( w2 )-1.0f;

        d1 = HALF + HALF*d1;
        d2 = HALF + HALF*d2;

        vcall_ref[i] = s0[i]*d1 - x[i]*e*d2;
        vput_ref[i]  = vcall_ref[i] - s0[i] + x[i]*e;

        if(abs(vcall_ref[i] - vcall[i]) > threshold )
        {
            cerr << "VALIDAION FAILED!!!\n vcall_ref" << "[" << i << "] = "
                << vcall_ref[i] << ", vcall" << "[" << i << "] = "
                << vcall[i] << "\n"
                << "Probably validation threshold = " << threshold << " is too small\n";
            return false;
        }
        if(abs(vput_ref[i] - vput[i]) > threshold)
        {
            cerr << "VALIDAION FAILED!!!\n vput_ref" << "[" << i << "] = "
                << vput_ref[i] << ", vput" << "[" << i << "] = "
                << vput[i] << "\n"
                << "Probably validation threshold = " << threshold << " is too small\n";
            return false;
        }

    }

    std::cout << "VALIDATION PASSED\n";
    return true;
}

// The main Monte Carlo based options pricing function with all application-specific
// OpenCL host-side code.
template <typename T>
void mc (
    CmdParserMC& cmdparser,
    OpenCLBasic& oclobjects,
    OpenCLProgramOneKernel& executable
    )
{
    size_t noptions = cmdparser.options.getValue();
    int nsamples = cmdparser.samples.getValue();
    cout << "Running Monte Carlo options pricing for " << noptions << " options, with " << nsamples << " samples\n";

    // Query for necessary alignment
    size_t alignment = zeroCopyPtrAlignment(oclobjects.device);
    assert(alignment >= sizeof(T)); // should be for sure

    cmdparser.validateParameters(oclobjects, executable, sizeof(T), alignment);

    size_t array_memory_size = zeroCopySizeAlignment(sizeof(T)*noptions, oclobjects.device);
    cout << "Size of memory region for each array: " << array_memory_size << " bytes\n";

    // Allocate memory for matrices
    // Use simple auto pointer implementation for automatic memory management
    OpenCLDeviceAndHostMemory<T> s0;
    s0.host = (T*)aligned_malloc(array_memory_size, alignment);
    OpenCLDeviceAndHostMemory<T> x;
    x.host = (T*)aligned_malloc(array_memory_size, alignment);
    OpenCLDeviceAndHostMemory<T> t;
    t.host = (T*)aligned_malloc(array_memory_size, alignment);
    OpenCLDeviceAndHostMemory<T> vcall;
    vcall.host = (T*)aligned_malloc(array_memory_size, alignment);
    OpenCLDeviceAndHostMemory<T> vput;
    vput.host = (T*)aligned_malloc(array_memory_size, alignment);
    OpenCLDeviceAndHostMemory<T> vcall_ref;
    vcall_ref.host = (T*)aligned_malloc(array_memory_size, alignment);
    OpenCLDeviceAndHostMemory<T> vput_ref;
    vput_ref.host = (T*)aligned_malloc(array_memory_size, alignment);


    // Fill the input arrays with random values in predefinedc range
    // Stock price
    fill_rand_uniform_01(s0.host, noptions);
    T S0L = 10.0f;
    T S0H = 50.0f;
    for(size_t i = 0; i < noptions; i++)
    {
        s0.host[i] = s0.host[i]*(S0H-S0L)+S0L;
    }
    // Strike price
    fill_rand_uniform_01(x.host, noptions);
    T XL = 10.0f;
    T XH = 50.0f;
    for(size_t i = 0; i < noptions; i++)
    {
        x.host[i] = x.host[i]*(XH-XL)+XL;
    }
    // Time
    fill_rand_uniform_01(t.host, noptions);
    T TL = 0.2f;
    T TH = 2.0f;
    for(size_t i = 0; i < noptions; i++)
    {
        t.host[i] = t.host[i]*(TH-TL)+TL;
    }
    // Zero result buffers
    for(size_t i = 0; i < noptions; i++)
    {
        vcall.host[i] = vput.host[i] = vcall_ref.host[i] = vput_ref.host[i] = 0.0f;
    }

    cl_int err = 0; // OpenCL error code

    // Create OpenCL buffers for the matrices based on just allocated memory regions
    // Create buffers with CL_MEM_USE_HOST_PTR to minimize copying and
    // model situation when matrices are hosted by some native library that
    // uses OpenCL to accelerate calculations.

    s0.device = clCreateBuffer(
        oclobjects.context,
        CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
        array_memory_size,
        s0.host,
        &err
        );
    SAMPLE_CHECK_ERRORS(err);

    x.device = clCreateBuffer(
        oclobjects.context,
        CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
        array_memory_size,
        x.host,
        &err
        );
    SAMPLE_CHECK_ERRORS(err);

    t.device = clCreateBuffer(
        oclobjects.context,
        CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
        array_memory_size,
        t.host,
        &err
        );
    SAMPLE_CHECK_ERRORS(err);

    vcall.device = clCreateBuffer(
        oclobjects.context,
        CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR,
        array_memory_size,
        vcall.host,
        &err
        );
    SAMPLE_CHECK_ERRORS(err);

    vput.device = clCreateBuffer(
        oclobjects.context,
        CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR,
        array_memory_size,
        vput.host,
        &err
        );
    SAMPLE_CHECK_ERRORS(err);


    T risk_free = 0.05f;
    T sigma = 0.2f; // volatility
    cout << "Using risk free rate = " << risk_free << " and volatility = " << sigma << "\n";

    // Setting kernel arguments

    err = clSetKernelArg(executable.kernel, 0, sizeof(cl_mem), &vcall.device);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 1, sizeof(cl_mem), &vput.device);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 2, sizeof(T), &risk_free);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 3, sizeof(T), &sigma);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 4, sizeof(cl_mem), &s0.device);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 5, sizeof(cl_mem), &x.device);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 6, sizeof(cl_mem), &t.device);
    SAMPLE_CHECK_ERRORS(err);

    // Define ndrange iteration space: global and local sizes based on
    // parameters given by user

    size_t global_size[1] = {noptions};
    size_t tmp_wg_size = cmdparser.work_group_size.getValue();
    size_t *local_size = NULL;
    if(tmp_wg_size)
    {
        local_size = &tmp_wg_size;
    }


    for(int i = 0; i < cmdparser.iterations.getValue(); ++i)
    {
        // Here we start measurings host time for kernel execution
        double start = time_stamp();

        err = clEnqueueNDRangeKernel(
            oclobjects.queue,
            executable.kernel,
            1,
            0,
            global_size,
            local_size,
            0, 0, 0
            );
        SAMPLE_CHECK_ERRORS(err);

        err = clFinish(oclobjects.queue);
        SAMPLE_CHECK_ERRORS(err);

        // It is important to measure end host time immediately after clFinish
        double end = time_stamp();

        double time = end - start;
        cout << "Host time: " << time << " sec.\n";
        cout << "Host perf: " << noptions/time << " Options per second\n";

        if(i == 0 && cmdparser.validation.getValue())
        {
            // validate result for the first iteration only and
            // only if user wants this

            clEnqueueMapBuffer(
                oclobjects.queue,
                vcall.device,
                CL_TRUE,    // blocking map
                CL_MAP_READ,
                0,
                array_memory_size,
                0, 0, 0,
                &err
                );
            SAMPLE_CHECK_ERRORS(err);

            clEnqueueMapBuffer(
                oclobjects.queue,
                vput.device,
                CL_TRUE,    // blocking map
                CL_MAP_READ,
                0,
                array_memory_size,
                0, 0, 0,
                &err
                );
            SAMPLE_CHECK_ERRORS(err);


            // After map calls, host memory area for result arrays are
            // automatically updated with the latest bits from the device
            // So we just use it
            if(
                !checkValidity(noptions, risk_free, sigma, s0.host, x.host, t.host,
                vcall.host, vput.host, vcall_ref.host, vput_ref.host, (T)cmdparser.validation_threshold.getValue()
                )
                )
            {
                throw runtime_error("Validation procedure reported failures");
            }

            err = clEnqueueUnmapMemObject(
                oclobjects.queue,
                vput.device,
                vput.host,
                0, 0, 0
                );
            SAMPLE_CHECK_ERRORS(err);

            err = clEnqueueUnmapMemObject(
                oclobjects.queue,
                vcall.device,
                vcall.host,
                0, 0, 0
                );
            SAMPLE_CHECK_ERRORS(err);


            // Finish here is only required for correct time measurment on the next iteration
            // It does not affect correctness of calculations as the in-order OpenCL queue is used
            clFinish(oclobjects.queue);
            SAMPLE_CHECK_ERRORS(err);
        }
    }
}


int main (int argc, const char** argv)
{
    try
    {
        CmdParserMC cmdparser(argc, argv);
        cmdparser.parse();

        // Immediatly exit if user wanted to see usage info only.
        if(cmdparser.help.isSet())
        {
            return EXIT_SUCCESS;
        }

        // Create necessary OpenCL objects up to device queue
        OpenCLBasic oclobjects(
            cmdparser.platform.getValue(),
            cmdparser.device_type.getValue(),
            cmdparser.device.getValue()
            );

        // Form build options string from given parameters: macros definitions to pass into kernels
        string build_options;

        if(cmdparser.arithmetic_float.isSet())
        {
            build_options = "-D__DO_FLOAT__ -cl-denorms-are-zero -cl-fast-relaxed-math -cl-single-precision-constant"
                " -DNSAMP=" + to_str(cmdparser.samples.getValue());
        }
        else if(cmdparser.arithmetic_double.isSet())
        {
            build_options = "-cl-denorms-are-zero -cl-fast-relaxed-math"
                " -DNSAMP=" + to_str(cmdparser.samples.getValue());;
        }

        cout << "Build program options: " << inquotes(build_options) << "\n";

        // Build kernel
        OpenCLProgramOneKernel executable(
            oclobjects,
            L"montecarlo.cl",
            "",
            "MonteCarloEuroOptCLKernelScalarBoxMuller",
            build_options
            );

        // Call Monte Carlo-based options pricing method with required type of elements
        if(cmdparser.arithmetic_float.isSet())
        {
            mc<float>(cmdparser, oclobjects, executable);
        }
        else if(cmdparser.arithmetic_double.isSet())
        {
            mc<double>(cmdparser, oclobjects, executable);
        }

        // All resource deallocations happen in destructors of helper objects.

        return EXIT_SUCCESS;
    }
    catch(const CmdParser::Error& error)
    {
        cerr
            << "[ ERROR ] In command line: " << error.what() << "\n"
            << "Run " << argv[0] << " -h for usage info.\n";
        return EXIT_FAILURE;
    }
    catch(const exception& error)
    {
        cerr << "[ ERROR ] " << error.what() << "\n";
        return EXIT_FAILURE;
    }
    catch(...)
    {
        cerr << "[ ERROR ] Unknown/internal error happened.\n";
        return EXIT_FAILURE;
    }
}
