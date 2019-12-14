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

#include "stdafx.h"
#include <iostream>

#include "oclobject.hpp"
#include "cmdparser.hpp"
#include "basic.hpp"

using namespace std;


#pragma warning( push )

//local work size
#define BLOCK_DIM 16

#define TEST_RUNS 20

//to enable kernel version with per pixel processing
//#define PER_PIXEL

//memory objects used by the sample
cl_mem        g_inputBuffer = NULL;
cl_mem        g_inputBuffer1 = NULL;
cl_mem        g_inputBuffer2 = NULL;
cl_mem        g_outputBuffer = NULL;
cl_mem        g_inputSubBuffer = NULL;
cl_mem        g_outputSubBuffer = NULL;
cl_mem        g_inputSubBufferPG = NULL;
cl_mem        g_outputSubBufferPG = NULL;

//context, either single- or multi-device ("shared")
cl_context    g_context = NULL;

//command queue for CPU and Processor Graphics
cl_command_queue g_cmd_queue = NULL;
cl_command_queue g_cmd_queue_pg = NULL;

cl_program    g_program = NULL;
cl_kernel    g_kernel = NULL;

cl_uint     g_globalWorkSize = 0;

//alignment required by the USE_HOST_PTR to avoid copying upon map/unmap, values for CPU and Processor Graphics devices resp
cl_int      g_min_align = 0;
cl_int      g_min_align_pg = 0;

//events list, one event for CPU and another for Processor Graphics
cl_event    g_events_list[2];


//set of flags to enable various processing branches/modes
bool g_bRunOnPG = false;//executing on Processor-Graphics (single-device mode)
bool g_bUseBothDevices = false;//executing simultaneously on CPU and Processor-Graphics (multi-device mode)
bool g_bSwapImage = false;//emulating the new input arriving each frame
bool g_bSilentMode = true;//disabling detailed staticstics




//performance counters
cl_double g_PerformanceCountOverallStart;
cl_double g_PerformanceCountOverallStop;

//variables for load-balancing and averaging of the splitting ratio (CB - cyclic buffer to keep performance history of the prev frames)
cl_double g_NDRangeTime1 = 1;
cl_double g_NDRangeTime2 = 1;
cl_double g_NDRangeTimeRatioLast = 0.5;
cl_double g_NDRangeTimeRatioCB[256];
cl_uint g_NDRangeTimeRatioCBCounter=0;
const cl_uint g_NDRangeTimeRatioCBLength=10;
cl_bool g_NDRangeTimeRatioCBFull = false;

//counter for image swap mode
cl_uint g_ImageSwapCounter=0;

cl_platform_id g_platform_id;


void Cleanup_OpenCL()
{
    //release kernel, program, memory etc objects
    if(!g_bSwapImage)
    {
        if( g_inputBuffer ) {clReleaseMemObject( g_inputBuffer ); g_inputBuffer = NULL;}
    }
    else
    {
        if( g_inputBuffer1 ) {clReleaseMemObject( g_inputBuffer1 ); g_inputBuffer1 = NULL;}
        if( g_inputBuffer2 ) {clReleaseMemObject( g_inputBuffer2 ); g_inputBuffer2 = NULL;}
    }
    if( g_outputBuffer ) {clReleaseMemObject( g_outputBuffer );  g_outputBuffer = NULL;}
    if( g_inputSubBuffer ) {clReleaseMemObject( g_inputSubBuffer ); g_inputSubBuffer = NULL;}
    if( g_outputSubBuffer ) {clReleaseMemObject( g_outputSubBuffer );  g_outputSubBuffer = NULL;}
    if( g_inputSubBufferPG ) {clReleaseMemObject( g_inputSubBufferPG ); g_inputSubBufferPG = NULL;}
    if( g_outputSubBufferPG ) {clReleaseMemObject( g_outputSubBufferPG );  g_outputSubBufferPG = NULL;}
    if( g_kernel ) {clReleaseKernel( g_kernel );  g_kernel = NULL;}
    if( g_program ) {clReleaseProgram( g_program );  g_program = NULL;}
    if( g_cmd_queue )
    {
        clFinish(g_cmd_queue);
        clReleaseCommandQueue( g_cmd_queue );
        g_cmd_queue = NULL;
    }
    if( g_cmd_queue_pg )
    {
        clFinish(g_cmd_queue_pg);
        clReleaseCommandQueue( g_cmd_queue_pg );
        g_cmd_queue_pg = NULL;
    }
    if( g_context ) {clReleaseContext( g_context );  g_context = NULL;}
}

