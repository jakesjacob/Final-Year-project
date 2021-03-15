


// GIFRender.cpp : Defines the entry point for the console application.
//
// The code in this file was initially found online, in a tutorial.
// It has been revised significantly in this derivative.
// No copyright claim was found in the code.
// http://commandlinefanatic.com/cgi-bin/showarticle.cgi?article=art011
//

#include "mbed.h"

#include "GraphicsDisplay.h"


#define DEBUG "GIF_"
// 
// INFO("Stuff to show %d", var); // new-line is automatically appended
//
#if (defined(DEBUG) && !defined(TARGET_LPC11U24))
#define INFO(x, ...) std::printf("[INF %s %4d] "x"\r\n", DEBUG, __LINE__, ##__VA_ARGS__);
#define WARN(x, ...) std::printf("[WRN %s %4d] "x"\r\n", DEBUG, __LINE__, ##__VA_ARGS__);
#define ERR(x, ...)  std::printf("[ERR %s %4d] "x"\r\n", DEBUG, __LINE__, ##__VA_ARGS__);
static void HexDump(const char * title, const uint8_t * p, int count) {
    int i;
    char buf[100] = "0000: ";

    if (*title)
        INFO("%s", title);
    for (i = 0; i<count; ) {
        sprintf(buf + strlen(buf), "%02X ", *(p + i));
        if ((++i & 0x0F) == 0x00) {
            INFO("%s", buf);
            if (i < count)
                sprintf(buf, "%04X: ", i);
            else
                buf[0] = '\0';
        }
    }
    if (strlen(buf))
        INFO("%s", buf);
}
#else
#define INFO(x, ...)
#define WARN(x, ...)
#define ERR(x, ...)
#define HexDump(a, b, c)
#endif


#define EXTENSION_INTRODUCER   0x21
#define IMAGE_DESCRIPTOR       0x2C
#define TRAILER                0x3B

#define GRAPHIC_CONTROL        0xF9
#define APPLICATION_EXTENSION  0xFF
#define COMMENT_EXTENSION      0xFE
#define PLAINTEXT_EXTENSION    0x01


typedef struct {
    unsigned char byte;
    int prev;
    int len;
} dictionary_entry_t;

typedef struct {
    unsigned char extension_code;
    unsigned char block_size;
} extension_t;
const uint16_t extension_size = 2;

typedef struct {
    unsigned char fields;
    unsigned short delay_time;
    unsigned char transparent_color_index;
} graphic_control_extension_t;
const uint16_t graphic_control_extension_size = 4;

typedef struct {
    unsigned char application_id[8];
    unsigned char version[3];
} application_extension_t;
const uint16_t application_extension_size = 11;

typedef struct {
    unsigned short left;
    unsigned short top;
    unsigned short width;
    unsigned short height;
    unsigned char cell_width;
    unsigned char cell_height;
    unsigned char foreground_color;
    unsigned char background_color;
} plaintext_extension_t;
const uint16_t plaintext_extension_size = 12;


size_t GraphicsDisplay::read_filesystem_bytes(void * buffer, int numBytes, FILE * fh) {
    size_t bytesRead;

    bytesRead = fread(buffer, 1, numBytes, fh);
    HexDump("mem", (const uint8_t *) buffer, bytesRead);
    return bytesRead;
}


