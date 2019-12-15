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

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------

#define MAX_EPSILON 10
#define D3D10_SHARING_EXTENSION "cl_nv_d3d10_sharing"

static char *SDK_name = "simpleD3D10Texture";

ID3D10Device*           g_pd3dDevice = NULL; // Our rendering device
IDXGISwapChain*         g_pSwapChain = NULL; // The swap chain of the window
ID3D10RenderTargetView* g_pSwapChainRTV = NULL; //The Render target view on the swap chain ( used for clear)
ID3D10RasterizerState*  g_pRasterState = NULL;

ID3D10InputLayout*      g_pInputLayout = NULL;
ID3D10Effect*           g_pSimpleEffect = NULL;
ID3D10EffectTechnique*  g_pSimpleTechnique = NULL;
ID3D10EffectVectorVariable* g_pvQuadRect = NULL;
ID3D10EffectScalarVariable* g_pUseCase = NULL;
ID3D10EffectShaderResourceVariable* g_pTexture2D = NULL;
ID3D10EffectShaderResourceVariable* g_pTexture3D = NULL;
ID3D10EffectShaderResourceVariable* g_pTextureCube = NULL;

static const char g_simpleEffectSrc[] =
    "float4 g_vQuadRect; \n" \
    "int g_UseCase; \n" \
    "Texture2D g_Texture2D; \n" \
    "Texture3D g_Texture3D; \n" \
    "TextureCube g_TextureCube; \n" \
    "\n" \
    "SamplerState samLinear{ \n" \
    "    Filter = MIN_MAG_LINEAR_MIP_POINT; \n" \
    "};\n" \
    "\n" \
    "struct Fragment{ \n" \
    "    float4 Pos : SV_POSITION;\n" \
    "    float3 Tex : TEXCOORD0; };\n" \
    "\n" \
    "Fragment VS( uint vertexId : SV_VertexID )\n" \
    "{\n" \
    "    Fragment f;\n" \
    "    f.Tex = float3( 0.f, 0.f, 0.f); \n"\
    "    if (vertexId == 1) f.Tex.x = 1.f; \n"\
    "    else if (vertexId == 2) f.Tex.y = 1.f; \n"\
    "    else if (vertexId == 3) f.Tex.xy = float2(1.f, 1.f); \n"\
    "    \n" \
    "    f.Pos = float4( g_vQuadRect.xy + f.Tex * g_vQuadRect.zw, 0, 1);\n" \
    "    \n" \
    "    if (g_UseCase == 1) { \n"\
    "        if (vertexId == 1) f.Tex.z = 0.5f; \n"\
    "        else if (vertexId == 2) f.Tex.z = 0.5f; \n"\
    "        else if (vertexId == 3) f.Tex.z = 1.f; \n"\
    "    } \n" \
    "    else if (g_UseCase >= 2) { \n"\
    "        f.Tex.xy = f.Tex.xy * 2.f - 1.f; \n"\
    "    } \n" \
    "    return f;\n" \
    "}\n" \
    "\n" \
    "float4 PS( Fragment f ) : SV_Target\n" \
    "{\n" \
    "    if (g_UseCase == 0) return g_Texture2D.Sample( samLinear, f.Tex.xy ); \n" \
    "    else if (g_UseCase == 1) return g_Texture3D.Sample( samLinear, f.Tex ); \n" \
    "    else if (g_UseCase == 2) return g_TextureCube.Sample( samLinear, float3(f.Tex.xy, 1.0) ); \n" \
    "    else if (g_UseCase == 3) return g_TextureCube.Sample( samLinear, float3(f.Tex.xy, -1.0) ); \n" \
    "    else if (g_UseCase == 4) return g_TextureCube.Sample( samLinear, float3(1.0, f.Tex.xy) ); \n" \
    "    else if (g_UseCase == 5) return g_TextureCube.Sample( samLinear, float3(-1.0, f.Tex.xy) ); \n" \
    "    else if (g_UseCase == 6) return g_TextureCube.Sample( samLinear, float3(f.Tex.x, 1.0, f.Tex.y) ); \n" \
    "    else if (g_UseCase == 7) return g_TextureCube.Sample( samLinear, float3(f.Tex.x, -1.0, f.Tex.y) ); \n" \
    "    else return float4(f.Tex, 1);\n" \
    "}\n" \
    "\n" \
    "technique10 Render\n" \
    "{\n" \
    "    pass P0\n" \
    "    {\n" \
    "        SetVertexShader( CompileShader( vs_4_0, VS() ) );\n" \
    "        SetGeometryShader( NULL );\n" \
    "        SetPixelShader( CompileShader( ps_4_0, PS() ) );\n" \
    "    }\n" \
    "}\n" \
    "\n";

