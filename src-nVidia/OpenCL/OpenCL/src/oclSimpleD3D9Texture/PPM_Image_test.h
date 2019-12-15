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

//
//  Utility funcs to wrap up savings a surface or the back buffer as a PPM file
//	In addition, wraps up a threshold comparision of two PPMs.
//
//	These functions are designed to be used to implement an automated QA testing for SDK samples.
//
//	Author: Bryan Dudash
//  Email: sdkfeedback@nvidia.com
//
// Copyright (c) NVIDIA Corporation. All rights reserved.
////////////////////////////////////////////////////////////////////////////////

#include <fstream>
#include <iostream>

const unsigned int PGMHeaderSize = 0x40;
#define MIN_EPSILON_ERROR 1e-3f

////////////////////////////////////////////////////////////////////////////// 
//! Compare two arrays of arbitrary type       
//! @return  true if \a reference and \a data are identical, otherwise false
//! @param reference  handle to the reference data / gold image
//! @param data       handle to the computed data
//! @param len        number of elements in reference and data
//! @param epsilon    epsilon to use for the comparison
//! @param epsilon    threshold % of (# of bytes) for pass/fail
//////////////////////////////////////////////////////////////////////////////
template<class T, class S>
bool compareDataAsFloatThreshold( const T* reference, const T* data, const unsigned int len, 
                    const S epsilon, const float threshold) 
{
    if( epsilon < 0)
		return false;

    // If we set epsilon to be 0, let's set a minimum threshold
    float max_error = max( (float)epsilon, MIN_EPSILON_ERROR );
    int error_count = 0;
    bool result = true;

    for( unsigned int i = 0; i < len; ++i) {
        float diff = fabs((float)reference[i] - (float)data[i]);
        bool comp = (diff < max_error);
        result &= comp;

        if( ! comp) 
        {
            error_count++;
#ifdef _DEBUG
		if (error_count < 50) {
            printf("\n    ERROR(epsilon=%4.3f), i=%d, (ref)0x%02x / (data)0x%02x / (diff)%d\n", max_error, i, reference[i], data[i], (unsigned int)diff);
		}
#endif
        }
    }

    if (threshold == 0.0f) {
        if (error_count) {
            printf("total # of errors = %d\n", error_count);
        }
        return (error_count == 0);
    } else {

        if (error_count) {
            printf("%4.2f(%%) of bytes mismatched (count=%d)\n", (float)error_count*100/(float)len, error_count);
        }

        return ((len*threshold > error_count));
    }
}

////////////////////////////////////////////////////////////////////////////////
//! Compare two integer arrays (inc Threshold for # of pixel we can have errors)
//! @return  CUTTrue if \a reference and \a data are identical, 
//!          otherwise CUTFalse
//! @param reference  handle to the reference data / gold image
//! @param data       handle to the computed data
//! @param len        number of elements in reference and data
////////////////////////////////////////////////////////////////////////////////
bool Compareubt( const unsigned char* reference, const unsigned char* data,
             const unsigned int len, const float epsilon, const float threshold ) 
{
    return compareDataAsFloatThreshold( reference, data, len, epsilon, threshold );
}

//////////////////////////////////////////////////////////////////////////////
//! Write / Save PPM or PGM file
//! @note Internal usage only
//! @param file  name of the image file
//! @param data  handle to the data read
//! @param w     width of the image
//! @param h     height of the image
//////////////////////////////////////////////////////////////////////////////  
bool savePPM( const char* file, unsigned char *data, 
         unsigned int w, unsigned int h, unsigned int channels) 
{
    if( NULL == data)
		return false;
    if( w <= 0)
		return false;
    if( h <= 0)
		return false;

    std::fstream fh( file, std::fstream::out | std::fstream::binary );
    if( fh.bad()) 
    {
        std::cerr << "savePPM() : Opening file failed." << std::endl;
        return false;
    }

    if (channels == 1)
    {
        fh << "P5\n";
    }
    else if (channels == 3) {
        fh << "P6\n";
    }
    else {
        std::cerr << "savePPM() : Invalid number of channels." << std::endl;
        return false;
    }

    fh << w << "\n" << h << "\n" << 0xff << std::endl;

    for( unsigned int i = 0; (i < (w*h*channels)) && fh.good(); ++i) 
    {
        fh << data[i];
    }
    fh.flush();

    if( fh.bad()) 
    {
        std::cerr << "savePPM() : Writing data failed." << std::endl;
        return false;
    } 
    fh.close();

    return true;
}