/// uncompress_gif the data to memory
///
/// @param[in] code_length is the size of this descriptor field
/// @param[in] input is a pointer to the input data
/// @param[in] input_length is the number of bytes in the input.
/// @param[out] out is where the uncompress_gifed information is written.
/// @return noerror on success, or failure code
///
RetCode_t GraphicsDisplay::uncompress_gif(int code_length, const unsigned char *input, int input_length, unsigned char *out) {
    int i, bit;
    int code, prev = -1;
    dictionary_entry_t * dictionary;
    int dictionary_ind;
    unsigned int mask = 0x01;
    int reset_code_length;
    int clear_code; // This varies depending on code_length
    int stop_code;  // one more than clear code
    int match_len;

    clear_code = 1 << (code_length);
    stop_code = clear_code + 1;
    reset_code_length = code_length;

    // Create a dictionary large enough to hold "code_length" entries.
    // Once the dictionary overflows, code_length increases
    dictionary = (dictionary_entry_t *) malloc(sizeof(dictionary_entry_t) * (1 << (code_length + 1)));
    if (dictionary == NULL)
        return not_enough_ram;
    // Initialize the first 2^code_len entries of the dictionary with their
    // indices.  The rest of the entries will be built up dynamically.

    // Technically, it shouldn't be necessary to initialize the
    // dictionary.  The spec says that the encoder "should output a
    // clear code as the first code in the image data stream".  It doesn't
    // say must, though...
    for (dictionary_ind = 0; dictionary_ind < (1 << code_length); dictionary_ind++) {
        dictionary[dictionary_ind].byte = (uint8_t) dictionary_ind;
        // XXX this only works because prev is a 32-bit int (> 12 bits)
        dictionary[dictionary_ind].prev = -1;
        dictionary[dictionary_ind].len = 1;
    }
    // 2^code_len + 1 is the special "end" code; don't give it an entry here
    dictionary_ind++;
    dictionary_ind++;

    // TODO verify that the very last byte is clear_code + 1
    while (input_length) {
        code = 0x0;
        // Always read one more bit than the code length
        for (i = 0; i < (code_length + 1); i++) {
            // This is different than in the file read example; that
            // was a call to "next_bit"
            bit = (*input & mask) ? 1 : 0;
            mask <<= 1;
            if (mask == 0x100) {
                mask = 0x01;
                input++;
                input_length--;
            }
            code = code | (bit << i);
        }
        if (code == clear_code) {
            code_length = reset_code_length;
            dictionary = (dictionary_entry_t *) realloc(dictionary,
                sizeof(dictionary_entry_t) * (1 << (code_length + 1)));
            for (dictionary_ind = 0; dictionary_ind < (1 << code_length); dictionary_ind++) {
                dictionary[dictionary_ind].byte = (uint8_t) dictionary_ind;
                // XXX this only works because prev is a 32-bit int (> 12 bits)
                dictionary[dictionary_ind].prev = -1;
                dictionary[dictionary_ind].len = 1;
            }
            dictionary_ind++;
            dictionary_ind++;
            prev = -1;
            continue;
        } else if (code == stop_code) {
            if (input_length > 1) {
                free(dictionary);
                return not_supported_format;
            }
            break;
        }

        // Update the dictionary with this character plus the _entry_
        // (character or string) that came before it
        if ((prev > -1) && (code_length < 12)) {
            if (code > dictionary_ind) {
                //fprintf(stderr, "code = %.02x, but dictionary_ind = %.02x\n", code, dictionary_ind);
                free(dictionary);
                return not_supported_format;
            }
            if (code == dictionary_ind) {
                int ptr = prev;

                while (dictionary[ptr].prev != -1) {
                    ptr = dictionary[ptr].prev;
                }
                dictionary[dictionary_ind].byte = dictionary[ptr].byte;
            } else {
                int ptr = code;
                while (dictionary[ptr].prev != -1) {
                    ptr = dictionary[ptr].prev;
                }
                dictionary[dictionary_ind].byte = dictionary[ptr].byte;
            }
            dictionary[dictionary_ind].prev = prev;
            dictionary[dictionary_ind].len = dictionary[prev].len + 1;
            dictionary_ind++;
            // GIF89a mandates that this stops at 12 bits
            if ((dictionary_ind == (1 << (code_length + 1))) &&
                (code_length < 11)) {
                code_length++;
                dictionary = (dictionary_entry_t *) realloc(dictionary,
                    sizeof(dictionary_entry_t) * (1 << (code_length + 1)));
            }
        }
        prev = code;
        // Now copy the dictionary entry backwards into "out"
        match_len = dictionary[code].len;
        while (code != -1) {
            int pos = dictionary[code].len - 1;
            out[pos] = dictionary[code].byte;
            //INFO("%p out[%d] = %02X\r\n", out, pos, out[pos]);
            if (dictionary[code].prev == code) {
                free(dictionary);
                return not_supported_format;
            }
            code = dictionary[code].prev;
        }
        out += match_len;
    }
    free(dictionary);
    return noerror;
}


