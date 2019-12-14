// Copyright (c) 2014 Intel Corporation
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

// image padding to avoid boundary handling
#define XPAD 16

// Naive Sobel kernel, directly following Sobel operator definition
__kernel void Sobel_uchar (__global uchar *pSrcImage, __global uchar *pDstImage)
{
    uint dstYStride = get_global_size(0);
    uint dstIndex   = get_global_id(1) * dstYStride + get_global_id(0);
    uint srcYStride = dstYStride + XPAD*2;
    uint srcIndex   = get_global_id(1) * srcYStride + get_global_id(0) + XPAD;

    uint a,		b,		c;
    uint d,	/*center*/  f;
    uint g,		h,		i;

    // Read data in
    a = pSrcImage[srcIndex-1];
    b = pSrcImage[srcIndex];
    c = pSrcImage[srcIndex+1];
    srcIndex += srcYStride;

    d = pSrcImage[srcIndex-1];
    /*center*/
    f = pSrcImage[srcIndex+1];
    srcIndex += srcYStride;

    g = pSrcImage[srcIndex-1];
    h = pSrcImage[srcIndex];
    i = pSrcImage[srcIndex+1];	

    // calculate horizontal and vertical derivative approximations
    uint xVal = c* 1 -              1*a	+
                f* 2 -	/*center*/	2*d	+
                i* 1 -              1*g;

    uint yVal =	g*1 + h*2 + i*1 -
                /*center*/	 
                (a*1 + b*2 + c*1);

    // Write data out
    pDstImage[dstIndex] =  min((uint)255, (uint)sqrt((float)(xVal*xVal + yVal*yVal)));
}

// Image is processed by 4 pixel chunks
// Thread launching overhead reduced by ~4x
// Bandwidth is better utilized by using uchar4 loads and stores
__kernel void Sobel_uchar4 (__global uchar4* pSrcImage, __global uchar4* pDstImage)
{
    uint dstYStride = get_global_size(0);
    uint dstIndex   = get_global_id(1) * dstYStride + get_global_id(0);
    uint srcYStride = dstYStride + (XPAD*2)/4;
    uint srcIndex   = get_global_id(1) * srcYStride + get_global_id(0) + XPAD/4;

    uint a; uint4 b; uint c;
    uint d; uint4 e; uint f;
    uint g; uint4 h; uint i;

    // Read data in	
    a = ((__global uchar*)(pSrcImage+srcIndex))[-1];		
    b = convert_uint4(pSrcImage[srcIndex]);	 
    c = ((__global uchar*)(pSrcImage+srcIndex))[4];
    srcIndex += srcYStride;

    d = ((__global uchar*)(pSrcImage+srcIndex))[-1];
    e = convert_uint4(pSrcImage[srcIndex]);	         
    f = ((__global uchar*)(pSrcImage+srcIndex))[4];
    srcIndex += srcYStride;

    g = ((__global uchar*)(pSrcImage+srcIndex))[-1];
    h = convert_uint4(pSrcImage[srcIndex]);	 
    i = ((__global uchar*)(pSrcImage+srcIndex))[4];

    uint4 xVal, yVal;

    xVal =    (uint4)(b.yzw, c) -   (uint4)(a, b.xyz) + 
            2*(uint4)(e.yzw, f) - 2*(uint4)(d, e.xyz) + 
              (uint4)(h.yzw, i) -   (uint4)(g, h.xyz) ;

    yVal = (uint4)(g, h.xyz) + 2*h + (uint4)(h.yzw, i) -
           (uint4)(a, b.xyz) - 2*b - (uint4)(b.yzw, c) ;

    // Write data out		
    pDstImage[dstIndex] = convert_uchar4( min((float4)255.0f, sqrt(convert_float4(xVal*xVal + yVal*yVal))) );
}

// Further increase chunks to 16x16 blocks and do all math in floats
// Calculations are performed using 16way vectors
// Extra load operations saved by reusing data from previous lines
// Thread launching overhead reduced by ~256x
// Convolution calculation is sped up by using FP operations
__kernel void Sobel_uchar16_to_float16_vload_16 (__global uchar16* pSrcImage, __global uchar16* pDstImage)
{
    uint dstYStride = get_global_size(0);
    uint dstIndex   = 16 * get_global_id(1) * dstYStride + get_global_id(0);
    uint srcYStride = dstYStride + (XPAD*2)/16;
    uint srcIndex   = 16 * get_global_id(1) * srcYStride + get_global_id(0) + XPAD/16;

    float a; float16 b; float c;
    float d; float16 e; float f;
    float g; float16 h; float i;

    // Read data in	for first two lines of a tile
    a = convert_float(((__global uchar*)(pSrcImage+srcIndex))[-1]);
    b = convert_float16(vload16(0, (__global uchar*)(pSrcImage+srcIndex)));	 
    c = convert_float(((__global uchar*)(pSrcImage+srcIndex))[16]);	
    srcIndex += srcYStride;
    d = convert_float(((__global uchar*)(pSrcImage+srcIndex))[-1]);
    e = convert_float16(vload16(0, (__global uchar*)(pSrcImage+srcIndex)));
    f = convert_float(((__global uchar*)(pSrcImage+srcIndex))[16]);

    for(uint k = 0; k < 16; k++)
    {
        // read third line
        srcIndex += srcYStride;
        g = convert_float((            (__global uchar*)(pSrcImage+srcIndex))[-1]);
        h = convert_float16(vload16(0, (__global uchar*)(pSrcImage+srcIndex)));
        i = convert_float((            (__global uchar*)(pSrcImage+srcIndex))[16]);

        float16 xVal, yVal;		

        xVal =       (float16)(b.s123, b.s4567, b.s89abcdef, c) -      (float16)(a, b.s0123, b.s456789ab, b.scde) +
                2.0f*(float16)(e.s123, e.s4567, e.s89abcdef, f) - 2.0f*(float16)(d, e.s0123, e.s456789ab, e.scde) +
                     (float16)(h.s123, h.s4567, h.s89abcdef, i) -      (float16)(g, h.s0123, h.s456789ab, h.scde);

        yVal = (float16)(g, h.s0123, h.s456789ab, h.scde) + 2.0f*h + (float16)(h.s123, h.s4567, h.s89abcdef, i) -
               (float16)(a, b.s0123, b.s456789ab, b.scde) - 2.0f*b - (float16)(b.s123, b.s4567, b.s89abcdef, c);

        // Write data out
        vstore16(convert_uchar16(min((float16)255.0f, sqrt(xVal*xVal + yVal*yVal))), 0, (__global uchar*)(pDstImage+dstIndex));

        // to save load operations, just shift and reuse already loaded data for next iteration
        a = d; b = e; c = f;
        d = g; e = h; f = i;
        dstIndex += dstYStride;
    }
}
