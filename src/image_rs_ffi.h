/*
 * This file is part of Lightning Image Viewer
 * Copyright (c) 2021-2025 Eugene Shatsky
 * Licensed under GPLv3+
 * image-rs is licensed under Apache-2.0, MIT
 */

#ifndef IMAGE_RS_FFI_H
#define IMAGE_RS_FFI_H

#ifdef __cplusplus
extern "C" {
#endif

struct image_rs_ffi_Frame {
    uint8_t* buf;
    size_t buf_len;

    uint32_t width;
    uint32_t height;

    uint32_t x_offset;
    uint32_t y_offset;

    uint32_t delay_numer_ms;
    uint32_t delay_denom_ms;
};

struct image_rs_ffi_Frames {
    const struct image_rs_ffi_Frame* ffi_frames_vec_data;
    size_t count;

    void* frames_vec;
    void* ffi_frames_vec;
};

struct image_rs_ffi_RgbaImage {
    uint8_t* buf;
    size_t buf_len;
    size_t buf_vec_cap;

    uint32_t width;
    uint32_t height;
};

void* image_rs_ffi_get_decoder(const char* path_ptr);

uint8_t image_rs_ffi_get_orientation_as_exif(void* decoder_ptr);

void* image_rs_ffi_get_frames_iter(void* decoder_ptr);

struct image_rs_ffi_Frames image_rs_ffi_get_frames(void* frames_iter_ptr);

void image_rs_ffi_free_frames(struct image_rs_ffi_Frames ffi_frames);

struct image_rs_ffi_RgbaImage image_rs_ffi_get_rgba_image(void* decoder_ptr);

void image_rs_ffi_free_rgba_image(struct image_rs_ffi_RgbaImage ffi_rgba_image);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // IMAGE_RS_FFI_H