/// read a block
/// 
/// @param[in] fh is the handle to the gif file.
/// @param[in] data is a pointer to a pointer to the data being processed.
/// @return -1 on failure, 0 when done, or >0 as the length of a processed data block
///
int GraphicsDisplay::read_gif_sub_blocks(FILE * fh, unsigned char **data) {
    int data_length;
    int index;
    unsigned char block_size;

    // Everything following are data sub-blocks, until a 0-sized block is encountered.
    data_length = 0;
    *data = NULL;
    index = 0;

    while (1) {
        if (read_filesystem_bytes(&block_size, 1, fh) < 1) {
            return -1;
        }
        if (block_size == 0) {
            break;
        }
        data_length += block_size;
        *data = (unsigned char *) realloc(*data, data_length);
        if (data) {
            if (read_filesystem_bytes(*data + index, block_size, fh) < block_size) {
                return -1;
            }
            index += block_size;
        } else {
            return -1;
        }
    }
    return data_length;
}

// color table is encoded as 24-bit values, but we don't need that much space, since 
// the RA8875 is configured as either 8 or 16-bit color.
//
RetCode_t GraphicsDisplay::readColorTable(color_t * colorTable, int colorTableSize, FILE * fh) {
    struct {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    } rgb;
    while (colorTableSize--) {
        if (read_filesystem_bytes(&rgb, 3, fh) < 3)
            return not_supported_format;
        *colorTable++ = RGB(rgb.r, rgb.g, rgb.b);
    }
    return noerror;
}

RetCode_t GraphicsDisplay::readGIFImageDescriptor(FILE * fh, gif_image_descriptor_t * imageDescriptor) {
    if (read_filesystem_bytes(imageDescriptor, gif_image_descriptor_size, fh) < gif_image_descriptor_size)
        return not_supported_format;
    INFO("gif_image_descriptor\r\n");
    INFO("       left: %d\r\n", imageDescriptor->image_left_position);
    INFO("        top: %d\r\n", imageDescriptor->image_top_position);
    INFO("      width: %d\r\n", imageDescriptor->image_width);
    INFO("     height: %d\r\n", imageDescriptor->image_height);
    INFO("     fields: %02Xx\r\n", imageDescriptor->fields);
    INFO("          lct: %2d\r\n", (imageDescriptor->fields & 0x80) ? 1 : 0);
    INFO("          res: %2d\r\n", (imageDescriptor->fields & 0x40) ? 1 : 0);
    INFO("         sort: %2d\r\n", (imageDescriptor->fields & 0x20) ? 1 : 0);
    INFO("          res: %2d\r\n", ((imageDescriptor->fields & 0x18) >> 3));
    INFO("      lct siz: %2d\r\n", 1 << ((imageDescriptor->fields & 0x07) + 1));
    if (imageDescriptor->fields & 0x80) {
        local_color_table_size = 1 << ((imageDescriptor->fields & 0x07) + 1);
        local_color_table = (color_t *) malloc(sizeof(color_t) * local_color_table_size);
        if (local_color_table == NULL)
            return not_enough_ram;
        if (readColorTable(local_color_table, local_color_table_size, fh) != noerror) {
            free(local_color_table);
            local_color_table = NULL;
            return not_supported_format;
        }
    }
    return noerror;
}

/// Process the image section of the GIF file
///
/// @param[in] fh is the handle to the file.
/// @param[in] uncompress_gifed_data is a pointer to a pointer to where the data will be placed.
/// @returns true if all went well.
///
RetCode_t GraphicsDisplay::process_gif_image_descriptor(FILE * fh, uint8_t ** uncompress_gifed_data, int width, int height) {
    int compressed_data_length;
    unsigned char *compressed_data = NULL;
    unsigned char lzw_code_size;
    int uncompress_gifed_data_length = 0;
    RetCode_t res = not_supported_format;
    local_color_table_size = 0;

    if (read_filesystem_bytes(&lzw_code_size, 1, fh) < 1)
        goto done;
    INFO("lzw_code_size\r\n");
    INFO("   lzw code: %d\r\n", lzw_code_size);
    compressed_data_length = read_gif_sub_blocks(fh, &compressed_data);
    if (compressed_data_length > 0) {
        uncompress_gifed_data_length = width * height;
        *uncompress_gifed_data = (unsigned char *) malloc(uncompress_gifed_data_length);
        if (*uncompress_gifed_data == NULL)
            return not_enough_ram;
        res = uncompress_gif(lzw_code_size, compressed_data, compressed_data_length, *uncompress_gifed_data);
    }
done:
    if (compressed_data)
        free(compressed_data);
    return res;
}