#ifdef __linux__
bool Setup_OpenCL( const char *program_source )
#else
bool Setup_OpenCL( const wchar_t *program_source )
#endif
{
    cl_device_id devices[16];
    size_t cb;
    cl_uint size_ret = 0;
    cl_int err;
    cl_device_id device_ID;
    char device_name[128] = {0};

    printf("Trying to run on %s\n", g_bUseBothDevices? "CPU+Processor Graphics" : (g_bRunOnPG ? "Processor Graphics": "CPU"));

    cl_context_properties context_properties[3] = {CL_CONTEXT_PLATFORM, (cl_context_properties)g_platform_id, NULL };

    // create the OpenCL context with CPU/PG or both
    cl_device_type dev = g_bUseBothDevices ? (CL_DEVICE_TYPE_CPU|CL_DEVICE_TYPE_GPU) : (g_bRunOnPG ? CL_DEVICE_TYPE_GPU:CL_DEVICE_TYPE_CPU);
    g_context = clCreateContextFromType(context_properties, dev, NULL, NULL, &err);

    if (g_context == (cl_context)0)
    {
        printf("ERROR: Failed to clCreateContextFromType...\n");
        printf("%s\n", OCL_GetErrorString(err));
        Cleanup_OpenCL();
        return false;
    }


    // get the official list of the devices associated with context
    err = clGetContextInfo(g_context, CL_CONTEXT_DEVICES, 0, NULL, &cb);
    clGetContextInfo(g_context, CL_CONTEXT_DEVICES, cb, devices, NULL);

    // queue for the first device(the only queue in the case of single-device scenario)
    g_cmd_queue = clCreateCommandQueue(g_context, devices[0], CL_QUEUE_PROFILING_ENABLE, &err);
    if (g_cmd_queue == (cl_command_queue)0)
    {
        printf("ERROR: Failed to clCreateCommandQueue device 0 (CPU)...\n");
        printf("%s\n", OCL_GetErrorString(err));
        Cleanup_OpenCL();
        return false;
    }

    if(g_bUseBothDevices)
    {
        // queue for the second device
        g_cmd_queue_pg = clCreateCommandQueue(g_context, devices[1], CL_QUEUE_PROFILING_ENABLE, &err);
        if (g_cmd_queue_pg == (cl_command_queue)0)
        {
            printf("ERROR: Failed to clCreateCommandQueue device 1 (PG)...\n");
            printf("%s\n", OCL_GetErrorString(err));
            Cleanup_OpenCL();
            return false;
        }
    }


    char *sources = ReadSources(program_source);    // read program .cl source file
    g_program = clCreateProgramWithSource(g_context, 1, (const char**)&sources, NULL, &err);
    if (g_program == (cl_program)0)
    {
        printf("ERROR: Failed to create Program with source...\n");
        printf("%s\n", OCL_GetErrorString(err));
        Cleanup_OpenCL();
        free(sources);
        return false;
    }

    static const char buildOpts[] = "-cl-fast-relaxed-math -cl-denorms-are-zero";

    err = clBuildProgram(g_program, 0, NULL, buildOpts, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        printf("ERROR: Failed to build program...\n");
        printf("%s\n", OCL_GetErrorString(err));
        BuildFailLog(g_program, devices[0]);
        Cleanup_OpenCL();
        free(sources);
        return false;
    }
    // kernel object is shared by devices, similarly to program object
#ifdef PER_PIXEL
    g_kernel = clCreateKernel(g_program, "ToneMappingPerPixel", &err);
#else
    g_kernel = clCreateKernel(g_program, "ToneMappingLine", &err);
#endif
    if (g_kernel == (cl_kernel)0)
    {
        printf("ERROR: Failed to create kernel...\n");
        printf("%s\n", OCL_GetErrorString(err));
        Cleanup_OpenCL();
        free(sources);
        return false;
    }

    free(sources);

    // let's retrieve info on the devices in the context

    // query caps of the first device
    device_ID = devices[0];

    err = clGetDeviceInfo(device_ID, CL_DEVICE_NAME, 128, device_name, NULL);
    if (err!=CL_SUCCESS)
    {
        printf("ERROR: Failed to get device information (device name)...\n");
        Cleanup_OpenCL();
        return false;
    }
    printf("Using device %s...\n", device_name);

    g_min_align = zeroCopyPtrAlignment(device_ID);
    printf("Expected min alignment for buffers is %d bytes...\n", g_min_align);

    if(g_bUseBothDevices)
    {
        // now let's query caps of the second device
        device_ID = devices[1];

        err = clGetDeviceInfo(device_ID, CL_DEVICE_NAME, 128, device_name, NULL);
        if (err!=CL_SUCCESS)
        {
            printf("ERROR: Failed to get device information (device name)...\n");
            Cleanup_OpenCL();
            return false;
        }
        printf("Using device %s...\n", device_name);

        g_min_align_pg = zeroCopyPtrAlignment(device_ID);
        printf("Expected min alignment for buffers is %d bytes...\n", g_min_align_pg);
        g_min_align = max(g_min_align, g_min_align_pg);
    }

    return true; // success...
}

