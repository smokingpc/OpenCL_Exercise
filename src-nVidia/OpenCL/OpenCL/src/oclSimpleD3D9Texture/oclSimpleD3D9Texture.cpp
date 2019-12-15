/*
 * Copyright 1993-2010 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */

// this define tells to use intermediate buffer
// the direct write to the texture doesn't seem to work... for now...
//#define USE_STAGING_BUFFER

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WINDOWS_LEAN_AND_MEAN
#include <windows.h>
#endif

// D3D includes
#include <d3dx9.h>

// OpenCL includes
#include <oclUtils.h>
#include <shrQATest.h>
#include <CL/cl_d3d9_ext.h>
#include <CL/cl_ext.h>

// Project specific includes
#include "oclSimpleD3D9Texture.h"
#include "rendercheck_d3d9.h"

int *pArgc = NULL;
char **pArgv = NULL;

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
HRESULT InitD3D9( HWND hWnd );
HRESULT InitCL(int argc, const char** argv);
HRESULT InitTextures( );
HRESULT ReleaseTextures();
HRESULT RegisterD3D9ResourceWithCL();
HRESULT DeviceLostHandler();
void	RunKernels();
HRESULT DrawScene();
void	RunCL();
LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void Cleanup(int iExitCode=0);
void (*pCleanup)(int) = &Cleanup;
void TestNoDX9();
void TriggerFPSUpdate();

//-----------------------------------------------------------------------------
// Program main
//-----------------------------------------------------------------------------
int main(int argc, char** argv)
{
	pArgc = &argc;
	pArgv = argv;

	// start logs 
    shrQAStart(argc, argv);
    shrSetLogFileName ("oclSimpleD3D9Texture.txt");
    shrLog("%s Starting...\n\n", argv[0]); 

    // process command line arguments
    if (argc > 1) 
    {
        bQATest   = shrCheckCmdLineFlag(argc, (const char **)argv, "qatest");
        bNoPrompt = shrCheckCmdLineFlag(argc, (const char **)argv, "noprompt");
    }

	//
	// create window
	//
    // Register the window class
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, MsgProc, 0L, 0L,
                      GetModuleHandle(NULL), NULL, NULL, NULL, NULL,
                      "OpenCL/D3D9 Texture InterOP", NULL };
    RegisterClassEx( &wc );

	int xBorder = ::GetSystemMetrics(SM_CXSIZEFRAME);
	int yMenu = ::GetSystemMetrics(SM_CYMENU);
	int yBorder = ::GetSystemMetrics(SM_CYSIZEFRAME);

    // Create the application's window (padding by window border for uniform BB sizes across OSs)
    HWND hWnd = CreateWindow( wc.lpszClassName, "OpenCL/D3D9 Texture InterOP",
                              WS_OVERLAPPEDWINDOW, 0, 0, g_WindowWidth + 2*xBorder, g_WindowHeight+ 2*yBorder+yMenu,
                              NULL, NULL, wc.hInstance, NULL );

    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    // init fps timer
    shrDeltaT (1);

    // Initialize Direct3D
    if( SUCCEEDED( InitD3D9(hWnd) ) &&
        SUCCEEDED( InitCL(argc, (const char **)argv) ) &&
		SUCCEEDED( InitTextures() ) )
	{
        if (!g_bDeviceLost) 
		{
            RegisterD3D9ResourceWithCL();
        }
	}

	//
	// the main loop
	//
    while(false == g_bDone) 
	{
        RunCL();
        DrawScene();

		//
		// handle I/O
		//
		MSG msg;
		ZeroMemory( &msg, sizeof(msg) );
		while( msg.message!=WM_QUIT )
		{
			if( PeekMessage( &msg, NULL, 0U, 0U, PM_REMOVE ) )
			{
				TranslateMessage( &msg );
				DispatchMessage( &msg );
			}
			else
			{
				RunCL();
				DrawScene();

				if(bQATest)
				{
					for(int count=0;count<g_iFrameToCompare;count++)
					{
						RunCL();
						DrawScene();
					}

					const char *ref_image_path = "ref_oclSimpleD3D9Texture.ppm";
					const char *cur_image_path = "oclSimpleD3D9Texture.ppm";

					// Save a reference of our current test run image
					CheckRenderD3D9::BackbufferToPPM(g_pD3DDevice,cur_image_path);

					// compare to offical reference image, printing PASS or FAIL.
					g_bPassed = CheckRenderD3D9::PPMvsPPM(cur_image_path,ref_image_path,argv[0],MAX_EPSILON, 0.15f);

					PostQuitMessage(0);
					g_bDone = true;
				}
			}
		}
    };

	// Unregister windows class
	UnregisterClass( wc.lpszClassName, wc.hInstance );

    // Cleanup and leave
    Cleanup (g_bPassed ? EXIT_SUCCESS : EXIT_FAILURE);
}