////////////////////////////////////////////////////////////////////////////////
//! Save PPM image file (with unsigned char as data element type, padded to 4 byte)
//! @param file  name of the image file
//! @param data  handle to the data read
//! @param w     width of the image
//! @param h     height of the image
////////////////////////////////////////////////////////////////////////////////
bool SavePPM4ub( const char* file, unsigned char *data, 
               unsigned int w, unsigned int h) 
{
    // strip 4th component
    int size = w * h;
    unsigned char *ndata = (unsigned char*) malloc( sizeof(unsigned char) * size*3);
    unsigned char *ptr = ndata;
    for(int i=0; i<size; i++) {
        *ptr++ = *data++;
        *ptr++ = *data++;
        *ptr++ = *data++;
        data++;
    }
    
    return savePPM( file, ndata, w, h, 3);
}

//////////////////////////////////////////////////////////////////////////////
//! Load PGM or PPM file
//! @note if data == NULL then the necessary memory is allocated in the 
//!       function and w and h are initialized to the size of the image
//! @return true if the file loading succeeded, otherwise false
//! @param file        name of the file to load
//! @param data        handle to the memory for the image file data
//! @param w        width of the image
//! @param h        height of the image
//! @param channels number of channels in image
//////////////////////////////////////////////////////////////////////////////
bool loadPPM( const char* file, unsigned char** data, 
         unsigned int *w, unsigned int *h, unsigned int *channels ) 
{
    FILE *fp = NULL;
    if(NULL == (fp = fopen(file, "rb"))) 
    {
        std::cerr << "LoadPPM() : Failed to open file: " << file << std::endl;
        return false;
    }

    // check header
    char header[PGMHeaderSize], *string = NULL;
    string = fgets( header, PGMHeaderSize, fp);
    if (strncmp(header, "P5", 2) == 0)
    {
        *channels = 1;
    }
    else if (strncmp(header, "P6", 2) == 0)
    {
        *channels = 3;
    }
    else {
        std::cerr << "LoadPPM() : File is not a PPM or PGM image" << std::endl;
        *channels = 0;
        return false;
    }

    // parse header, read maxval, width and height
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int maxval = 0;
    unsigned int i = 0;
    while(i < 3) 
    {
        string = fgets(header, PGMHeaderSize, fp);
        if(header[0] == '#') 
            continue;

        if(i == 0) 
        {
            i += sscanf( header, "%u %u %u", &width, &height, &maxval);
        }
        else if (i == 1) 
        {
            i += sscanf( header, "%u %u", &height, &maxval);
        }
        else if (i == 2) 
        {
            i += sscanf(header, "%u", &maxval);
        }
    }

    // check if given handle for the data is initialized
    if( NULL != *data) 
    {
        if (*w != width || *h != height) 
        {
            std::cerr << "LoadPPM() : Invalid image dimensions." << std::endl;
            return false;
        }
    } 
    else 
    {
        *data = (unsigned char*) malloc( sizeof( unsigned char) * width * height * *channels);
        *w = width;
        *h = height;
    }

    // read and close file
    size_t fsize = 0;
    fsize = fread( *data, sizeof(unsigned char), width * height * *channels, fp);
    fclose(fp);

    return true;
}