cl_float* readInput(cl_uint* arrayWidth, cl_uint* arrayHeight)
{

    // Load from HDR-image

    // Variables
    int iMemSize = 0;
    int iResultMemSize = 0;
    float fTmpVal = 0.0f;
    int iWidth = 0;
    int iHeight = 0;
    cl_float* inputArray = 0;

    FILE* pRGBAFile = 0;
#ifdef __linux__    
    std::string tmp = wstringToString(L"ToneMappingMultiDevice.rgb");
    pRGBAFile = fopen(FULL_PATH_A(tmp.c_str()),"rb");
#else
    pRGBAFile = _wfopen(FULL_PATH_W("ToneMappingMultiDevice.rgb"),L"rb");
#endif    
    if(!pRGBAFile)
    {
        printf("HOST: Failed to open the HDR image file!\n");
        return 0;
    }

    fread((void*)&iWidth, sizeof(int), 1, pRGBAFile);
    fread((void*)&iHeight, sizeof(int), 1, pRGBAFile);
    printf("width = %d\n", iWidth);
    printf("height = %d\n", iHeight);

    if(iWidth<=0 || iHeight<=0 || iWidth > 1000000 || iHeight > 1000000)
    {
        printf("HOST: width or height values are invalid!\n");
        fclose(pRGBAFile);
        return 0;
    }

    // The image size in memory (bytes).
    iMemSize = iWidth*iHeight*sizeof(cl_float4);

    // Allocate memory.
    inputArray = (cl_float*)aligned_malloc(zeroCopySizeAlignment(iMemSize), g_min_align);
    if(!inputArray)
    {
        printf("Failed to allocate memory for input HDR image!\n");
        fclose(pRGBAFile);
        return 0;
    }

    // Calculate global work size
    g_globalWorkSize = iHeight;


    // Read data from the input file to memory.
    fread((void*)inputArray, 1, iMemSize, pRGBAFile);

    // Extended dynamic range (4 channels pixel)
    for(int i = 0; i < iWidth*iHeight*4; i++)
    {
        inputArray[i] = 5.0f*inputArray[i];
    }


    // HDR-image height & width
    *arrayWidth = iWidth;
    *arrayHeight = iHeight;

    fclose(pRGBAFile);

    // Save input image in bitmap file (without tone mapping, just linear scale and crop)
    float fTmpFVal = 0.0f;
    cl_uint* outUIntBuf=0;
    outUIntBuf = (cl_uint*)malloc(iWidth*iHeight*sizeof(cl_uint));
    if(!outUIntBuf)
    {
        free(inputArray);
        printf("Failed to allocate memory for output image!\n");
        return 0;
    }
    for(int y = 0; y < iHeight; y++)
    {
        for(int x = 0; x < iWidth; x++)
        {
            // Ensure that no value is greater than 255.0
            cl_uint uiTmp[4]; // 4 - means 4-channel pixel
            fTmpFVal = (255.0f*inputArray[(y*iWidth+x)*4+0]);
            if(fTmpFVal>255.0f)
                fTmpFVal=255.0f;
            uiTmp[0] = (cl_uint)(fTmpFVal);

            fTmpFVal = (255.0f*inputArray[(y*iWidth+x)*4+1]);
            if(fTmpFVal>255.0f)
                fTmpFVal=255.0f;
            uiTmp[1] = (cl_uint)(fTmpFVal);

            fTmpFVal = (255.0f*inputArray[(y*iWidth+x)*4+2]);
            if(fTmpFVal>255.0f)
                fTmpFVal=255.0f;
            uiTmp[2] = (cl_uint)(fTmpFVal);

            inputArray[(y*iWidth+x)*4+3] = 0.0f;
            fTmpFVal = (255.0f*inputArray[(y*iWidth+x)*4+3]);
            if(fTmpFVal>255.0f)
                fTmpFVal=255.0f;
            uiTmp[3] = (cl_uint)(fTmpFVal);    //Alfa

            outUIntBuf[(iHeight-1-y)*iWidth+x] = 0x000000FF & uiTmp[2];
            outUIntBuf[(iHeight-1-y)*iWidth+x] |= 0x0000FF00 & ((uiTmp[1]) << 8);
            outUIntBuf[(iHeight-1-y)*iWidth+x] |= 0x00FF0000 & ((uiTmp[0]) << 16);
            outUIntBuf[(iHeight-1-y)*iWidth+x] |= 0xFF000000 & ((uiTmp[3]) << 24);
        }
    }
    //----
    SaveImageAsBMP( outUIntBuf, iWidth, iHeight, "ToneMappingMultiDeviceInput.bmp");
    free(outUIntBuf);

    return inputArray;
}

// declaration of the reference native function
void EvaluateRaw(float* inputArray, float* outputArray, CHDRData *pData, int arrayWidth, int iRow);

void ExecuteToneMappingReference(cl_float* inputArray, cl_float* outputArray, CHDRData *pData, cl_uint arrayWidth, cl_uint arrayHeight)
{
    // image height loop
    for(unsigned int j = 0; j < arrayHeight;j++)
    {
        EvaluateRaw(inputArray, outputArray, pData, arrayWidth, j);
    }
}

// helper function for calculation of the splitting ratio (for input/output buffers)
void ComputeSplittingRatio(cl_uint arrayHeight, cl_uint *arrayHeightDev1, cl_uint *arrayHeightDev2)
{
    // estimate ratio using the previous frame performance data
    cl_double dNDRangeRatio = (g_NDRangeTime2*g_NDRangeTimeRatioLast)/(g_NDRangeTime1*(1-g_NDRangeTimeRatioLast)+g_NDRangeTime2*g_NDRangeTimeRatioLast);

    // here we compute splitting ratio,while averaging it over last "frames"
    // fill cyclic buffer
    g_NDRangeTimeRatioCB[g_NDRangeTimeRatioCBCounter] = dNDRangeRatio;
    g_NDRangeTimeRatioCBCounter++;
    cl_double tmpNDRangeTimeRatioSum = 0.0;
    // average over cyclic buffer
    int num = g_NDRangeTimeRatioCBFull ? g_NDRangeTimeRatioCBLength : g_NDRangeTimeRatioCBCounter;
    for(int iii = 0; iii < num; iii++)
    {
        tmpNDRangeTimeRatioSum += g_NDRangeTimeRatioCB[iii];
    }
    tmpNDRangeTimeRatioSum = tmpNDRangeTimeRatioSum/num; // averaging
    // check cyclic buffer fullness
    if(g_NDRangeTimeRatioCBCounter==g_NDRangeTimeRatioCBLength)
    {
        g_NDRangeTimeRatioCBFull = true;
        g_NDRangeTimeRatioCBCounter = 0; // reset cyclic buffer counter
    }
    // update ratio
    dNDRangeRatio = tmpNDRangeTimeRatioSum;

    // estimate buffer split ratio
    *arrayHeightDev1 = (cl_uint)(dNDRangeRatio*(cl_double)arrayHeight);
    *arrayHeightDev1 = (*arrayHeightDev1 / BLOCK_DIM)*BLOCK_DIM; // make the arrayHeightDev1 to be dividable by local size
    *arrayHeightDev2 = arrayHeight - *arrayHeightDev1; // the rest is for the second device

    g_NDRangeTimeRatioLast = dNDRangeRatio;

}