//-----------------------------------------------------------------------------
// Name: TriggerFPSUpdate()
// Desc: Triggers reset of fps vars at transition 
//-----------------------------------------------------------------------------
void TriggerFPSUpdate()
{
    iFrameCount = 0; 
    shrDeltaT(1);
    iFramesPerSec = 1;
    iFrameTrigger = 2;
}

//-----------------------------------------------------------------------------
// Name: InitD3D9()
// Desc: Initializes Direct3D9
//-----------------------------------------------------------------------------
HRESULT InitD3D9(HWND hWnd) 
{
	// Create the D3D object.
    if( NULL == ( g_pD3D = Direct3DCreate9( D3D_SDK_VERSION ) ) )
	{
        shrLog("No Direct3D9 device available\n");
        Cleanup(EXIT_SUCCESS);
	}

    // Find the first CL capable device
    for(g_iAdapter = 0; g_iAdapter < g_pD3D->GetAdapterCount(); g_iAdapter++)
    {
		D3DCAPS9 caps;
		if (FAILED(g_pD3D->GetDeviceCaps(g_iAdapter, D3DDEVTYPE_HAL, &caps)))
			// Adapter doesn't support Direct3D
			continue;

        if(FAILED(g_pD3D->GetAdapterIdentifier(g_iAdapter, 0, &g_adapter_id)))
			return E_FAIL;
		break;
    }
    // we check to make sure we have found a OpenCL-compatible D3D device to work on
    if(g_iAdapter == g_pD3D->GetAdapterCount() ) 
    {
        shrLog("No OpenCL-compatible Direct3D9 device available\n");
		// destroy the D3D device
		g_pD3D->Release();
        Cleanup(EXIT_SUCCESS);
    }

	// Create the D3D Display Device
    RECT                  rc;       GetClientRect(hWnd,&rc);
    D3DDISPLAYMODE        d3ddm;    g_pD3D->GetAdapterDisplayMode(g_iAdapter, &d3ddm);
    D3DPRESENT_PARAMETERS d3dpp;    ZeroMemory( &d3dpp, sizeof(d3dpp) );
    d3dpp.Windowed               = TRUE;
    d3dpp.BackBufferCount        = 1;
    d3dpp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow          = hWnd;
	d3dpp.BackBufferWidth	     = g_WindowWidth;
    d3dpp.BackBufferHeight       = g_WindowHeight;

    d3dpp.BackBufferFormat       = d3ddm.Format;

    
	if (FAILED (g_pD3D->CreateDevice (g_iAdapter, D3DDEVTYPE_HAL, hWnd, 
									  D3DCREATE_HARDWARE_VERTEXPROCESSING, 
									  &d3dpp, &g_pD3DDevice) ))
		return E_FAIL;	

	// We clear the back buffer
	g_pD3DDevice->BeginScene();
	g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);	
	g_pD3DDevice->EndScene();

	return S_OK;
}

//-----------------------------------------------------------------------------
// Name: CreateKernelProgram()
// Desc: Creates OpenCL program and kernel instances
//-----------------------------------------------------------------------------
HRESULT CreateKernelProgram(
	const char *exepath, const char *clName, const char *clPtx, const char *kernelEntryPoint,
	cl_program			&cpProgram,
	cl_kernel			&ckKernel )
{
    // Program Setup
    size_t program_length;
    const char* source_path = shrFindFilePath(clName, exepath);
    char *source = oclLoadProgSource(source_path, "", &program_length);
    oclCheckErrorEX(source != NULL, shrTRUE, pCleanup);

    // create the program
    cpProgram = clCreateProgramWithSource(cxGPUContext, 1,(const char **) &source, &program_length, &ciErrNum);
    oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
    free(source);

    // build the program
#ifdef USE_STAGING_BUFFER
	static char *opts = "-cl-fast-relaxed-math -DUSE_STAGING_BUFFER";
#else
	static char *opts = "-cl-fast-relaxed-math";
#endif
    ciErrNum = clBuildProgram(cpProgram, 0, NULL, opts, NULL, NULL);
    if (ciErrNum != CL_SUCCESS)
    {
        // write out standard error, Build Log and PTX, then cleanup and exit
        shrLogEx(LOGBOTH | ERRORMSG, ciErrNum, STDERROR);
        oclLogBuildInfo(cpProgram, oclGetFirstDev(cxGPUContext));
        oclLogPtx(cpProgram, oclGetFirstDev(cxGPUContext), clPtx);
        Cleanup(EXIT_FAILURE); 
    }

    // create the kernel
    ckKernel = clCreateKernel(cpProgram, kernelEntryPoint, &ciErrNum);
    if (!ckKernel)
    {
        Cleanup(EXIT_FAILURE); 
    }

    // set the args values
	return ciErrNum ? E_FAIL : S_OK;
}