////////////////////////////////////////////////////////////////////////////////
//! Load PPM image file (with unsigned char as data element type), padding 4th component
//! @return true if reading the file succeeded, otherwise false
//! @param file  name of the image file
//! @param data  handle to the data read
//! @param w     width of the image
//! @param h     height of the image
////////////////////////////////////////////////////////////////////////////////
bool LoadPPM4ub( const char* file, unsigned char** data, 
               unsigned int *w,unsigned int *h)
{
    unsigned char *idata = 0;
    unsigned int channels;
    
    if (loadPPM( file, &idata, w, h, &channels)) {
        // pad 4th component
        int size = *w * *h;
        // keep the original pointer
        unsigned char* idata_orig = idata;
        *data = (unsigned char*) malloc( sizeof(unsigned char) * size * 4);
        unsigned char *ptr = *data;
        for(int i=0; i<size; i++) {
            *ptr++ = *idata++;
            *ptr++ = *idata++;
            *ptr++ = *idata++;
            *ptr++ = 0;
        }
        free( idata_orig);
        return true;
    }
    else
    {
        free( idata);
        return false;
    }
}

////////////////////////////////////////////////////////////////////////////////
//! Compare two PPM image files with an epsilon tolerance for equality
//! @return  true if \a reference and \a data are identical, 
//!          otherwise false
//! @param src_file   filename for the image to be compared
//! @param data       filename for the reference data / gold image
//! @param epsilon    epsilon to use for the comparison
//! @param threshold  threshold of pixels that can still mismatch to pass (i.e. 0.15f = 15% must pass)
//! @param verboseErrors output details of image mismatch to std::cerr
////////////////////////////////////////////////////////////////////////////////

bool ComparePPM( const char *src_file, const char *ref_file, 
			  const float epsilon, const float threshold, bool verboseErrors )
{
	unsigned char *src_data, *ref_data;
	unsigned long error_count = 0;
	unsigned int ref_width, ref_height;
	unsigned int src_width, src_height;

	if (src_file == NULL || ref_file == NULL) {
		if(verboseErrors) std::cerr << "PPMvsPPM: src_file or ref_file is NULL.  Aborting comparison\n";
		return false;
	}

    if(verboseErrors) {
        std::cerr << "> Compare (a)rendered:  <" << src_file << ">\n";
        std::cerr << ">         (b)reference: <" << ref_file << ">\n";
    }

	if (LoadPPM4ub(ref_file, &ref_data, &ref_width, &ref_height) != true) 
	{
		if(verboseErrors) std::cerr << "PPMvsPPM: unable to load ref image file: "<< ref_file << "\n";
		return false;
	}

	if (LoadPPM4ub(src_file, &src_data, &src_width, &src_height) != true) 
	{
		std::cerr << "PPMvsPPM: unable to load src image file: " << src_file << "\n";
		return false;
	}

	if(src_height != ref_height || src_width != ref_width)
	{
		if(verboseErrors) std::cerr << "PPMvsPPM: source and ref size mismatch (" << src_width << 
			"," << src_height << ")vs(" << ref_width << "," << ref_height << ")\n";

//        src_height = min(src_height, ref_height);
//        src_width  = min(src_width , ref_width );
//		return false;
	}

	if(verboseErrors) std::cerr << "PPMvsPPM: comparing images size (" << src_width << 
		"," << src_height << ") epsilon(" << epsilon << "), threshold(" << threshold*100 << "%)\n";
//	if (Compareube( ref_data, src_data, src_width*src_height*4, epsilon ) == false) 
	if (Compareubt( ref_data, src_data, src_width*src_height*4, epsilon, threshold ) == false) 
	{
		error_count=1;
	}

	if (error_count == 0) 
	{ 
		if(verboseErrors) std::cerr << "    OK\n\n"; 
	} else 
	{
		if(verboseErrors) std::cerr << "    FAILURE!  "<<error_count<<" errors...\n\n";
	}
	return (error_count == 0);
}