// prepare sub-buffers and constant buffer
bool PrepareResources(cl_uint arrayWidth, cl_uint *arrayHeightDev1, cl_uint *arrayHeightDev2)
{
    cl_int err = CL_SUCCESS;
    cl_buffer_region inputBufferRegion = { 0,  sizeof(cl_float4) * arrayWidth * (*arrayHeightDev1) };
    cl_buffer_region outputBufferRegion = { 0,  sizeof(cl_float4) * arrayWidth * (*arrayHeightDev1) };
    cl_buffer_region inputBufferRegionPG = { sizeof(cl_float4) * arrayWidth * (*arrayHeightDev1),  sizeof(cl_float4) * arrayWidth * (*arrayHeightDev2) };
    cl_buffer_region outputBufferRegionPG = { sizeof(cl_float4) * arrayWidth * (*arrayHeightDev1),  sizeof(cl_float4) * arrayWidth * (*arrayHeightDev2) };

    // check alignment
    assert((sizeof(cl_float4) * arrayWidth * (*arrayHeightDev1))%g_min_align==0);


    g_inputSubBuffer = clCreateSubBuffer(g_inputBuffer, 0, CL_BUFFER_CREATE_TYPE_REGION, &inputBufferRegion, &err);
    if (err != CL_SUCCESS)
    {
        printf("ERROR: Failed to clCreateSubBuffer (1)...\n");
        return false;
    }
    g_outputSubBuffer = clCreateSubBuffer(g_outputBuffer, 0, CL_BUFFER_CREATE_TYPE_REGION, &outputBufferRegion, &err);
    if (err != CL_SUCCESS)
    {
        printf("ERROR: Failed to clCreateSubBuffer (2)...\n");
        return false;
    }
    g_inputSubBufferPG = clCreateSubBuffer(g_inputBuffer, 0, CL_BUFFER_CREATE_TYPE_REGION, &inputBufferRegionPG, &err);
    if (err != CL_SUCCESS)
    {
        printf("ERROR: Failed to clCreateSubBuffer (3)...\n");
        return false;
    }
    g_outputSubBufferPG = clCreateSubBuffer(g_outputBuffer, 0, CL_BUFFER_CREATE_TYPE_REGION, &outputBufferRegionPG, &err);
    if (err != CL_SUCCESS)
    {
        printf("ERROR: Failed to clCreateSubBuffer (4)...\n");
        return false;
    }
    return true;
}

// release sub-buffers
void ReleaseSubResources()
{
    if(g_inputSubBuffer)
    {
        clReleaseMemObject(g_inputSubBuffer); g_inputSubBuffer = NULL;
    }
    if(g_outputSubBuffer)
    {
        clReleaseMemObject(g_outputSubBuffer); g_outputSubBuffer = NULL;
    }
    if(g_inputSubBufferPG)
    {
        clReleaseMemObject(g_inputSubBufferPG); g_inputSubBufferPG = NULL;
    }
    if(g_outputSubBufferPG)
    {
        clReleaseMemObject(g_outputSubBufferPG); g_outputSubBufferPG = NULL;
    }
}

bool FrameExecutionStatistics()
{
    cl_ulong start = 0;
    cl_ulong end = 0;
    cl_int err = CL_SUCCESS;

    // notice that pure HW execution time is END-START
    err = clGetEventProfilingInfo(g_events_list[0], CL_PROFILING_COMMAND_START, sizeof(cl_ulong), &start, NULL);
    if (err != CL_SUCCESS)
    {
        printf("ERROR: Failed to get clGetEventProfilingInfo CL_PROFILING_COMMAND_START...\n");
        return false;
    }
    err = clGetEventProfilingInfo(g_events_list[0], CL_PROFILING_COMMAND_END, sizeof(cl_ulong), &end, NULL);
    if (err != CL_SUCCESS)
    {
        printf("ERROR: Failed to get clGetEventProfilingInfo CL_PROFILING_COMMAND_END...\n");
        return false;
    }
    g_NDRangeTime1 = (cl_double)(end - start)*(cl_double)(1e-06);
    printf("Execution time: for #1 device is %.3f ms", g_NDRangeTime1);

    if(g_bUseBothDevices)
    {
        err = clGetEventProfilingInfo(g_events_list[1], CL_PROFILING_COMMAND_START, sizeof(cl_ulong), &start, NULL);
        if (err != CL_SUCCESS)
        {
            printf("ERROR: Failed to get clGetEventProfilingInfo CL_PROFILING_COMMAND_START...\n");
            return false;
        }
        err = clGetEventProfilingInfo(g_events_list[1], CL_PROFILING_COMMAND_END, sizeof(cl_ulong), &end, NULL);
        if (err != CL_SUCCESS)
        {
            printf("ERROR: Failed to get clGetEventProfilingInfo CL_PROFILING_COMMAND_END...\n");
            return false;
        }
        g_NDRangeTime2 = (cl_double)(end - start)*(cl_double)(1e-06);
        printf(", for the #2 device: %.3f ms \nThe frame was splitted as %.1f%% by %.1f%% between device #1 and #2", g_NDRangeTime2, g_NDRangeTimeRatioLast*100, (1-g_NDRangeTimeRatioLast)*100);
    }
    printf("\n");
    return true;
}

