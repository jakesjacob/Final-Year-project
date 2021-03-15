
#ifndef GRAPHICSDISPLAYGIF_H
#define GRAPHICSDISPLAYGIF_H

typedef struct {
    uint16_t image_left_position;
    uint16_t image_top_position;
    uint16_t image_width;
    uint16_t image_height;
    uint8_t fields;
} gif_image_descriptor_t;
const uint16_t gif_image_descriptor_size = 9;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t fields;
    uint8_t background_color_index;
    uint8_t pixel_aspect_ratio;
} gif_screen_descriptor_t;
const uint16_t gif_screen_descriptor_size = 7;

#endif // GRAPHICSDISPLAYGIF_H