//-----------------------------------------------------------------------------
// Name: InitCL()
// Desc: Get platform and devices and create context and queues
//-----------------------------------------------------------------------------
HRESULT InitCL(int argc, const char** argv)
{
    cl_platform_id	cpPlatform;

    //Get the NVIDIA platform
    ciErrNum = oclGetPlatformID(&cpPlatform);
    oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);

    //
	// Initialize extension functions for D3D9
	//
    INITPFN(clGetDeviceIDsFromD3D9NV);
    INITPFN(clCreateFromD3D9VertexBufferNV);
    INITPFN(clCreateFromD3D9IndexBufferNV);
    INITPFN(clCreateFromD3D9SurfaceNV);
    INITPFN(clCreateFromD3D9TextureNV);
    INITPFN(clCreateFromD3D9CubeTextureNV);
    INITPFN(clCreateFromD3D9VolumeTextureNV);
    INITPFN(clEnqueueAcquireD3D9ObjectsNV);
    INITPFN(clEnqueueReleaseD3D9ObjectsNV);
	INITPFN(clGetDeviceIDsFromD3D9NV);

	// Query the OpenCL device that would be good for the current D3D device
	// We need to take the one that is on the same Gfx card.

	// Get the device ids for the adapter 
    cl_device_id cdDevice; 
    cl_uint num_devices = 0;

    ciErrNum = clGetDeviceIDsFromD3D9NV(
        cpPlatform,
        CL_D3D9_DEVICE_NV,//CL_D3D9_ADAPTER_NAME_NV,
        g_pD3DDevice,//adapterName,
        CL_PREFERRED_DEVICES_FOR_D3D9_NV, //CL_ALL_DEVICES_FOR_D3D9_NV,
        1,
        &cdDevice,
        &num_devices);

	if (ciErrNum == -1) {
		shrLog("No OpenCL device available that supports D3D9, exiting...\n");
        Cleanup (EXIT_SUCCESS);
	} else {
	    oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
	}

	cl_context_properties props[] = 
    {
        CL_CONTEXT_D3D9_DEVICE_NV, (cl_context_properties)g_pD3DDevice, 
        CL_CONTEXT_PLATFORM, (cl_context_properties)cpPlatform, 
        0
    };
    cxGPUContext = clCreateContext(props, 1, &cdDevice, NULL, NULL, &ciErrNum);
    oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);

	// Log device used 
	shrLog("Device: ");
    oclPrintDevName(LOGBOTH, cdDevice);
    shrLog("\n");

    // create a command-queue
    cqCommandQueue = clCreateCommandQueue(cxGPUContext, cdDevice, 0, &ciErrNum);
    oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);

	CreateKernelProgram(argv[0], "texture_2d.cl", "texture_2d.ptx", "cl_kernel_texture_2d", cpProgram_tex2d, ckKernel_tex2d);
	CreateKernelProgram(argv[0], "texture_cube.cl", "texture_cube.ptx", "cl_kernel_texture_cube", cpProgram_texcube, ckKernel_texcube);
	CreateKernelProgram(argv[0], "texture_volume.cl", "texture_volume.ptx", "cl_kernel_texture_volume", cpProgram_texvolume, ckKernel_texvolume);

	return S_OK;
}

//-----------------------------------------------------------------------------
// Name: RegisterD3D9ResourceWithCL()
// Desc: 
//-----------------------------------------------------------------------------
HRESULT RegisterD3D9ResourceWithCL()
{
    return S_OK;
}