bool SubmitJobsToQueues(const cl_uint arrayWidth, const cl_uint arrayHeight, const CHDRData HDRData)
{
    cl_uint arrayHeightDev1;
    cl_uint arrayHeightDev2;
    cl_int err = CL_SUCCESS;

    if(g_bUseBothDevices)
    {
        // compute splitting ratio for buffers
        ComputeSplittingRatio(arrayHeight, &arrayHeightDev1, &arrayHeightDev2);

        // create sub-resources (buffers)
        if(!PrepareResources(arrayWidth, &arrayHeightDev1, &arrayHeightDev2))
        {
            return false;
        }
    }

    // set kernel args for the first device (also in case it is the only device)
    err  = clSetKernelArg(g_kernel, 0, sizeof(cl_mem),  (void *) & (g_bUseBothDevices ? g_inputSubBuffer  : g_inputBuffer ) );
    err  |= clSetKernelArg(g_kernel, 1, sizeof(cl_mem), (void *) & (g_bUseBothDevices ? g_outputSubBuffer : g_outputBuffer ) );
    err  |= clSetKernelArg(g_kernel, 2, sizeof(CHDRData), (void *) &HDRData);
    err  |= clSetKernelArg(g_kernel, 3, sizeof(cl_int), (void *) &arrayWidth);
    if (err != CL_SUCCESS)
    {
        printf("ERROR: Failed to set input g_kernel arguments...\n");
        return false;
    }

    size_t globalWorkSize[2];
    size_t globalWorkSize2[2];

    size_t localWorkSize[2]= {BLOCK_DIM, BLOCK_DIM};

    if(g_bUseBothDevices)
    {
#ifndef PER_PIXEL
        globalWorkSize[0] = arrayHeightDev1;
        globalWorkSize2[0] = arrayHeightDev2;
        globalWorkSize[1] = arrayWidth;
        globalWorkSize2[1] = arrayWidth;
#else
        globalWorkSize[1] = arrayHeightDev1;
        globalWorkSize2[1] = arrayHeightDev2;
        globalWorkSize[0] = arrayWidth;
        globalWorkSize2[0] = arrayWidth;
#endif
        if(!g_bSilentMode)
        {
            printf("Global work size device#1    %d\n", globalWorkSize[0]);
            printf("Global work size device#2    %d\n", globalWorkSize2[0]);
            printf("Original local work size     %d\n", localWorkSize[0]);
        }
    }
    else
    {
#ifndef PER_PIXEL
        globalWorkSize[0] = g_globalWorkSize;
        globalWorkSize[1] = arrayWidth;
#else
        globalWorkSize[1] = g_globalWorkSize;
        globalWorkSize[0] = arrayWidth;
#endif
        if(!g_bSilentMode)
        {
            printf("Original global work size %d\n", globalWorkSize[0]);
            printf("Original local work size %d\n", localWorkSize[0]);
        }
    }

#ifdef PER_PIXEL
    cl_uint workDim = 2;
#else
    cl_uint workDim = 1;
#endif

    // submit kernel command for the first device
    if (CL_SUCCESS != clEnqueueNDRangeKernel(g_cmd_queue, g_kernel, workDim, NULL, globalWorkSize, localWorkSize, 0, NULL, &g_events_list[0]))
    {
        printf("ERROR: Failed to run kernel...\n");
        return false;
    }
    if(g_bUseBothDevices)
    {
        // submit kernel command for the second device
        err  = clSetKernelArg(g_kernel, 0, sizeof(cl_mem), (void *) &g_inputSubBufferPG);
        err  |= clSetKernelArg(g_kernel, 1, sizeof(cl_mem), (void *) &g_outputSubBufferPG);
        err  |= clSetKernelArg(g_kernel, 2, sizeof(CHDRData), (void *) &HDRData);
        err  |= clSetKernelArg(g_kernel, 3, sizeof(cl_int), (void *) &arrayWidth);


        if (err != CL_SUCCESS)
        {
            printf("ERROR: Failed to set input g_kernel arguments...\n");
            return false;
        }
        if (CL_SUCCESS != clEnqueueNDRangeKernel(g_cmd_queue_pg, g_kernel, workDim, NULL, globalWorkSize2, localWorkSize, 0, NULL, &g_events_list[1]))
        {
            printf("ERROR: Failed to run kernel...\n");
            return false;
        }
    }
    return true;
}

bool ExecuteToneMappingKernelSimple(cl_float* inputArray, cl_float* outputArray,  cl_float* tmpArray, const CHDRData HDRData, cl_uint arrayWidth, cl_uint arrayHeight)
{
    cl_int err = CL_SUCCESS;
    // allocate the buffer
    // the same frame is processed
    g_inputBuffer =
        clCreateBuffer
        (
            g_context,
            CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
            zeroCopySizeAlignment(sizeof(cl_float4) * arrayWidth * arrayHeight),
            inputArray,
            NULL
        );
    if (g_inputBuffer == (cl_mem)0)
    {
        printf("ERROR: Failed to create Input Buffer...\n");
        return false;
    }

    SubmitJobsToQueues(arrayWidth,arrayHeight, HDRData);
    if(g_bUseBothDevices)
    {
        // TODO Where processing for errors???
        // flush both queues to get things rolling in parallel
        err = clFlush(g_cmd_queue_pg); // let's flush PG's queue first (before CPU device occupies all the cores with its commands)
        err = clFlush(g_cmd_queue);
        // now let's wait
        err = clWaitForEvents (2, &g_events_list[0]);
    }
    else
    {
        // single-device case is easy
        err = clFinish(g_cmd_queue);
    }
    FrameExecutionStatistics();
    if(g_bUseBothDevices)
    {
        ReleaseSubResources();
        err  = clReleaseEvent(g_events_list[0]);
        err |= clReleaseEvent(g_events_list[1]);
        if (err != CL_SUCCESS)
        {
            printf("ERROR: Could not release events\n");
            return false;
        }
    }
    else
    {
        err  = clReleaseEvent(g_events_list[0]);
        if (err != CL_SUCCESS)
        {
            printf("ERROR: Could not release events\n");
            return false;
        }
    }

    clReleaseMemObject(g_inputBuffer); g_inputBuffer = NULL;
    return true;
}