int GraphicsDisplay::process_gif_extension(FILE * fh) {
    extension_t extension;
    graphic_control_extension_t gce;
    application_extension_t application;
    plaintext_extension_t plaintext;
    unsigned char *extension_data = NULL;
    /* int extension_data_length; */

    if (read_filesystem_bytes(&extension, extension_size, fh) < extension_size) {
        return 0;
    }
    INFO("extension\r\n");
    INFO("        code: %d\r\n", extension.extension_code);
    INFO("  block size: %d\r\n", extension.block_size);
    switch (extension.extension_code) {
        case GRAPHIC_CONTROL:
            if (read_filesystem_bytes(&gce, graphic_control_extension_size, fh) < graphic_control_extension_size) {
                return 0;
            }
            INFO("graphic_control_extension\r\n");
            INFO("      fields: %d\r\n", gce.fields);
            INFO("  delay time: %d\r\n", gce.delay_time);
            INFO(" transparent: %d\r\n", gce.transparent_color_index);
            break;
        case APPLICATION_EXTENSION:
            if (read_filesystem_bytes(&application, application_extension_size, fh) < application_extension_size) {
                return 0;
            }
            HexDump("application", (const uint8_t *) &application, sizeof(application));
            break;
        case COMMENT_EXTENSION:
            // comment extension; do nothing - all the data is in the
            // sub-blocks that follow.
            break;
        case PLAINTEXT_EXTENSION:
            if (read_filesystem_bytes(&plaintext, plaintext_extension_size, fh) < plaintext_extension_size) {
                return 0;
            }
            HexDump("plaintext", (const uint8_t *) &plaintext, sizeof(plaintext));
            break;
        default:
            return (0);
    }
    // All extensions are followed by data sub-blocks; even if it's
    // just a single data sub-block of length 0
    /* extension_data_length = */ read_gif_sub_blocks(fh, &extension_data);
    if (extension_data != NULL)
        free(extension_data);
    return 1;
}


RetCode_t GraphicsDisplay::_RenderGIF(loc_t ScreenX, loc_t ScreenY, FILE * fh) {
    //int color_resolution_bits;
    global_color_table_size = 0;
    local_color_table_size = 0;

    if (GetGIFHeader(fh) != noerror)
        return not_supported_format;
    //color_resolution_bits = ((screen_descriptor.fields & 0x70) >> 4) + 1;
    if (screen_descriptor.fields & 0x80) {
        // If bit 7 is set, the next block is a global color table; read it
        global_color_table_size = 1 << ((screen_descriptor.fields & 0x07) + 1);
        global_color_table = (color_t *) malloc(sizeof(color_t) * global_color_table_size);
        if (global_color_table == NULL)
            return not_enough_ram;
        // XXX this could conceivably return a short count...
        if (readColorTable(global_color_table, global_color_table_size, fh) != noerror) {
            return not_supported_format;
        }
        HexDump("Global Color Table", (const uint8_t *) global_color_table, 3 * global_color_table_size);
    }
    unsigned char block_type = 0x0;
    uint8_t * uncompress_gifed_data = NULL;
    gif_image_descriptor_t gif_image_descriptor;        // the image fragment
    while (block_type != TRAILER) {
        if (read_filesystem_bytes(&block_type, sizeof(block_type), fh) < sizeof(block_type))
            return not_supported_format;
        INFO("block type: %02X", block_type);
        
        switch (block_type) {
            case IMAGE_DESCRIPTOR:
                if (readGIFImageDescriptor(fh, &gif_image_descriptor) != noerror)
                    return not_supported_format;
                if (process_gif_image_descriptor(fh, &uncompress_gifed_data, gif_image_descriptor.image_width, gif_image_descriptor.image_height) == noerror) {
                    if (uncompress_gifed_data) {
                        // Ready to render to the screen
                        INFO("Render to (%d,%d)\r\n", ScreenX, ScreenY);
                        color_t * active_color_table = (local_color_table) ? local_color_table : global_color_table;

                        // create a local image buffer for this fragment
                        color_t * cbmp = (color_t *) malloc(sizeof(color_t) * gif_image_descriptor.image_width * gif_image_descriptor.image_height);
                        if (cbmp == NULL)
                            return not_enough_ram;
                        for (int i = 0; i < gif_image_descriptor.image_width * gif_image_descriptor.image_height; i++) {
                            int cIndex = uncompress_gifed_data[i];
                            cbmp[i] = active_color_table[cIndex];
                        }
                        // Write Fragment to Screen
                        #if 0
                        INFO("Render fragment: (%d,%d), offset: (%d,%d), w x h (%d,%d)\r\n",
                            ScreenX, ScreenY,
                            gif_image_descriptor.image_left_position,
                            gif_image_descriptor.image_top_position,
                            gif_image_descriptor.image_width,
                            gif_image_descriptor.image_height);
                        for (uint16_t y = 0; y < gif_image_descriptor.image_height; y++) {
                            for (uint16_t x = 0; x < gif_image_descriptor.image_width; x++) {
                                INFO("%04X ", cbmp[y * gif_image_descriptor.image_height + x]);
                            }
                            INFO("\r\n");
                        }
                        #else
                        rect_t restore = windowrect;
                        window(ScreenX + gif_image_descriptor.image_left_position,
                            ScreenY + gif_image_descriptor.image_top_position,
                            gif_image_descriptor.image_width,
                            gif_image_descriptor.image_height);
                        pixelStream(cbmp, screen_descriptor.width * screen_descriptor.height,
                            ScreenX + gif_image_descriptor.image_left_position,
                            ScreenY + gif_image_descriptor.image_top_position);
                        window(restore);
                        #endif
                        // end write
                        free(cbmp);
                    }
                    if (local_color_table) {
                        free(local_color_table);
                        local_color_table = NULL;
                    }
                } else {
                    return not_supported_format;
                }
                break;
            case EXTENSION_INTRODUCER:
                if (!process_gif_extension(fh))
                    return not_supported_format;
                break;
            case TRAILER:
                break;
            default:
                return not_supported_format;
        }
    }
    return noerror;
}


