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
#include <mmsystem.h>

// D3D10 includes
#include "dynlink_d3d10.h"

// OpenCL includes
#include <oclUtils.h>
#include <shrQATest.h>
#include <CL/cl_d3d10_ext.h>
#include <CL/cl_ext.h>

// Project specific includes
#include "oclSimpleD3D10Texture.h"
#include "rendercheck_d3d10.h"

int *pArgc = NULL;
char **pArgv = NULL;

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
HRESULT InitD3D10( HWND hWnd, bool &noD3DAvailable );
HRESULT InitCL(int argc, const char** argv);
HRESULT InitTextures( );
HRESULT ReleaseTextures();
HRESULT DeviceLostHandler();
LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void RunKernels();
void DrawScene();
void RunCL();
void TriggerFPSUpdate();
void Cleanup(int iExitCode=0);
void (*pCleanup)(int) = &Cleanup;
void TestNoDX9();


//-----------------------------------------------------------------------------
// Program main
//-----------------------------------------------------------------------------
int main(int argc, char** argv)
{
	pArgc = &argc;
	pArgv = argv;

	shrQAStart(argc, argv);

    // start logs 
    shrSetLogFileName ("oclSimpleD3D10Texture.txt");
    shrLog("%s Starting...\n\n", argv[0]); 

	bool bCheckD3D10 = dynlinkLoadD3D10API();
    // If D3D10 is not present, print an error message and then quit
    if (!bCheckD3D10) {
        printf("%s did not detect a D3D10 device, exiting...\n", SDK_name);
        dynlinkUnloadD3D10API();
		// Cleanup and leave
		Cleanup (EXIT_SUCCESS);
    }

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
                      "OpenCL/D3D10 Texture InterOP", NULL };
    RegisterClassEx( &wc );

	int xBorder = ::GetSystemMetrics(SM_CXSIZEFRAME);
	int yMenu = ::GetSystemMetrics(SM_CYMENU);
	int yBorder = ::GetSystemMetrics(SM_CYSIZEFRAME);

    // Create the application's window (padding by window border for uniform BB sizes across OSs)
    HWND hWnd = CreateWindow( wc.lpszClassName, "OpenCL/D3D10 Texture InterOP",
                              WS_OVERLAPPEDWINDOW, 0, 0, g_WindowWidth + 2*xBorder, g_WindowHeight+ 2*yBorder+yMenu,
                              NULL, NULL, wc.hInstance, NULL );

    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    // init fps timer
    shrDeltaT (1);

	bool noD3DAvailable;
	HRESULT hr = InitD3D10(hWnd, noD3DAvailable);
	// let's give-up if D3D failed. But we will write "succeed"
	if(FAILED(hr))
	{
		// Unregister windows class
		UnregisterClass( wc.lpszClassName, wc.hInstance );
		//
		// and exit with SUCCESS if the reason is unavailability
		//
        Cleanup(noD3DAvailable ? EXIT_SUCCESS : EXIT_FAILURE);
	}
	if(FAILED(InitCL(argc, (const char **)argv)) || FAILED(InitTextures()))
	{
        Cleanup(EXIT_FAILURE);
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

					const char *ref_image_path = "ref_oclSimpleD3D10Texture.ppm";
					const char *cur_image_path = "oclSimpleD3D10Texture.ppm";

					// Save a reference of our current test run image
					CheckRenderD3D10::ActiveRenderTargetToPPM(g_pd3dDevice,cur_image_path);

					// compare to offical reference image, printing PASS or FAIL.
					g_bPassed = CheckRenderD3D10::PPMvsPPM(cur_image_path,ref_image_path,argv[0],MAX_EPSILON, 0.15f);

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
// Name: InitD3D10()
// Desc: Initializes Direct3D
//-----------------------------------------------------------------------------
HRESULT InitD3D10(HWND hWnd, bool &noD3DAvailable) 
{
    HRESULT hr = S_OK;
	noD3DAvailable = false;

    // Select our adapter
    IDXGIAdapter* pCLCapableAdapter = NULL;
    {
        // iterate through the candidate adapters
        IDXGIFactory *pFactory;
        hr = sFnPtr_CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)(&pFactory) );
        if(FAILED(hr))
		{
			noD3DAvailable = true;
			return hr;
		}

        for (UINT adapter = 0; !pCLCapableAdapter; ++adapter)
        {
            // get a candidate DXGI adapter
            IDXGIAdapter* pAdapter = NULL;
            hr = pFactory->EnumAdapters(adapter, &pAdapter);
            if (FAILED(hr))
            {
                break;
            }
			// TODO: check here if the adapter is ok for CL
            {
                // if so, mark it as the one against which to create our d3d10 device
                pCLCapableAdapter = pAdapter;
				break;
            }
            pAdapter->Release();
        }
        pFactory->Release();
    }
    if(!pCLCapableAdapter)
        if(FAILED(hr))
		{
			noD3DAvailable = true;
			return E_FAIL;
		}

    // Set up the structure used to create the device and swapchain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory( &sd, sizeof(sd) );
    sd.BufferCount = 1;
    sd.BufferDesc.Width = g_WindowWidth;
    sd.BufferDesc.Height = g_WindowHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;

    // Create device and swapchain
    hr = sFnPtr_D3D10CreateDeviceAndSwapChain( 
        pCLCapableAdapter, 
        D3D10_DRIVER_TYPE_HARDWARE, 
        NULL, 
        0,
        D3D10_SDK_VERSION, 
        &sd, 
        &g_pSwapChain, 
        &g_pd3dDevice);
    if(FAILED(hr))
	{
		noD3DAvailable = true;
		return hr;
	}
    pCLCapableAdapter->Release();
	pCLCapableAdapter = NULL;

    // Create a render target view of the swapchain
    ID3D10Texture2D* pBuffer;
    hr = g_pSwapChain->GetBuffer( 0, __uuidof( ID3D10Texture2D ), (LPVOID*)&pBuffer);
    if(FAILED(hr))
		return hr;

    hr = g_pd3dDevice->CreateRenderTargetView(pBuffer, NULL, &g_pSwapChainRTV);
    pBuffer->Release();
    if(FAILED(hr))
		return hr;

    g_pd3dDevice->OMSetRenderTargets(1, &g_pSwapChainRTV, NULL);

    // Setup the viewport
    D3D10_VIEWPORT vp;
    vp.Width = g_WindowWidth;
    vp.Height = g_WindowHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    g_pd3dDevice->RSSetViewports( 1, &vp );


    // Setup the effect
    {
        ID3D10Blob* pCompiledEffect;
        ID3D10Blob* pErrors = NULL;
        hr = sFnPtr_D3D10CompileEffectFromMemory(
            (void*)g_simpleEffectSrc,
            sizeof(g_simpleEffectSrc),
            NULL,
            NULL, // pDefines
            NULL, // pIncludes
            0, // HLSL flags
            0, // FXFlags
            &pCompiledEffect,
            &pErrors);

        if( pErrors ) 
        {
            LPVOID l_pError = NULL;
            l_pError = pErrors->GetBufferPointer(); // then cast to a char* to see it in the locals window 
            shrLog("Compilation error: \n %s", (char*) l_pError);
        }
		if(FAILED(hr))
			return hr;
        
        hr = sFnPtr_D3D10CreateEffectFromMemory(
            pCompiledEffect->GetBufferPointer(),
            pCompiledEffect->GetBufferSize(),
            0, // FXFlags
            g_pd3dDevice,
            NULL,
            &g_pSimpleEffect);
        pCompiledEffect->Release();
            
        g_pSimpleTechnique = g_pSimpleEffect->GetTechniqueByName( "Render" );

        g_pvQuadRect = g_pSimpleEffect->GetVariableByName("g_vQuadRect")->AsVector();
        g_pUseCase = g_pSimpleEffect->GetVariableByName("g_UseCase")->AsScalar();

        g_pTexture2D = g_pSimpleEffect->GetVariableByName("g_Texture2D")->AsShaderResource();
        g_pTexture3D = g_pSimpleEffect->GetVariableByName("g_Texture3D")->AsShaderResource();
        g_pTextureCube = g_pSimpleEffect->GetVariableByName("g_TextureCube")->AsShaderResource();


        // Setup  no Input Layout
        g_pd3dDevice->IASetInputLayout(0);
        g_pd3dDevice->IASetPrimitiveTopology( D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
    }

    D3D10_RASTERIZER_DESC rasterizerState;
    rasterizerState.FillMode = D3D10_FILL_SOLID;
    rasterizerState.CullMode = D3D10_CULL_FRONT;
    rasterizerState.FrontCounterClockwise = false;
    rasterizerState.DepthBias = false;
    rasterizerState.DepthBiasClamp = 0;
    rasterizerState.SlopeScaledDepthBias = 0;
    rasterizerState.DepthClipEnable = false;
    rasterizerState.ScissorEnable = false;
    rasterizerState.MultisampleEnable = false;
    rasterizerState.AntialiasedLineEnable = false;
    g_pd3dDevice->CreateRasterizerState( &rasterizerState, &g_pRasterState );
    g_pd3dDevice->RSSetState( g_pRasterState );

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
	// Initialize extension functions for D3D10
	//
	INITPFN(clGetDeviceIDsFromD3D10NV);
	INITPFN(clCreateFromD3D10BufferNV);
	INITPFN(clCreateFromD3D10Texture2DNV);
	INITPFN(clCreateFromD3D10Texture3DNV);
	INITPFN(clEnqueueAcquireD3D10ObjectsNV);
	INITPFN(clEnqueueReleaseD3D10ObjectsNV);

	// Query the OpenCL device that would be good for the current D3D device
	// We need to take the one that is on the same Gfx card.
	
	// Get the device ids for the adapter 
    cl_device_id cdDevice; 
    cl_uint num_devices = 0;

    ciErrNum = clGetDeviceIDsFromD3D10NV(
        cpPlatform,
        CL_D3D10_DEVICE_NV,
        g_pd3dDevice,
        CL_PREFERRED_DEVICES_FOR_D3D10_NV,
        1,
        &cdDevice,
        &num_devices);

	if (ciErrNum == -1) {
		shrLog("No OpenCL device available that supports D3D10, exiting...\n");
		Cleanup (EXIT_SUCCESS);
	} else {
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
	}

	cl_context_properties props[] = 
    {
        CL_CONTEXT_D3D10_DEVICE_NV, (cl_context_properties)g_pd3dDevice, 
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
// Name: InitTextures()
// Desc: Initializes Direct3D Textures (allocation and initialization)
//-----------------------------------------------------------------------------
HRESULT InitTextures()
{
    //
    // create the D3D resources we'll be using
    //
    // 2D texture
    {
        g_texture_2d.width  = 256;
        g_texture_2d.pitch  = g_texture_2d.width; // for now, let's set pitch == to width
        g_texture_2d.height = 256;

        D3D10_TEXTURE2D_DESC desc;
        ZeroMemory( &desc, sizeof(D3D10_TEXTURE2D_DESC) );
        desc.Width = g_texture_2d.width;
        desc.Height = g_texture_2d.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D10_USAGE_DEFAULT;
        desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
        if (FAILED(g_pd3dDevice->CreateTexture2D( &desc, NULL, &g_texture_2d.pTexture)))
            return E_FAIL;

        if (FAILED(g_pd3dDevice->CreateShaderResourceView(g_texture_2d.pTexture, NULL, &g_texture_2d.pSRView)) )
            return E_FAIL;

        g_pTexture2D->SetResource( g_texture_2d.pSRView );

		// Create the OpenCL part
		g_texture_2d.clTexture = clCreateFromD3D10Texture2DNV(
			cxGPUContext,
			0,
			g_texture_2d.pTexture,
			0,
			&ciErrNum);
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);

#ifdef USE_STAGING_BUFFER
		// Memory Setup : allocate 4 bytes (RGBA) pixels
		// Create the intermediate buffers in which OpenCL will do the rendering
		// then we will blit the result back to the texture that we will have mapped to OpenCL area
		g_texture_2d.clMem = clCreateBuffer(cxGPUContext, CL_MEM_READ_ONLY, 4 * g_texture_2d.pitch * g_texture_2d.height, NULL, &ciErrNum);
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
#endif
    }
    // 3D texture
    {
        g_texture_vol.width  = 64;
        g_texture_vol.height = 64;
        g_texture_vol.depth  = 64;
        g_texture_vol.pitch  = g_texture_vol.width;
        g_texture_vol.pitchslice  = g_texture_vol.pitch * g_texture_vol.height;

        D3D10_TEXTURE3D_DESC desc;
        ZeroMemory( &desc, sizeof(D3D10_TEXTURE3D_DESC) );
        desc.Width = g_texture_vol.width;
        desc.Height = g_texture_vol.height;
        desc.Depth = g_texture_vol.depth;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Usage = D3D10_USAGE_DEFAULT;
        desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;

        if (FAILED(g_pd3dDevice->CreateTexture3D( &desc, NULL, &g_texture_vol.pTexture)))
            return E_FAIL;

        if (FAILED(g_pd3dDevice->CreateShaderResourceView(g_texture_vol.pTexture, NULL, &g_texture_vol.pSRView)) )
            return E_FAIL;

        g_pTexture3D->SetResource( g_texture_vol.pSRView );
									g_texture_vol.clTexture = clCreateFromD3D10Texture3DNV(
									cxGPUContext,
									0,
									g_texture_vol.pTexture,
									0, //Miplevel
									&ciErrNum);

		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
		// Create the staging buffer for the volume texture because it is impossible to directly write into it
		g_texture_vol.clMem = clCreateBuffer(cxGPUContext, CL_MEM_READ_ONLY, 4 * g_texture_vol.pitch * g_texture_vol.height * g_texture_vol.depth, NULL, &ciErrNum);
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
    }

    // cube texture
    {
        g_texture_cube.size = 64;
        g_texture_cube.pitch  = g_texture_cube.size;

        D3D10_TEXTURE2D_DESC desc;
        ZeroMemory( &desc, sizeof(D3D10_TEXTURE2D_DESC) );
        desc.Width = g_texture_cube.size;
        desc.Height = g_texture_cube.size;
        desc.MipLevels = 1;
        desc.ArraySize = 6;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D10_USAGE_DEFAULT;
        desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D10_RESOURCE_MISC_TEXTURECUBE ;

        if (FAILED(g_pd3dDevice->CreateTexture2D( &desc, NULL, &g_texture_cube.pTexture)))
            return E_FAIL;

        D3D10_SHADER_RESOURCE_VIEW_DESC SRVDesc;
        ZeroMemory( &SRVDesc, sizeof(SRVDesc) );
        SRVDesc.Format = desc.Format;
        SRVDesc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURECUBE;
        SRVDesc.TextureCube.MipLevels = desc.MipLevels;
        SRVDesc.TextureCube.MostDetailedMip = 0;

        if (FAILED(g_pd3dDevice->CreateShaderResourceView(g_texture_cube.pTexture, &SRVDesc, &g_texture_cube.pSRView)) )
            return E_FAIL;

        g_pTextureCube->SetResource( g_texture_cube.pSRView );
	// Create the OpenCL part
	for(int i=0; i<6; i++)
	{
		g_texture_cube.clTexture[i] = clCreateFromD3D10Texture2DNV(
			cxGPUContext,
			0,
			g_texture_cube.pTexture,
			(D3DCUBEMAP_FACES)i, // face
			&ciErrNum);
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);

#ifdef USE_STAGING_BUFFER
		g_texture_cube.clMem[i] = clCreateBuffer(cxGPUContext, CL_MEM_READ_ONLY, 4 * g_texture_cube.pitch * g_texture_cube.size, NULL, &ciErrNum);
		oclCheckErrorEX(ciErrNum, CL_SUCCESS, pCleanup);
#endif
	}
    }

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
        if (g_texture_2d.pSRView != NULL) g_texture_2d.pSRView->Release();
        if (g_texture_2d.pTexture != NULL) g_texture_2d.pTexture->Release();
        if (g_texture_cube.pSRView != NULL) g_texture_cube.pSRView->Release();
        if (g_texture_cube.pTexture != NULL) g_texture_cube.pTexture->Release();
        if (g_texture_vol.pSRView != NULL) g_texture_vol.pSRView->Release();
        if (g_texture_vol.pSRView != NULL) g_texture_vol.pTexture->Release();
        if (g_pInputLayout != NULL) g_pInputLayout->Release();
        if (g_pSimpleEffect != NULL) g_pSimpleEffect->Release();
        if (g_pSwapChainRTV != NULL) g_pSwapChainRTV->Release();
        if (g_pSwapChain != NULL) g_pSwapChain->Release();
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
    ciErrNum = clEnqueueAcquireD3D10ObjectsNV(
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
    if(eventType != CL_COMMAND_ACQUIRE_D3D10_OBJECTS_NV)
	{
		shrLog("event type is not CL_COMMAND_ACQUIRE_D3D10_OBJECTS_NV !\n");
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
    ciErrNum = clEnqueueReleaseD3D10ObjectsNV(
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
    if(eventType != CL_COMMAND_RELEASE_D3D10_OBJECTS_NV)
	{
		shrLog("event type is not CL_COMMAND_RELEASE_D3D10_OBJECTS_NV !\n");
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
		szGlobalWorkSize[0] = shrRoundUp((int)szLocalWorkSize[0], g_texture_2d.pitch);
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
		szGlobalWorkSize[0] = shrRoundUp((int)szLocalWorkSize[0], g_texture_vol.pitch);
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

		//// ONLY staging buffer works, for volume texture
		//// do the copy here
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
		szGlobalWorkSize[0] = shrRoundUp((int)szLocalWorkSize[0], g_texture_cube.pitch);
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
//    - this function restores all of the OpenCL/D3D resources and contexts
//-----------------------------------------------------------------------------
HRESULT RestoreContextResources()
{
    // Reinitialize D3D10 resources, CL resources/contexts
    InitCL(0, NULL);
    InitTextures();

    return S_OK;
}

//-----------------------------------------------------------------------------
//! Draw the final result on the screen
//-----------------------------------------------------------------------------
void DrawScene()
{
    // Clear the backbuffer to a black color
    float ClearColor[4] = {0.5f, 0.5f, 0.6f, 1.0f};
    g_pd3dDevice->ClearRenderTargetView( g_pSwapChainRTV, ClearColor);

    //
    // draw the 2d texture
    //
    g_pUseCase->SetInt( 0 );
    float quadRect[4] = { -0.9f, -0.9f, 0.7f , 0.7f };
    g_pvQuadRect->SetFloatVector( (float* ) &quadRect);
    g_pSimpleTechnique->GetPassByIndex(0)->Apply(0);
    g_pd3dDevice->Draw( 4, 0 );

    //
    // draw a slice the 3d texture
    //
    g_pUseCase->SetInt( 1 );
    quadRect[1] = 0.1f;
    g_pvQuadRect->SetFloatVector( (float* ) &quadRect);
    g_pSimpleTechnique->GetPassByIndex(0)->Apply(0);
    g_pd3dDevice->Draw( 4, 0 );

    //
    // draw the 6 faces of the cube texture
    //
    float faceRect[4] = { -0.1f, -0.9f, 0.5f, 0.5f };
    for ( int f = 0; f < 6; f++ )
    {
        if (f == 3)
        {   
            faceRect[0] += 0.55f ;
            faceRect[1] = -0.9f ;
        }
        g_pUseCase->SetInt( 2 + f );
        g_pvQuadRect->SetFloatVector( (float* ) &faceRect);
        g_pSimpleTechnique->GetPassByIndex(0)->Apply(0);
        g_pd3dDevice->Draw( 4, 0 );
        faceRect[1] += 0.6f ;
    }

    // Present the backbuffer contents to the display
    g_pSwapChain->Present( 0, 0);

}

//-----------------------------------------------------------------------------
// Name: Cleanup()
// Desc: Releases all previously initialized objects
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

    //... TODO: add more cleanup

    // release the D3D resources we created
    ReleaseTextures();
    if (g_pd3dDevice != NULL) g_pd3dDevice->Release();
    dynlinkUnloadD3D10API();

    // finalize logs and leave
    shrQAFinishExit2(bQATest, *pArgc, (const char **)pArgv, (iExitCode == EXIT_SUCCESS) ? QA_PASSED : QA_FAILED); 
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