bool ExecuteToneMappingKernelWithInputFrameUpdate(bool bSwapImageAsync /*emulating the new input being updated in parallel with processing current frame*/,
                                                  cl_float* inputArray, cl_float* outputArray,  cl_float* tmpArray, const CHDRData HDRData, cl_uint arrayWidth, cl_uint arrayHeight)
{
    cl_int err = CL_SUCCESS;
    // emulating the new frame (with juggling by 2 buffers and mirroring values)
    g_inputBuffer = (g_ImageSwapCounter%2) ? g_inputBuffer1 :g_inputBuffer2;//flip-flop of the current input buffer
    SubmitJobsToQueues(arrayWidth,arrayHeight, HDRData);
    if(g_bUseBothDevices)
    {
        // flush both queues to get things rolling in parallel
        err  = clFlush(g_cmd_queue_pg);
        err |= clFlush(g_cmd_queue);
        // if we don't want to do smth on the host in paralell (e.g. no buffers to re-fill/swap), let's immediately wait
        if(!bSwapImageAsync)err |= clWaitForEvents (2, &g_events_list[0]);

    }
    else
    {
        // if we don't want to do smth on the host in paralell (e.g. no buffers to re-fill/swap), let's immediately wait
        if(!bSwapImageAsync) err = clFinish(g_cmd_queue);
    }
    if (err != CL_SUCCESS)
    {
        printf("ERROR: Failed to wait for results...\n");
        return false;
    }

    // let's do smth in the host thread
    // re-fill the input buffer with mirrored input image (just to simulate new input frame)
    if(g_ImageSwapCounter%2)
    {
        for(unsigned int iii = 0; iii < arrayHeight; iii++)
        {
            memcpy((void*)&inputArray[iii*arrayWidth*4], (void*)&tmpArray[(arrayHeight-1-iii)*arrayWidth*4], sizeof(cl_float4)*arrayWidth);
        }
    }
    else
    {
        for(unsigned int iii = 0; iii < arrayHeight; iii++)
        {
            memcpy((void*)&tmpArray[iii*arrayWidth*4], (void*)&inputArray[(arrayHeight-1-iii)*arrayWidth*4], sizeof(cl_float4)*arrayWidth);
        }
    }
    g_ImageSwapCounter++;

    if(bSwapImageAsync)
    {
        // now if we are in async mode let's wait for results (notice buffer swapping/re-filling and OCL commands were executing in parallel)
        err = g_bUseBothDevices ? clWaitForEvents (2, &g_events_list[0]) : clFinish(g_cmd_queue);
        if (err != CL_SUCCESS)
        {
            printf("ERROR: Failed to wait for results (async mode)...\n");
            return false;
        }

    }
    FrameExecutionStatistics();

    if(g_bUseBothDevices)
    {
        ReleaseSubResources();
        err  = clReleaseEvent(g_events_list[0]);
        err |= clReleaseEvent(g_events_list[1]);
        if (err != CL_SUCCESS)
        {
            printf("ERROR: Could not release events\n");
            return false;
        }
    }
    else
    {
        err  = clReleaseEvent(g_events_list[0]);
        if (err != CL_SUCCESS)
        {
            printf("ERROR: Could not release events\n");
            return false;
        }
    }

    return true;
}

// calculate FStops value (HDR parameter) from the arguments
float CalculateFStopsParameter( float powKLow, float kHigh )
{
    float curveBoxWidth = pow( 2.0f, kHigh ) - powKLow;
    float curveBoxHeight = pow( 2.0f, 3.5f )  - powKLow;

    // Initial boundary values
    float fFStopsLow = 0.0f;
    float fFStopsHigh = 100.0f;
    int iterations = 23; // interval bisection iterations

    // Interval bisection to find the final knee function fStops parameter
    for ( int i = 0; i < iterations; i++ )
    {
        float fFStopsMiddle = ( fFStopsLow + fFStopsHigh ) * 0.5f;
        if ( ( curveBoxWidth * fFStopsMiddle + 1.0f ) < exp( curveBoxHeight * fFStopsMiddle ) )
        {
            fFStopsHigh = fFStopsMiddle;
        }
        else
        {
            fFStopsLow = fFStopsMiddle;
        }
    }

    return ( fFStopsLow + fFStopsHigh ) * 0.5f;
}