//-----------------------------------------------------------------------------
// Name: InitTextures()
// Desc: Initializes Direct3D Textures (allocation and initialization)
//-----------------------------------------------------------------------------
HRESULT InitTextures()
{
	//
	// create the D3D resources we'll be using
	//

	// 2D texture
	g_texture_2d.width  = 256;
	g_texture_2d.height = 256;
	g_texture_2d.pitch = 256;
	if (FAILED(g_pD3DDevice->CreateTexture(g_texture_2d.width, g_texture_2d.height, 1, D3DUSAGE_DYNAMIC,
                                           D3DFMT_A8R8G8B8/*D3DFMT_A32B32G32R32F*/, D3DPOOL_DEFAULT, &g_texture_2d.pTexture, NULL) ))
	{
		return E_FAIL;
	}
	D3DLOCKED_RECT r;
	HRESULT hr = g_texture_2d.pTexture->LockRect(0, &r, NULL, 0);
	unsigned long *data = (unsigned long *)r.pBits;
	for(int i=0; i< 256*256; i++)
	{
		*data = 0xFF00FFFF;
		data++;
	}
	g_texture_2d.pTexture->UnlockRect(0);
	// Create the OpenCL part
    g_texture_2d.clTexture = clCreateFromD3D9TextureNV(
        cxGPUContext,
        0,
        g_texture_2d.pTexture,
        0,//miplevel
        &ciErrNum);
    oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
	//
	// Optional Check...
	//
    IDirect3DResource9* clResource = NULL;
    ciErrNum = clGetMemObjectInfo(
        g_texture_2d.clTexture,
        CL_MEM_D3D9_RESOURCE_NV,
        sizeof(clResource),
        &clResource,
        NULL);
	assert(clResource == g_texture_2d.pTexture);

#ifdef USE_STAGING_BUFFER
    // Memory Setup : allocate 4 bytes (RGBA) pixels
	// Create the intermediate buffers in which OpenCL will do the rendering
	// then we will blit the result back to the texture that we will have mapped to OpenCL area
	g_texture_2d.clMem = clCreateBuffer(cxGPUContext, CL_MEM_READ_ONLY, 4 * g_texture_2d.width * g_texture_2d.height, NULL, &ciErrNum);
    oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
#endif

	// cube texture
	g_texture_cube.size = 64;
	g_texture_cube.pitch = 64;
	if (FAILED(g_pD3DDevice->CreateCubeTexture(g_texture_cube.size, 1, D3DUSAGE_DYNAMIC, 
												D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, 
												&g_texture_cube.pTexture, NULL) ))
	{
		return E_FAIL;
	}
	// Create the OpenCL part
	for(int i=0; i<6; i++)
	{
		g_texture_cube.clTexture[i] = clCreateFromD3D9CubeTextureNV(
			cxGPUContext,
			0,
			g_texture_cube.pTexture,
			(D3DCUBEMAP_FACES)i, // face
			0, // miplevel
			&ciErrNum);
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);

#ifdef USE_STAGING_BUFFER
		g_texture_cube.clMem[i] = clCreateBuffer(cxGPUContext, CL_MEM_READ_ONLY, 4 * g_texture_cube.size * g_texture_cube.size, NULL, &ciErrNum);
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
#endif
	}
	// 3D texture
	g_texture_vol.width  = 16;
	g_texture_vol.height = 16;
	g_texture_vol.depth  = 8;
	g_texture_vol.pitch = 16;
	g_texture_vol.pitchslice = g_texture_vol.pitch * g_texture_vol.height;
	
	if (FAILED(g_pD3DDevice->CreateVolumeTexture(	g_texture_vol.width, g_texture_vol.height, 
													g_texture_vol.depth, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, 
													D3DPOOL_DEFAULT, &g_texture_vol.pTexture, NULL) ))
	{
		return E_FAIL;
	}
    g_texture_vol.clTexture = clCreateFromD3D9VolumeTextureNV(
        cxGPUContext,
        0,
        g_texture_vol.pTexture,
        0, //Miplevel
        &ciErrNum);
	oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);

	g_texture_vol.clMem = clCreateBuffer(cxGPUContext, CL_MEM_READ_ONLY, 4 * g_texture_vol.width * g_texture_vol.height * g_texture_vol.depth, NULL, &ciErrNum);
    oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);


	return S_OK;
}

//-----------------------------------------------------------------------------
// Name: ReleaseTextures()
// Desc: Release Direct3D Textures (free-ing)
//-----------------------------------------------------------------------------
HRESULT ReleaseTextures()
{
	//
	// clean up Direct3D
	// 
	{
		// release the resources we created
		if(g_texture_2d.pTexture) g_texture_2d.pTexture->Release();
		if(g_texture_cube.pTexture) g_texture_cube.pTexture->Release();
		if(g_texture_vol.pTexture) g_texture_vol.pTexture->Release();
    }

    return S_OK;
}
//-----------------------------------------------------------------------------
// Name: AcquireTexturesForOpenCL()
// Desc: Acquire textures for OpenCL
//-----------------------------------------------------------------------------
void AcquireTexturesForOpenCL()
{
	cl_event event;
	cl_mem memToAcquire[6+1+1];
	memToAcquire[0] = g_texture_2d.clTexture;
	memToAcquire[1] = g_texture_vol.clTexture;
	memToAcquire[2] = g_texture_cube.clTexture[0];
	memToAcquire[3] = g_texture_cube.clTexture[1];
	memToAcquire[4] = g_texture_cube.clTexture[2];
	memToAcquire[5] = g_texture_cube.clTexture[3];
	memToAcquire[6] = g_texture_cube.clTexture[4];
	memToAcquire[7] = g_texture_cube.clTexture[5];
    // do the acquire
    ciErrNum = clEnqueueAcquireD3D9ObjectsNV(
        cqCommandQueue,
        6 + 1 + 1, //cube map + tex2d + volume texture
        memToAcquire,
        0,
        NULL,
        &event);
	oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);

    // make sure the event type is correct
    cl_uint eventType = 0;
    ciErrNum = clGetEventInfo(
        event,
        CL_EVENT_COMMAND_TYPE,
        sizeof(eventType),
        &eventType,
        NULL);
	oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
    if(eventType != CL_COMMAND_ACQUIRE_D3D9_OBJECTS_NV)
	{
		shrLog("event type is not CL_COMMAND_ACQUIRE_D3D9_OBJECTS_NV !\n");
	}
    ciErrNum = clReleaseEvent(event);
	oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
}