// hasGIFHeader determines if it is a GIF file
//
// This reads a few bytes of the file and determines if they have the
// GIF89a signature. GIF87a is not supported.
//
// @param fh is a file handle.
// @returns true if it is GIF89a.
//
bool GraphicsDisplay::hasGIFHeader(FILE * fh) {
    char GIF_Header[6];
    if (read_filesystem_bytes(GIF_Header, sizeof(GIF_Header), fh) != sizeof(GIF_Header))
        return false;
    if (strncmp("GIF89a", GIF_Header, sizeof(GIF_Header)))
        return false;
    return true;
}


RetCode_t GraphicsDisplay::GetGIFHeader(FILE * fh) {
    if (read_filesystem_bytes(&screen_descriptor, gif_screen_descriptor_size, fh) < gif_screen_descriptor_size) {
        return not_supported_format;
    }
    screen_descriptor_isvalid = true;
    INFO("screen_descriptor\r\n");
    INFO("      width: %d\r\n", screen_descriptor.width);
    INFO("     height: %d\r\n", screen_descriptor.height);
    INFO("     fields: %02Xx\r\n", screen_descriptor.fields);
    INFO("          gct: %2d\r\n", (screen_descriptor.fields & 0x80) ? 1 : 0);
    INFO("          res: %2d\r\n", ((screen_descriptor.fields & 0x70) >> 4) + 1);
    INFO("         sort: %2d\r\n", (screen_descriptor.fields & 0x08) ? 1 : 0);
    INFO("      gct siz: %2d\r\n", 1 << ((screen_descriptor.fields & 0x07) + 1));
    INFO("   back clr: %d\r\n", screen_descriptor.background_color_index);
    INFO("    pix rat: %d\r\n", screen_descriptor.pixel_aspect_ratio);
    return noerror;
}


RetCode_t GraphicsDisplay::GetGIFMetrics(gif_screen_descriptor_t * imageDescriptor, const char * Name_GIF) {
    RetCode_t ret = not_supported_format;

    if (screen_descriptor_isvalid) {
        ret = noerror;
    } else {
        FILE *fh = fopen(Name_GIF, "rb");
        if (fh) {
            ret = GetGIFHeader(fh);
            fclose(fh);
        }
    }
    if (ret == noerror)
        *imageDescriptor = screen_descriptor;
    return ret;
}


RetCode_t GraphicsDisplay::RenderGIFFile(loc_t x, loc_t y, const char *Name_GIF) {
    RetCode_t rt = file_not_found;

    INFO("Opening {%s}", Name_GIF);
    screen_descriptor_isvalid = false;
    FILE *fh = fopen(Name_GIF, "rb");
    if (fh) {
        if (hasGIFHeader(fh)) {
            rt = _RenderGIF(x, y, fh);
        }
        fclose(fh);
        if (global_color_table)
            free(global_color_table);
        if (local_color_table)
            free(local_color_table);
    }
    return rt;
}


