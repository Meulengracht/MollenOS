/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS - Vioarr Window Compositor System
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */

/* Includes
 * - System */
#include "../../utils/log_manager.hpp"
#include "texture_manager.hpp"
#include <cstdlib>
#include <fstream>
#include <string>
#include <png.h>

struct PngMemoryStream {
    char*   FileBuffer;
    size_t  Index;
};

void MemoryStreamRead(png_structp png_ptr, png_bytep outBytes, png_size_t byteCountToRead) {
    png_voidp io_ptr = png_get_io_ptr(png_ptr);
    if(io_ptr == NULL) {
        return;
    }
    struct PngMemoryStream *MemoryStream = (struct PngMemoryStream*)io_ptr;
    memcpy(outBytes, &MemoryStream->FileBuffer[MemoryStream->Index], byteCountToRead);
    MemoryStream->Index += byteCountToRead;
}

GLuint CTextureManager::CreateTexturePNG(const char *Path, int *Width, int *Height)
{
    struct PngMemoryStream *MemoryStream = NULL;
    FILE *fp;
    std::string msg = "";
    char *fbuffer = NULL;
    long fsize = 0;

    // Step 1, read the entire image into buffer
    fp = fopen(Path, "rb");
    if (fp == NULL) {
        sLog.Error("Failed to locate file-path");
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);
    rewind(fp);
    fbuffer = new char[fsize];
    fsize = fread(fbuffer, 1, fsize, fp);
    fclose(fp);

    // Validate header
    if (png_sig_cmp((png_const_bytep)fbuffer, 0, 8)) {
        msg = "error: ";
        msg += Path;
        msg += " is not a PNG.";
        sLog.Error(msg.c_str());
        delete[] fbuffer;
        return 0;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        sLog.Error("error: png_create_read_struct returned 0.");
        delete[] fbuffer;
        return 0;
    }

    // create png info struct
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        sLog.Error("error: png_create_info_struct returned 0.");
        png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
        delete[] fbuffer;
        return 0;
    }

    // create png info struct
    png_infop end_info = png_create_info_struct(png_ptr);
    if (!end_info) {
        sLog.Error("error: png_create_info_struct returned 0.");
        png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp) NULL);
        delete[] fbuffer;
        return 0;
    }

    // the code in this if statement gets called if libpng encounters an error
    if (setjmp(png_jmpbuf(png_ptr))) {
        sLog.Error("error from libpng");
        png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
        delete[] fbuffer;
        return 0;
    }

    // Create memory stream
    MemoryStream = new struct PngMemoryStream;
    MemoryStream->FileBuffer = fbuffer;
    MemoryStream->Index      = 8;

    // Set read function
    png_set_read_fn(png_ptr, (png_voidp)MemoryStream, MemoryStreamRead);

    // let libpng know you already read the first 8 bytes
    png_set_sig_bytes(png_ptr, 8);

    // read all the info up to the image data
    png_read_info(png_ptr, info_ptr);

    // Read image metadata
    png_uint_32 temp_width  = png_get_image_width(png_ptr, info_ptr);
    png_uint_32 temp_height = png_get_image_height(png_ptr, info_ptr);
    png_uint_32 bit_depth   = png_get_bit_depth(png_ptr, info_ptr);
    png_uint_32 channels    = png_get_channels(png_ptr, info_ptr);
    png_uint_32 color_type  = png_get_color_type(png_ptr, info_ptr);
    if (Width){ *Width = (int)temp_width; }
    if (Height){ *Height = (int)temp_height; }

    GLint format;
    switch (color_type) {
        case PNG_COLOR_TYPE_PALETTE:
            format = GL_RGB;
            png_set_palette_to_rgb(png_ptr);
            //Don't forget to update the channel info
            //It's used later to know how big a buffer we need for the image
            channels = 3;           
            break;
        case PNG_COLOR_TYPE_GRAY_ALPHA:
            format = GL_RGBA;
            png_set_gray_to_rgb(png_ptr);
        case PNG_COLOR_TYPE_GRAY:
            format = GL_RGB;
            if (bit_depth < 8) {
                png_set_expand_gray_1_2_4_to_8(png_ptr);
            }
            png_set_gray_to_rgb(png_ptr);
            //And the bitdepth info
            bit_depth = 8;
            break;
        case PNG_COLOR_TYPE_RGB:
            format = GL_RGB;
            break;
        case PNG_COLOR_TYPE_RGB_ALPHA:
            format = GL_RGBA;
            break;
        default:
            msg = Path;
            msg += ": Unknown libpng color type ";
            msg += std::to_string(color_type) + ".";
            sLog.Error(msg.c_str());
            return 0;
    }

    // if the image has a transperancy set.. convert it to a full Alpha channel..
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png_ptr);
        channels += 1;
    }

    //We don't support 16 bit precision.. so if the image Has 16 bits per channel
    //precision... round it down to 8.
    if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
        bit_depth = 8;
    }

    if (bit_depth != 8) {
        msg = Path;
        msg += ": Unsupported bit depth ";
        msg += std::to_string(bit_depth) + ". Must be 8.";
        sLog.Error(msg.c_str());
        return 0;
    }

    // Update the png info struct.
    png_read_update_info(png_ptr, info_ptr);

    // Data pointers
    png_bytep* row_pointers = NULL;
    char* image_data        = NULL;

    // Initialize row pointers
    const unsigned int stride = temp_width * bit_depth * channels / 8;
    row_pointers    = new png_bytep[temp_height];
    image_data      = new char[temp_width * temp_height * bit_depth * channels / 8];

    for (size_t i = 0; i < temp_height; i++) {
        //Set the pointer to the data pointer + i times the row stride.
        //Notice that the row order is reversed with q.
        //This is how at least OpenGL expects it,
        //and how many other image loaders present the data.
        png_uint_32 q   = (temp_height - i - 1) * stride;
        row_pointers[i] = (png_bytep)image_data + q;
    }

    // Perform the actual read
    png_read_image(png_ptr, row_pointers);

    // Generate the OpenGL texture object
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, format, temp_width, temp_height, 0, format, GL_UNSIGNED_BYTE, image_data);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // clean up
    png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
    free(image_data);
    free(row_pointers);
    delete[] fbuffer;
    return texture;
}