//-----------------------------------------------------------------------------
// Name: ReleaseTexturesFromOpenCL()
// Desc: Release Textures from OpenCL
//-----------------------------------------------------------------------------
void ReleaseTexturesFromOpenCL()
{
	cl_event event;
	cl_mem memToAcquire[6+1+1];
	memToAcquire[0] = g_texture_2d.clTexture;
	memToAcquire[1] = g_texture_vol.clTexture;
	memToAcquire[2] = g_texture_cube.clTexture[0];
	memToAcquire[3] = g_texture_cube.clTexture[1];
	memToAcquire[4] = g_texture_cube.clTexture[2];
	memToAcquire[5] = g_texture_cube.clTexture[3];
	memToAcquire[6] = g_texture_cube.clTexture[4];
	memToAcquire[7] = g_texture_cube.clTexture[5];
    // do the acquire
    ciErrNum = clEnqueueReleaseD3D9ObjectsNV(
        cqCommandQueue,
        6 + 1 + 1, //cube map + tex2d + volume texture
        memToAcquire,
        0,
        NULL,
        &event);
	oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);

    // make sure the event type is correct
    cl_uint eventType = 0;
    ciErrNum = clGetEventInfo(
        event,
        CL_EVENT_COMMAND_TYPE,
        sizeof(eventType),
        &eventType,
        NULL);
	oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
    if(eventType != CL_COMMAND_RELEASE_D3D9_OBJECTS_NV)
	{
		shrLog("event type is not CL_COMMAND_RELEASE_D3D9_OBJECTS_NV !\n");
	}
    ciErrNum = clReleaseEvent(event);
	oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
}

