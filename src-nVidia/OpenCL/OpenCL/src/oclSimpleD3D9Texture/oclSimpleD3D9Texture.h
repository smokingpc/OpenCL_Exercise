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

#define MAX_EPSILON 25
#define D3D9_SHARING_EXTENSION "cl_nv_d3d9_sharing"

static char *SDK_name = "simpleD3D9Texture";

clGetDeviceIDsFromD3D9NV_fn           clGetDeviceIDsFromD3D9NV = NULL;
clCreateFromD3D9VertexBufferNV_fn     clCreateFromD3D9VertexBufferNV = NULL;
clCreateFromD3D9IndexBufferNV_fn      clCreateFromD3D9IndexBufferNV = NULL;
clCreateFromD3D9SurfaceNV_fn          clCreateFromD3D9SurfaceNV = NULL;
clCreateFromD3D9TextureNV_fn          clCreateFromD3D9TextureNV = NULL;
clCreateFromD3D9CubeTextureNV_fn      clCreateFromD3D9CubeTextureNV = NULL;
clCreateFromD3D9VolumeTextureNV_fn    clCreateFromD3D9VolumeTextureNV = NULL;
clEnqueueAcquireD3D9ObjectsNV_fn      clEnqueueAcquireD3D9ObjectsNV = NULL;
clEnqueueReleaseD3D9ObjectsNV_fn      clEnqueueReleaseD3D9ObjectsNV = NULL;

#define INITPFN(x) \
    x = (x ## _fn)clGetExtensionFunctionAddress(#x);\
	if(!x) { shrLog("failed getting " #x); Cleanup(EXIT_FAILURE); }

// CL objects
cl_context			cxGPUContext;
cl_command_queue	cqCommandQueue;
cl_device_id		device;
cl_uint				uiNumDevsUsed = 1;          // Number of devices used in this sample 
cl_program			cpProgram_tex2d;
cl_program			cpProgram_texcube;
cl_program			cpProgram_texvolume;
cl_kernel			ckKernel_tex2d;
cl_kernel			ckKernel_texcube;
cl_kernel			ckKernel_texvolume;
size_t				szGlobalWorkSize[2];
size_t				szLocalWorkSize[2];
cl_mem				cl_pbos[2] = {0,0};
cl_int				ciErrNum;

// Timer and fps vars
int					iFrameCount = 0;                // FPS count for averaging
int					iFrameTrigger = 90;             // FPS trigger for sampling
int					iFramesPerSec = 0;              // frames per second
int					iTestSets = 3;                  // # of loop set retriggers before auto exit when bNoPrompt = shrTrue

// app configuration parms
const char* cProcessor [] = {"OpenCL GPU", "Host C++ CPU"};
int iProcFlag = 0;                  // 0 = GPU, 1 = CPU
shrBOOL bNoPrompt = shrFALSE;		// false = normal GL loop, true = Finite period of GL loop (a few seconds)
shrBOOL bQATest = shrFALSE;			// false = normal GL loop, true = run No-GL test sequence
int		g_iFrameToCompare = 10;

bool                  g_bDone   = false;
bool				  g_bPassed = true;
IDirect3D9		    * g_pD3D; // Used to create the D3DDevice
unsigned int          g_iAdapter;
IDirect3DDevice9    * g_pD3DDevice;
D3DADAPTER_IDENTIFIER9 g_adapter_id;

D3DDISPLAYMODE        g_d3ddm;    
D3DPRESENT_PARAMETERS g_d3dpp;    

bool                  g_bWindowed    = true;
bool                  g_bDeviceLost  = false;

const unsigned int    g_WindowWidth  = 720;
const unsigned int    g_WindowHeight = 720;

// Data structure for 2D texture shared between DX9 and CL
struct
{
	IDirect3DTexture9*	pTexture;
	cl_mem				clTexture;
	cl_mem				clMem;
	unsigned int		pitch;
	unsigned int		width;
	unsigned int		height;	
} g_texture_2d;

// Data structure for cube texture shared between DX9 and CL
struct
{
	IDirect3DCubeTexture9* pTexture;
	cl_mem				clTexture[6];
	cl_mem				clMem[6];
	unsigned int		pitch;
	unsigned int		size;
} g_texture_cube;

// Data structure for volume textures shared between DX9 and CL
struct
{
	IDirect3DVolumeTexture9* pTexture;
	cl_mem				clTexture;
	cl_mem				clMem;
	unsigned int		pitch;
	unsigned int		pitchslice;
	unsigned int		width;
	unsigned int		height;
	unsigned int		depth;
} g_texture_vol;