// main execution routine - perform Tone Mapping post-processing on float4 vectors
int main (int argc, const char** argv)
{
    cl_uint arrayWidth;
    cl_uint arrayHeight;
    int ret = EXIT_SUCCESS; //return code


    // init HDR parameters
    float kLow = -3.0f;
    float kHigh = 7.5f;
    float exposure = 3.0f;
    float gamma = 1.0f;
    float defog = 0.0f;

    try
    {


        CmdParserDeviceType cmd(argc, argv);
        cmd.device_type.setValuePlaceholder("cpu | gpu | cpu+gpu");
        cmd.device_type.setDefaultValue("cpu");
        CmdOption<bool>  param_stat_mode(cmd,0,"iteration-statistic","","print per iteration statistics",!g_bSilentMode);
        CmdOption<bool>  param_swap_image(cmd,0,"input-swaping","","updating the input frame  on each iteration, and compares sync./async. update mode",g_bSwapImage);

        cmd.parse();

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

        if(dev_type == CL_DEVICE_TYPE_GPU)
        {
            g_bRunOnPG = true;
        }
        if(dev_type == (CL_DEVICE_TYPE_GPU|CL_DEVICE_TYPE_CPU))
        {
            g_bUseBothDevices = true;
            g_bRunOnPG = false;
        }
        g_bSwapImage = param_swap_image.getValue();
        g_bSilentMode = !param_stat_mode.getValue();

        g_platform_id = selectPlatform(cmd.platform.getValue());
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


    // fill HDR parameters structure
    CHDRData HDRData;
    HDRData.fGamma = gamma;
    HDRData.fPowGamma = pow(2.0f, -3.5f*gamma);
    HDRData.fDefog = defog;

    HDRData.fPowKLow = pow( 2.0f, kLow );
    HDRData.fPowKHigh = pow( 2.0f, kHigh );
    HDRData.fPow35 = pow(2.0f, 3.5f);
    HDRData.fPowExposure = pow( 2.0f, exposure +  2.47393f );

    // calculate FStops
    HDRData.fFStops = CalculateFStopsParameter(HDRData.fPowKLow, kHigh);
    printf("CalculateFStopsParameter result = %f\n", HDRData.fFStops);

    HDRData.fFStopsInv = 1.0f/HDRData.fFStops;


    // initialize Open CL objects (context, queue, etc.)
    if(!Setup_OpenCL(FULL_PATH("ToneMappingMultiDevice.cl")) )
        return -1;


    // fill input frame
    cl_float* inputArray = 0;
    // read input image
    inputArray = readInput(&arrayWidth, &arrayHeight);
    if(inputArray==0)
        return -1;

    // fill tmp frame
    cl_float* tmpArray = 0;
    if(g_bSwapImage)
    {
        // read input image
        tmpArray = readInput(&arrayWidth, &arrayHeight);
        if(tmpArray==0)
            return -1;
    }


    printf("Input size is %d X %d\n", arrayWidth, arrayHeight);
    size_t size = sizeof(cl_float4) * arrayWidth * arrayHeight;
    size_t alignedSize = zeroCopySizeAlignment(size);
    cl_float* outputArray = (cl_float*)aligned_malloc(alignedSize, g_min_align);
    cl_float* refArray = (cl_float*)aligned_malloc(sizeof(cl_float4) * arrayWidth * arrayHeight, g_min_align);

    // create buffers
    if(g_bSwapImage) // we need 2 buffers (to flip-flop them)
    {
        g_inputBuffer1 =
            clCreateBuffer
            (
                g_context,
                CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
                alignedSize,
                inputArray,
                NULL
            );
        if (g_inputBuffer1 == (cl_mem)0)
        {
            printf("ERROR: Failed to create Input Buffer...\n");
            return -1;
        }
        g_inputBuffer2 =
            clCreateBuffer
            (
                g_context,
                CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
                alignedSize,
                tmpArray,
                NULL
            );
        if (g_inputBuffer2 == (cl_mem)0)
        {
            printf("ERROR: Failed to create Input Buffer #2...\n");
            return -1;
        }
    }

    g_outputBuffer =
        clCreateBuffer
        (
            g_context,
            CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR,
            alignedSize,
            outputArray,
            NULL
        );
    if (g_outputBuffer == (cl_mem)0)
    {
        printf("ERROR: Failed to create Output Buffer...\n");
        return -1;
    }

    if(!g_bSwapImage)
    { // no emulation of input data update
        g_PerformanceCountOverallStart=time_stamp();
        for(int iter = 0; iter < TEST_RUNS; iter++)
        {
            // do tone mapping
            if(!g_bSilentMode)
            {
                printf("Executing OpenCL kernel...\n");
            }
            ExecuteToneMappingKernelSimple(inputArray, outputArray, tmpArray, HDRData, arrayWidth, arrayHeight);

        }
        g_PerformanceCountOverallStop=time_stamp();;
        printf("Overall execution time for %d frames is %.3f ms.\n", TEST_RUNS,
            1000.0f*(float)(g_PerformanceCountOverallStop - g_PerformanceCountOverallStart));
    }
    else
    {
        g_PerformanceCountOverallStart=time_stamp();
        for(int iter = 0; iter < TEST_RUNS; iter++)
        {
            // do kernel execution and frame update in default (sync) mode
            if(!g_bSilentMode)
            {
                printf("Executing OpenCL kernel...\n");
            }
            ExecuteToneMappingKernelWithInputFrameUpdate(false, inputArray, outputArray, tmpArray, HDRData, arrayWidth, arrayHeight);

        }
        g_PerformanceCountOverallStop=time_stamp();
        float fSyncModeTimeInMs = 1000.0f*(float)(g_PerformanceCountOverallStop - g_PerformanceCountOverallStart);
        printf("Overall execution time for %d frames is %.3f ms (sync mode)\n", TEST_RUNS, fSyncModeTimeInMs);

        // now the same stuff in async way
	g_PerformanceCountOverallStart=time_stamp();
        for(int iter = 0; iter < TEST_RUNS; iter++)
        {
            // do kernel execution and frame update in async  mode
            if(!g_bSilentMode)
            {
                printf("Executing OpenCL kernel...\n");
            }
            ExecuteToneMappingKernelWithInputFrameUpdate(true, inputArray, outputArray, tmpArray, HDRData, arrayWidth, arrayHeight);

        }
        g_PerformanceCountOverallStop=time_stamp();
        float fAsyncModeTimeInMs = 1000.0f*(float)(g_PerformanceCountOverallStop - g_PerformanceCountOverallStart);
        printf("Overall execution time for %d frames is %.3f ms (async mode)\n", TEST_RUNS, fAsyncModeTimeInMs);
        if(fSyncModeTimeInMs<fAsyncModeTimeInMs)
            printf("Async mode is nowhere faster than sync, probably we are already bandwidth-limited\n");
        else
            printf("Async mode is %.3f%% faster than sync\n", 100*(fSyncModeTimeInMs/fAsyncModeTimeInMs-1));
    }

    if(!g_bSilentMode)
    {
        printf("Executing reference...\n");
    }
    ExecuteToneMappingReference(inputArray, refArray, &HDRData, arrayWidth, arrayHeight);

    // doing map/unmap to sync the memory content with the host mem pointed by outputArray (this is required by spec)
    void* tmp_ptr = NULL;
    tmp_ptr = clEnqueueMapBuffer(g_cmd_queue, g_outputBuffer, true, CL_MAP_READ, 0, sizeof(cl_float4) * arrayWidth * arrayHeight , 0, NULL, NULL, NULL);
    if(tmp_ptr!=outputArray)
    {
        printf("ERROR: clEnqueueMapBuffer failed to return original pointer\n"); // since we used CL_USE_HOST_PTR we want to operate on the same mem not copy
        return -1;
    }
    // save results in bitmap files
    if(!g_bSilentMode)
    {
        float fTmpFVal = 0.0f;
        cl_uint* outUIntBuf=0;
        outUIntBuf = (cl_uint*)malloc(arrayWidth*arrayHeight*sizeof(cl_uint));
        if(!outUIntBuf)
        {
            printf("Failed to allocate memory for output BMP image!\n");
            return -1;
        }
        for(unsigned int y = 0; y < arrayHeight; y++)
        {
            for(unsigned int x = 0; x < arrayWidth; x++)
            {
                cl_uint uiTmp[4]; // 4 - means 4-channel pixel
                fTmpFVal = (outputArray[(y*arrayWidth+x)*4+0]);
                uiTmp[0] = (cl_uint)(fTmpFVal);

                fTmpFVal = (outputArray[(y*arrayWidth+x)*4+1]);
                uiTmp[1] = (cl_uint)(fTmpFVal);

                fTmpFVal = (outputArray[(y*arrayWidth+x)*4+2]);
                uiTmp[2] = (cl_uint)(fTmpFVal);

                fTmpFVal = (outputArray[(y*arrayWidth+x)*4+3]);
                uiTmp[3] = (cl_uint)(fTmpFVal);    // Alfa

                outUIntBuf[(arrayHeight-1-y)*arrayWidth+x] = 0x000000FF & uiTmp[2];
                outUIntBuf[(arrayHeight-1-y)*arrayWidth+x] |= 0x0000FF00 & ((uiTmp[1]) << 8);
                outUIntBuf[(arrayHeight-1-y)*arrayWidth+x] |= 0x00FF0000 & ((uiTmp[0]) << 16);
                outUIntBuf[(arrayHeight-1-y)*arrayWidth+x] |= 0xFF000000 & ((uiTmp[3]) << 24);
            }
        }
        //----
        SaveImageAsBMP( outUIntBuf, arrayWidth, arrayHeight, "ToneMappingMultiDeviceOutput.bmp");

        for(unsigned int y = 0; y < arrayHeight; y++)
        {
            for(unsigned int x = 0; x < arrayWidth; x++)
            {
                cl_uint uiTmp[4]; // 4 - means 4-channel pixel
                fTmpFVal = (refArray[(y*arrayWidth+x)*4+0]);
                uiTmp[0] = (cl_uint)(fTmpFVal);

                fTmpFVal = (refArray[(y*arrayWidth+x)*4+1]);
                uiTmp[1] = (cl_uint)(fTmpFVal);

                fTmpFVal = (refArray[(y*arrayWidth+x)*4+2]);
                uiTmp[2] = (cl_uint)(fTmpFVal);

                fTmpFVal = (refArray[(y*arrayWidth+x)*4+3]);
                uiTmp[3] = (cl_uint)(fTmpFVal);    // Alfa

                outUIntBuf[(arrayHeight-1-y)*arrayWidth+x] = 0x000000FF & uiTmp[2];
                outUIntBuf[(arrayHeight-1-y)*arrayWidth+x] |= 0x0000FF00 & ((uiTmp[1]) << 8);
                outUIntBuf[(arrayHeight-1-y)*arrayWidth+x] |= 0x00FF0000 & ((uiTmp[0]) << 16);
                outUIntBuf[(arrayHeight-1-y)*arrayWidth+x] |= 0xFF000000 & ((uiTmp[3]) << 24);
            }
        }
        //----
        SaveImageAsBMP( outUIntBuf, arrayWidth, arrayHeight, "ToneMappingMultiDeviceOutputReference.bmp");
        free(outUIntBuf);
    }

    // Do verification
    if(!g_bSilentMode)
    {
        printf("Performing verification...\n");
    }

    bool result = true;
    for(unsigned int i = 0; i < arrayWidth*arrayHeight*4; i++)    // 4 - means 4-channel pixel
    {
        // Compare the data
        if( fabsf(outputArray[i] - refArray[i]) > 0.1f )
        {
            printf("Error at location %d,  outputArray = %f, refArray = %f \n", i, outputArray[i], refArray[i]);
            result = false;
        }
    }
    if(!result)
    {
        printf("ERROR: Verification failed.\n");
    }
    else
    {
        if(!g_bSilentMode)
        {
            printf("Verification succeeded.\n");
        }
    }

    clEnqueueUnmapMemObject(g_cmd_queue, g_outputBuffer, tmp_ptr, 0, NULL, NULL);
    clFinish(g_cmd_queue);



    aligned_free( refArray );
    aligned_free( inputArray );
    aligned_free( outputArray );
    if(g_bSwapImage)
    {
        aligned_free( tmpArray );
    }

    Cleanup_OpenCL();

    if(!result)
    {
        return -1;
    }
    else
    {
        return 0;
    }
}

#pragma warning( pop )