//-----------------------------------------------------------------------------
//! Run the CL part of the computation
//-----------------------------------------------------------------------------
void RunKernels()
{
    static float t = 0.0f;

	// ----------------------------------------------------------------
    // populate the 2d texture
    {
		// set global and local work item dimensions
		szLocalWorkSize[0] = 16;
		szLocalWorkSize[1] = 16;
		szGlobalWorkSize[0] = shrRoundUp((int)szLocalWorkSize[0], g_texture_2d.width);
		szGlobalWorkSize[1] = shrRoundUp((int)szLocalWorkSize[1], g_texture_2d.height);

		// set the args values
#ifdef USE_STAGING_BUFFER
		ciErrNum |= clSetKernelArg(ckKernel_tex2d, 0, sizeof(g_texture_2d.clMem), (void *) &(g_texture_2d.clMem));
#else
		ciErrNum |= clSetKernelArg(ckKernel_tex2d, 0, sizeof(g_texture_2d.clTexture), (void *) &(g_texture_2d.clTexture));
#endif
		ciErrNum |= clSetKernelArg(ckKernel_tex2d, 1, sizeof(g_texture_2d.clTexture), (void *) &(g_texture_2d.clTexture));
		ciErrNum |= clSetKernelArg(ckKernel_tex2d, 2, sizeof(g_texture_2d.width), &g_texture_2d.width);
		ciErrNum |= clSetKernelArg(ckKernel_tex2d, 3, sizeof(g_texture_2d.height), &g_texture_2d.height);
		ciErrNum |= clSetKernelArg(ckKernel_tex2d, 4, sizeof(g_texture_2d.pitch), &g_texture_2d.pitch);
		ciErrNum |= clSetKernelArg(ckKernel_tex2d, 5, sizeof(t), &t);
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
	    
		// launch computation kernel
		ciErrNum |= clEnqueueNDRangeKernel(cqCommandQueue, ckKernel_tex2d, 2, NULL,
										  szGlobalWorkSize, szLocalWorkSize, 
										 0, NULL, NULL);
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);

#ifdef USE_STAGING_BUFFER
		size_t dst[3] = { 0, 0, 0};
		size_t region[3] = { g_texture_2d.width, g_texture_2d.height, 1};
		ciErrNum |= clEnqueueCopyBufferToImage(cqCommandQueue,
                   g_texture_2d.clMem		/* src_buffer */,
                   g_texture_2d.clTexture	/* dst_image */, 
                   0						/* src_offset */,
                   dst						/* dst_origin[3] */,
                   region					/* region[3] */, 
                   0						/* num_events_in_wait_list */,
                   NULL						/* event_wait_list */,
                   NULL						/* event */);
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
#endif
    }
	// ----------------------------------------------------------------
    // populate the volume texture
    {
		// set global and local work item dimensions
		szLocalWorkSize[0] = 16;
		szLocalWorkSize[1] = 16;
		szGlobalWorkSize[0] = shrRoundUp((int)szLocalWorkSize[0], g_texture_vol.width);
		szGlobalWorkSize[1] = shrRoundUp((int)szLocalWorkSize[1], g_texture_vol.height);

		// set the args values
		ciErrNum |= clSetKernelArg(ckKernel_texvolume, 0, sizeof(g_texture_vol.clMem), (void *) &(g_texture_vol.clMem));
		ciErrNum |= clSetKernelArg(ckKernel_texvolume, 1, sizeof(g_texture_vol.width), &g_texture_vol.width);
		ciErrNum |= clSetKernelArg(ckKernel_texvolume, 2, sizeof(g_texture_vol.height), &g_texture_vol.height);
		ciErrNum |= clSetKernelArg(ckKernel_texvolume, 3, sizeof(g_texture_vol.depth), &g_texture_vol.depth);
		ciErrNum |= clSetKernelArg(ckKernel_texvolume, 4, sizeof(g_texture_vol.pitch), &g_texture_vol.pitch);
		ciErrNum |= clSetKernelArg(ckKernel_texvolume, 5, sizeof(g_texture_vol.pitchslice), &g_texture_vol.pitchslice);
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
	    
		// launch computation kernel
		ciErrNum |= clEnqueueNDRangeKernel(cqCommandQueue, ckKernel_texvolume, 2, NULL,
										  szGlobalWorkSize, szLocalWorkSize, 
										 0, NULL, NULL);
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);

		// ONLY staging buffer works, for volume texture
		// do the copy here
		size_t dst[3] = { 0, 0, 0};
		size_t region[3] = { g_texture_vol.width, g_texture_vol.height, g_texture_vol.depth};
		ciErrNum |= clEnqueueCopyBufferToImage(cqCommandQueue,
                   g_texture_vol.clMem		/* src_buffer */,
                   g_texture_vol.clTexture	/* dst_image */, 
                   0						/* src_offset */,
                   dst						/* dst_origin[3] */,
                   region					/* region[3] */, 
                   0						/* num_events_in_wait_list */,
                   NULL						/* event_wait_list */,
                   NULL						/* event */);
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
    }

	// ----------------------------------------------------------------
    // populate the faces of the cube map
    for (int face = 0; face < 6; ++face)
    {
		// set global and local work item dimensions
		szLocalWorkSize[0] = 16;
		szLocalWorkSize[1] = 16;
		szGlobalWorkSize[0] = shrRoundUp((int)szLocalWorkSize[0], g_texture_cube.size);
		szGlobalWorkSize[1] = shrRoundUp((int)szLocalWorkSize[1], g_texture_cube.size);

		// set the args values
#ifdef USE_STAGING_BUFFER
		ciErrNum |= clSetKernelArg(ckKernel_texcube, 0, sizeof(g_texture_cube.clMem[face]), (void *) &(g_texture_cube.clMem[face]));
#else
		ciErrNum |= clSetKernelArg(ckKernel_texcube, 0, sizeof(g_texture_cube.clTexture[face]), (void *) &(g_texture_cube.clTexture[face]));
#endif
		ciErrNum |= clSetKernelArg(ckKernel_texcube, 1, sizeof(g_texture_cube.size), &g_texture_cube.size);
		ciErrNum |= clSetKernelArg(ckKernel_texcube, 2, sizeof(g_texture_cube.pitch), &g_texture_cube.pitch);
		ciErrNum |= clSetKernelArg(ckKernel_texcube, 3, sizeof(int), &face);
		ciErrNum |= clSetKernelArg(ckKernel_texcube, 4, sizeof(t), &t);
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
	    
		// launch computation kernel
		ciErrNum |= clEnqueueNDRangeKernel(cqCommandQueue, ckKernel_texcube, 2, NULL,
										  szGlobalWorkSize, szLocalWorkSize, 
										 0, NULL, NULL);
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);

#ifdef USE_STAGING_BUFFER
		size_t dst[3] = { 0, 0, 0};
		size_t region[3] = { g_texture_cube.size, g_texture_cube.size, 1};
		ciErrNum |= clEnqueueCopyBufferToImage(cqCommandQueue,
                   g_texture_cube.clMem[face]/* src_buffer */,
                   g_texture_cube.clTexture[face]/* dst_image */, 
                   0						/* src_offset */,
                   dst						/* dst_origin[3] */,
                   region					/* region[3] */, 
                   0						/* num_events_in_wait_list */,
                   NULL						/* event_wait_list */,
                   NULL						/* event */);
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
#endif
    }
    t += 0.1f;
}

