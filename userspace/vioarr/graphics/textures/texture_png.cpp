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
        sLog.Error(msg);
        delete fbuffer;
        return 0;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        sLog.Error("error: png_create_read_struct returned 0.");
        delete fbuffer;
        return 0;
    }

    // create png info struct
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        sLog.Error("error: png_create_info_struct returned 0.");
        png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
        delete fbuffer;
        return 0;
    }

    // create png info struct
    png_infop end_info = png_create_info_struct(png_ptr);
    if (!end_info) {
        sLog.Error("error: png_create_info_struct returned 0.");
        png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp) NULL);
        delete fbuffer;
        return 0;
    }

    // the code in this if statement gets called if libpng encounters an error
    if (setjmp(png_jmpbuf(png_ptr))) {
        sLog.Error("error from libpng");
        png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
        delete fbuffer;
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

    // variables to pass to get info
    int bit_depth, color_type;
    png_uint_32 temp_width, temp_height;

    // get info about png
    png_get_IHDR(png_ptr, info_ptr, &temp_width, &temp_height, &bit_depth, &color_type,
        NULL, NULL, NULL);

    if (Width){ *Width = temp_width; }
    if (Height){ *Height = temp_height; }

    if (bit_depth != 8) {
        msg = Path;
        msg += ": Unsupported bit depth ";
        msg += std::to_string(bit_depth) + ". Must be 8.";
        sLog.Error(msg);
        return 0;
    }

    GLint format;
    switch(color_type) {
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
            sLog.Error(msg);
            return 0;
    }

    // Update the png info struct.
    png_read_update_info(png_ptr, info_ptr);

    // Row size in bytes.
    int rowbytes = png_get_rowbytes(png_ptr, info_ptr);

    // glTexImage2d requires rows to be 4-byte aligned
    rowbytes += 3 - ((rowbytes-1) % 4);

    // Allocate the image_data as a big block, to be given to opengl
    png_byte * image_data = (png_byte *)malloc(rowbytes * temp_height * sizeof(png_byte)+15);
    if (image_data == NULL) {
        sLog.Error("error: could not allocate memory for PNG image data\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
        delete fbuffer;
        return 0;
    }

    // row_pointers is for pointing to image_data for reading the png with libpng
    png_byte ** row_pointers = (png_byte **)malloc(temp_height * sizeof(png_byte *));
    if (row_pointers == NULL) {
        sLog.Error("error: could not allocate memory for PNG row pointers\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
        free(image_data);
        delete fbuffer;
        return 0;
    }

    // set the individual row_pointers to point at the correct offsets of image_data
    for (unsigned int i = 0; i < temp_height; i++) {
        row_pointers[temp_height - 1 - i] = image_data + i * rowbytes;
    }

    // read the png into image_data through row_pointers
    png_read_image(png_ptr, row_pointers);

    // Generate the OpenGL texture object
    GLuint texture;
    sLog.Info("Generating texture");
    glGenTextures(1, &texture);
    sLog.Info("Binding texture");
    glBindTexture(GL_TEXTURE_2D, texture);
    sLog.Info("Projecting texture");
    TRACE("%u x %u x %u, 0x%x (rowbytes %i), format %i, alloc size %u", 
        temp_width, temp_height, bit_depth, image_data, rowbytes, format, rowbytes * temp_height * sizeof(png_byte)+15);
    glTexImage2D(GL_TEXTURE_2D, 0, format, temp_width, temp_height, 0, format, GL_UNSIGNED_BYTE, image_data);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    sLog.Info("Texture done");

    // clean up
    png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
    free(image_data);
    free(row_pointers);
    delete fbuffer;
    return texture;
}