// testing/tracing function used pervasively in tests.  if the condition is unsatisfied
// then spew and fail the function immediately (doing no cleanup)
#define AssertOrQuit(x) \
    if (!(x)) \
    { \
        shrLog("Assert unsatisfied in %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__); \
        return 1; \
    }


clGetDeviceIDsFromD3D10NV_fn        clGetDeviceIDsFromD3D10NV      = NULL;
clCreateFromD3D10BufferNV_fn		clCreateFromD3D10BufferNV      = NULL;
clCreateFromD3D10Texture2DNV_fn		clCreateFromD3D10Texture2DNV   = NULL;
clCreateFromD3D10Texture3DNV_fn     clCreateFromD3D10Texture3DNV   = NULL;
clEnqueueAcquireD3D10ObjectsNV_fn	clEnqueueAcquireD3D10ObjectsNV = NULL;
clEnqueueReleaseD3D10ObjectsNV_fn	clEnqueueReleaseD3D10ObjectsNV = NULL;

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
int					iFrameCount   = 0;                // FPS count for averaging
int					iFrameTrigger = 90;             // FPS trigger for sampling
int					iFramesPerSec = 0;              // frames per second
int					iTestSets     = 3;                  // # of loop set retriggers before auto exit when bNoPrompt = shrTrue

// app configuration parms
const char*           cProcessor [] = {"OpenCL GPU", "Host C++ CPU"};
int                   iProcFlag = 0;                  // 0 = GPU, 1 = CPU
shrBOOL               bNoPrompt = shrFALSE;		// false = normal GL loop, true = Finite period of GL loop (a few seconds)
shrBOOL               bQATest   = shrFALSE;			// false = normal GL loop, true = run No-GL test sequence
int		              g_iFrameToCompare = 10;

bool                  g_bDone   = false;
bool				  g_bPassed = true;
unsigned int          g_iAdapter;
D3DADAPTER_IDENTIFIER9 g_adapter_id;

D3DDISPLAYMODE        g_d3ddm;    
D3DPRESENT_PARAMETERS g_d3dpp;    

bool                  g_bWindowed    = true;

const unsigned int    g_WindowWidth  = 720;
const unsigned int    g_WindowHeight = 720;

// Data structure for 2D texture shared between DX9 and CL
struct
{
    ID3D10Texture2D			*pTexture;
    ID3D10ShaderResourceView *pSRView;
	cl_mem				clTexture;
	cl_mem				clMem;
	unsigned int		pitch;
	unsigned int		width;
	unsigned int		height;	
} g_texture_2d;

// Data structure for cube texture shared between DX9 and CL
struct
{
    ID3D10Texture2D			*pTexture;
    ID3D10ShaderResourceView *pSRView;
	cl_mem				clTexture[6];
	cl_mem				clMem[6];
	unsigned int		pitch;
	unsigned int		size;
} g_texture_cube;

// Data structure for volume textures shared between DX9 and CL
struct
{
    ID3D10Texture3D			*pTexture;
    ID3D10ShaderResourceView *pSRView;
	cl_mem				clTexture;
	cl_mem				clMem;
	unsigned int		pitch;
	unsigned int		pitchslice;
	unsigned int		width;
	unsigned int		height;
	unsigned int		depth;
} g_texture_vol;