//-----------------------------------------------------------------------------
//! RestoreContextResources
//    - this function restores all of the CL/D3D resources and contexts
//-----------------------------------------------------------------------------
HRESULT RestoreContextResources()
{
    // Reinitialize D3D9 resources, CL resources/contexts
    InitCL(0, NULL);
    InitTextures();
    RegisterD3D9ResourceWithCL();

    return S_OK;
}

//-----------------------------------------------------------------------------
//! DeviceLostHandler
//    - this function handles reseting and initialization of the D3D device
//      in the event this Device gets Lost
//-----------------------------------------------------------------------------
HRESULT DeviceLostHandler()
{
    HRESULT hr = S_OK;

    shrLog("-> Starting DeviceLostHandler() \n");
    // test the cooperative level to see if it's okay
    // to render
    if (FAILED(hr = g_pD3DDevice->TestCooperativeLevel()))
    {
        shrLog("TestCooperativeLevel = %08x failed, will attempt to reset\n", hr);

        // if the device was truly lost, (i.e., a fullscreen device just lost focus), wait
        // until we g_et it back

        if (hr == D3DERR_DEVICELOST) {
            shrLog("TestCooperativeLevel = %08x DeviceLost, will retry next call\n", hr);
            return S_OK;
        }

        // eventually, we will g_et this return value,
        // indicating that we can now reset the device
        if (hr == D3DERR_DEVICENOTRESET)
        {
            shrLog("TestCooperativeLevel = %08x will try to RESET the device\n", hr);
            // if we are windowed, read the desktop mode and use the same format for 
            // the back buffer; this effectively turns off color conversion

            if (g_bWindowed)
            {
                g_pD3D->GetAdapterDisplayMode( g_iAdapter, &g_d3ddm );
                g_d3dpp.BackBufferFormat = g_d3ddm.Format;
            }

            // now try to reset the device
            if (FAILED(hr = g_pD3DDevice->Reset(&g_d3dpp))) {
                shrLog("TestCooperativeLevel = %08x RESET device FAILED\n", hr);
                return hr;
            } else {
                shrLog("TestCooperativeLevel = %08x RESET device SUCCESS!\n", hr);

                // This is a common function we use to restore all hardware resources/state
                RestoreContextResources();

                shrLog("TestCooperativeLevel = %08x INIT device SUCCESS!\n", hr);

                // we have acquired the device
                g_bDeviceLost = false;
            }
        }
    }
    return hr;
}

//-----------------------------------------------------------------------------
//! Draw the final result on the screen
//-----------------------------------------------------------------------------
HRESULT DrawScene()
{
    HRESULT hr = S_OK;

    if (g_bDeviceLost) 
    {
        if ( FAILED(hr = DeviceLostHandler()) ) {
            shrLog("DeviceLostHandler FAILED returned %08x\n", hr);
            return hr;
        }
    }

    if (!g_bDeviceLost) 
    {
	    //
	    // we will use this index and vertex data throughout
	    //
	    unsigned int IB[6] = 
	    {
		    0,1,2,
		    0,2,3,
	    };
	    struct VertexStruct
	    {
		    float position[3];
		    float texture[3];
	    };

	    // 
	    // initialize the scene
	    //
	    D3DVIEWPORT9 viewport_window = {0, 0, 672, 192, 0, 1};
	    g_pD3DDevice->SetViewport(&viewport_window);
	    g_pD3DDevice->BeginScene();
	    g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);	
	    g_pD3DDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	    g_pD3DDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
	    g_pD3DDevice->SetFVF(D3DFVF_XYZ|D3DFVF_TEX1|D3DFVF_TEXCOORDSIZE3(0));

	    //
	    // draw the 2d texture
	    //
	    VertexStruct VB[4] = 
	    {
		    {  {-1,-1,0,}, {0,0,0,},  },
		    {  { 1,-1,0,}, {1,0,0,},  },
		    {  { 1, 1,0,}, {1,1,0,},  },
		    {  {-1, 1,0,}, {0,1,0,},  },
	    };
	    D3DVIEWPORT9 viewport = {32, 32, 256, 256, 0, 1};
	    g_pD3DDevice->SetViewport(&viewport);
	    g_pD3DDevice->SetTexture(0,g_texture_2d.pTexture);
        g_pD3DDevice->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, IB, D3DFMT_INDEX32, VB, sizeof(VertexStruct) );

	    //
	    // draw the Z-positive side of the cube texture
	    //
	    VertexStruct VB_Zpos[4] = 
	    {
		    {  {-1,-1,0,}, {-1,-1, 0.5f,},  },
		    {  { 1,-1,0,}, { 1,-1, 0.5f,},  },
		    {  { 1, 1,0,}, { 1, 1, 0.5f,},  },
		    {  {-1, 1,0,}, {-1, 1, 0.5f,},  },
	    };
	    viewport.Y += viewport.Height + 32;
	    g_pD3DDevice->SetViewport(&viewport);
	    g_pD3DDevice->SetTexture(0,g_texture_cube.pTexture);
        g_pD3DDevice->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, IB, D3DFMT_INDEX32, VB_Zpos, sizeof(VertexStruct) );

	    //
	    // draw the Z-negative side of the cube texture
	    //
	    VertexStruct VB_Zneg[4] = 
	    {
		    {  {-1,-1,0,}, { 1,-1,-0.5f,},  },
		    {  { 1,-1,0,}, {-1,-1,-0.5f,},  },
		    {  { 1, 1,0,}, {-1, 1,-0.5f,},  },
		    {  {-1, 1,0,}, { 1, 1,-0.5f,},  },
	    };
	    viewport.X += viewport.Width + 32;
	    g_pD3DDevice->SetViewport(&viewport);
	    g_pD3DDevice->SetTexture(0,g_texture_cube.pTexture);
        g_pD3DDevice->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, IB, D3DFMT_INDEX32, VB_Zneg, sizeof(VertexStruct) );

	    //
	    // draw a slice the volume texture
	    //
	    VertexStruct VB_Zslice[4] = 
	    {
		    {  {-1,-1,0,}, {0,0,0,},  },
		    {  { 1,-1,0,}, {1,0,0,},  },
		    {  { 1, 1,0,}, {1,1,1,},  },
		    {  {-1, 1,0,}, {0,1,1,},  },
	    };	
	    viewport.Y -= viewport.Height + 32;
	    g_pD3DDevice->SetViewport(&viewport);
	    g_pD3DDevice->SetTexture(0,g_texture_vol.pTexture);
        g_pD3DDevice->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, IB, D3DFMT_INDEX32, VB_Zslice, sizeof(VertexStruct) );

	    //
	    // end the scene
	    //
	    g_pD3DDevice->EndScene();
	    hr = g_pD3DDevice->Present(NULL, NULL, NULL, NULL);

        if (hr == D3DERR_DEVICELOST) {
            shrLog("DrawScene Present = %08x detected D3D DeviceLost\n", hr);
            g_bDeviceLost = true;

            ReleaseTextures();
            Cleanup(1);
        }
    }
    return hr;
}

//-----------------------------------------------------------------------------
// Name: RunCL()
// Desc: Launches the CL kernels to fill in the texture data
//-----------------------------------------------------------------------------
void RunCL()
{
	//
	// map the resources we've registered so we can access them in cl
	// - it is most efficient to map and unmap all resources in a single call,
	//   and to have the map/unmap calls be the boundary between using the GPU
	//   for Direct3D and cl
	//

    if (!g_bDeviceLost) {
		//
		// Transfer ownership from D3D to OpenCL
		//
		AcquireTexturesForOpenCL();
	    //
	    // run kernels which will populate the contents of those textures
	    //
	    RunKernels();
	    //
	    // give back the ownership to D3D
	    //
		ReleaseTexturesFromOpenCL();
    }
}

//-----------------------------------------------------------------------------
// Name: MsgProc()
// Desc: The window's message handler
//-----------------------------------------------------------------------------
static LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        case WM_KEYDOWN:
            if(wParam==VK_ESCAPE) 
			{
				g_bDone = true;
                Cleanup();
	            PostQuitMessage(0);
				return 0;
			}
            break;
        case WM_DESTROY:
			g_bDone = true;
            Cleanup();
            PostQuitMessage(0);
            return 0;
        case WM_PAINT:
            ValidateRect(hWnd, NULL);
            return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

//-----------------------------------------------------------------------------
// Name: Cleanup()
// Desc:  Clean up and exit
//-----------------------------------------------------------------------------
void Cleanup(int iExitCode)
{
    // Cleanup allocated objects
    shrLog("\nStarting Cleanup...\n\n");
	if(ckKernel_tex2d)clReleaseKernel(ckKernel_tex2d); 
	if(ckKernel_texcube)clReleaseKernel(ckKernel_texcube); 
	if(ckKernel_texvolume)clReleaseKernel(ckKernel_texvolume); 
    if(cpProgram_tex2d)clReleaseProgram(cpProgram_tex2d);
    if(cpProgram_texcube)clReleaseProgram(cpProgram_texcube);
    if(cpProgram_texvolume)clReleaseProgram(cpProgram_texvolume);
    if(cqCommandQueue)clReleaseCommandQueue(cqCommandQueue);
    if(cxGPUContext)clReleaseContext(cxGPUContext);

    // release the D3D resources we created
	ReleaseTextures();
	if (g_pD3DDevice != NULL) g_pD3DDevice->Release();
	if (g_pD3D != NULL) g_pD3D->Release();	

    //... TODO: add more cleanup

    // finalize logs and leave
    shrQAFinishExit2(bQATest, *pArgc, (const char **)pArgv, (iExitCode == EXIT_SUCCESS) ? QA_PASSED : QA_FAILED); 
}
