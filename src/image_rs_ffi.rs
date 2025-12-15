/*
 * This file is part of Lightning Image Viewer
 * Copyright (c) 2021-2025 Eugene Shatsky
 * Licensed under GPLv3+
 * image-rs is licensed under Apache-2.0, MIT
 */

use std::fs::File;
use std::ptr::{null, null_mut};
use std::io::BufReader;

use image::{ImageReader, AnimationDecoder, Frame, ImageDecoder};

// image::Frame
#[repr(C)]
struct image_rs_ffi_Frame {
    buf: *const u8, // rgba px data
    buf_len: usize,

    // TODO u32/uint32_t vs std::os::raw::c_uint/unsigned int?
    width: u32,
    height: u32,

    x_offset: u32,
    y_offset: u32,

    // delay_numer_ms/delay_denom_ms
    delay_numer_ms: u32,
    delay_denom_ms: u32,
}

// Vec<image::Frame>
#[repr(C)]
struct image_rs_ffi_Frames {
    // C-accessible view of Frames data incl. px buffers; doesn't own data
    ffi_frames_vec_data: *const image_rs_ffi_Frame,
    count: usize,

    // opaque ptrs to Rust vecs owning data; used for freeing
    // TODO unify vec freeing? Here in image_rs_ffi_Frames whole vecs are kept, but in image_rs_ffi_RgbaImage vec "raw parts" are
    // TODO one struct for anim frame and still img? Frame has px buf in image::RgbaImage too, but it also has offsets and delay
    frames_vec: *mut Vec<Frame>,
    ffi_frames_vec: *mut Vec<image_rs_ffi_Frame>, // ffi_frames_vec_data is allocated via Rust vec and has to be freed via it
}

// image::RgbaImage
#[repr(C)]
struct image_rs_ffi_RgbaImage {
    buf: *mut u8, // rgba px data, has to be mut to allow vec reconstruct
    buf_len: usize,
    // saved to reconstruct vec for freeing
    buf_vec_cap: usize,

    width: u32,
    height: u32,
}

// needed to make it possible to cast decoder trait obj to concrete type for type-specific things
// supertrait allowing to cast trait obj from it to Any (allowing further cast to concrete type) or to use ImageDecoder
trait ImageDecoderAny: ImageDecoder + std::any::Any {}
// blanket impl to allow casting trait obj of type which impls both ImageDecoder and Any (i. e. any concrete type decoder) to ImageDecoderAny
impl<T: ImageDecoder + std::any::Any> ImageDecoderAny for T {}

// get image decoder
#[unsafe(no_mangle)]
extern "C" fn image_rs_ffi_get_decoder(path_ptr: *const std::os::raw::c_char) -> *const std::ffi::c_void {
    if path_ptr.is_null() { return null(); }
    let path = unsafe { std::ffi::CStr::from_ptr(path_ptr).to_string_lossy().into_owned() };
    let file = match File::open(&path) {
        Ok(val) => val,
        Err(_) => return null(),
    };

    // TODO move to init fn?
    jxl_oxide::integration::register_image_decoding_hook();
    // TODO https://github.com/Cykooz/libheif-rs/blob/master/src/integration/image.rs has 4 hook register fns: heif, heic and avif and all, latter registering other 3
    // - what is heif hook? Isn't heif container used by both avif and heic?
    // - does avif hook override image-rs own avif support?
    #[cfg(feature = "libheif")]
    libheif_rs::integration::image::register_heic_decoding_hook();
    // TODO image-rs own avif support seems disabled even with libheif-rs only declared as optional dep and not enabled
    #[cfg(feature = "libheif")]
    libheif_rs::integration::image::register_avif_decoding_hook();

    let reader = match ImageReader::new(BufReader::new(file)).with_guessed_format() {
        Ok(val) => val,
        Err(_) => return null(),
    };

    // simpler version without animation support
    //let decoder = match reader.into_decoder() {
    //    Ok(val) => val,
    //    Err(_) => return null(),
    //};

    // because image-rs has no generic API for animation decoding, we have to get decoder as concrete type obj for formats which have decoders implementing AnimationDecoder trait
    // we store it as trait obj of supertrait ImageDecoderAny to be able to pass it in generic way and later to cast it to concrete type to use AnimationDecoder if concrete type has it, or to use ImageDecoder
    // we box it to move it to heap, both because trait obj cannot be stored on stack and because decoder has to persist after fn return (opaque ptr)
    let decoder: Box<dyn ImageDecoderAny> = match reader.format() {
        // TODO check format like it is checked inside lib when choosing decoder
        Some(image::ImageFormat::Gif) => match image::codecs::gif::GifDecoder::new(reader.into_inner()) {
            Ok(val) => Box::new(val),
            Err(_) => return null()
        },
        Some(image::ImageFormat::WebP) => match image::codecs::webp::WebPDecoder::new(reader.into_inner()) {
            Ok(val) => Box::new(val),
            Err(_) => return null()
        },
        Some(image::ImageFormat::Png) => match image::codecs::png::PngDecoder::new(reader.into_inner()) {
            Ok(val) => Box::new(val),
            Err(_) => return null()
        },
        // for all other formats: reader.into_decoder() returns decoder of opaque type `impl ImageDecoder + '_`; actually internally it's Box<ImageDecoder> but because it's returned as opaque we have to box it again to satisfy compiler
        _ => match reader.into_decoder() {
            Ok(val) => Box::new(val),
            Err(_) => return null()
        }
    };

    // we box it again because inner box with trait obj holds "fat ptr" (2 ptrs pointing to data and vtable) which is not compatible with C; this moves inner box with "fat ptr" to heap too, and outer box holds "thin ptr" pointing to inner, which can be released and returned to C to be used as opaque
    // decoder can be accessed for consumption (reclaiming ownership, resulting in same `decoder` box as in this fn, with outer box reconstructed and destroyed immediately, and decoder with inner box holding it freed upon getting out of scope) via: `let decoder = *unsafe {Box::from_raw(decoder_ptr as *mut Box<ImageDecoderAny>) };` (typecast ptr which points to inner box -> restore outer box from typecasted ptr -> deref outer box, getting inner box -> bind it to var `decoder`; decoder methods can be called on the box itself, it autoderefs to decoder)
    // decoder can be accessed for inspection (not reclaiming ownership) via `let decoder: &dyn ImageDecoderAny = unsafe { &**(decoder_ptr as *mut Box<dyn ImageDecoderAny>) };` (typecast ptr -> deref-deref-and-borrow, getting ref to decoder without taking ownership of anything -> bind ref to var `decoder`)
    return Box::into_raw(Box::new(decoder)) as *const std::ffi::c_void
}

// mapping is in image-rs image/src/metadata.rs
#[unsafe(no_mangle)]
extern "C" fn image_rs_ffi_get_orientation_as_exif(decoder_ptr: *mut std::ffi::c_void) -> u8 {
    if decoder_ptr.is_null() { return 1; }
    // TODO is `&**`/`&mut **` ok?
    let decoder: &mut dyn ImageDecoderAny = unsafe { &mut **(decoder_ptr as *mut Box<dyn ImageDecoderAny>) };
    return decoder.orientation().unwrap_or(image::metadata::Orientation::NoTransforms).to_exif()
}

// get frames iter, consuming decoder
// decoder_ptr is invalid if non-null ptr is returned; otherwise it's still valid and can be used to decode as still image
#[unsafe(no_mangle)]
extern "C" fn image_rs_ffi_get_frames_iter(decoder_ptr: *mut std::ffi::c_void) -> *const std::ffi::c_void {
    if decoder_ptr.is_null() { return null(); }

    // for casting decoder to concrete type we need to cast it to Any first
    // decoder inspection (type check, non-consuming method calls) with preservation of decoder_ptr validity requires reference to decoder itself; it's not possible to cast ref to box to ref to box with other inner type
    // decoder consumption requires owned box with decoder; it's not possible to own trait obj directly; reclaiming ownership moves box to stack and invalidates decoder_ptr, so it is done only after it has been verified that decoder can be used to decode animation

    let decoder: &dyn std::any::Any = unsafe { &**(decoder_ptr as *mut Box<dyn ImageDecoderAny>) };

    if decoder.is::<image::codecs::gif::GifDecoder<BufReader<File>>>() {
        let decoder_as_owned = *unsafe { Box::from_raw(decoder_ptr as *mut Box<dyn ImageDecoderAny>) } as Box<dyn std::any::Any>;
        // TODO handle err
        // there's problem with these possible errs after retaking ownership of decoder: decoder_ptr is no longer valid; if we can't return frames ptr and decoder_ptr is invalid, we can't continue execution with this state; it might be possible to do some extremely unsafe trick to restore validity of decoder_ptr, but these errs are practically impossible; for now just .unwrap() to terminate if it happens
        let decoder_as_owned_concrete = decoder_as_owned.downcast::<image::codecs::gif::GifDecoder<BufReader<File>>>().unwrap();
        let frames = decoder_as_owned_concrete.into_frames();
        return Box::into_raw(Box::new(frames)) as *const std::ffi::c_void;
    }

    if let Some(decoder_as_concrete) = decoder.downcast_ref::<image::codecs::webp::WebPDecoder<BufReader<File>>>() {
        if decoder_as_concrete.has_animation() {
            let decoder_as_owned = *unsafe { Box::from_raw(decoder_ptr as *mut Box<dyn ImageDecoderAny>) } as Box<dyn std::any::Any>;
            // TODO handle err
            let decoder_as_owned_concrete = decoder_as_owned.downcast::<image::codecs::webp::WebPDecoder<BufReader<File>>>().unwrap();
            let frames = decoder_as_owned_concrete.into_frames();
            return Box::into_raw(Box::new(frames)) as *const std::ffi::c_void;
        }
        return null();
    }

    if let Some(decoder_as_concrete) = decoder.downcast_ref::<image::codecs::png::PngDecoder<BufReader<File>>>() {
        if let Ok(true) = decoder_as_concrete.is_apng() {
            let decoder_as_owned = *unsafe { Box::from_raw(decoder_ptr as *mut Box<dyn ImageDecoderAny>) } as Box<dyn std::any::Any>;
            // TODO handle err
            let decoder_as_owned_concrete = decoder_as_owned.downcast::<image::codecs::png::PngDecoder<BufReader<File>>>().unwrap();
            let apng_decoder = decoder_as_owned_concrete.apng().unwrap();
            let frames = apng_decoder.into_frames();
            return Box::into_raw(Box::new(frames)) as *const std::ffi::c_void;
        }
        return null();
    }

    // not anim capable decoder
    return null()
}

// get all frames, consuming frames_iter
// frames_iter_ptr is invalid after this
#[unsafe(no_mangle)]
extern "C" fn image_rs_ffi_get_frames(frames_iter_ptr: *mut std::ffi::c_void) -> image_rs_ffi_Frames {
    if frames_iter_ptr.is_null() { return image_rs_ffi_Frames{
        ffi_frames_vec_data: null_mut(),
        count: 0,
        frames_vec: null_mut(),
        ffi_frames_vec: null_mut()
    }; }
    let frames_iter: image::Frames<'static> = *unsafe { Box::from_raw(frames_iter_ptr as *mut image::Frames<'static>) };
    // consume iter and get `Vec<Frame>` of remaining frames
    // TODO this is actually "all or none", use .next() to get as many frames as possible to handle broken files
    let frames_vec: Vec<Frame> = match frames_iter.collect_frames() {
        Ok(val) => val,
        Err(_) => return image_rs_ffi_Frames{
            ffi_frames_vec_data: null_mut(),
            count: 0,
            frames_vec: null_mut(),
            ffi_frames_vec: null_mut()
        }
    };

    // construct C-accessible view
    let mut ffi_frames_vec: Vec<image_rs_ffi_Frame> = Vec::with_capacity(frames_vec.len());

    for frame in frames_vec.iter() {
        let rgba_image: &image::RgbaImage = frame.buffer();
        let (delay_numer_ms, delay_denom_ms) = frame.delay().numer_denom_ms();
        //dbg!(delay_numer_ms, delay_denom_ms);

        ffi_frames_vec.push(image_rs_ffi_Frame {
            // as_raw() returns underlying raw buf as &Container which is &Vec<u8>
            // there's also rgba_image.as_ptr() which returns as ptr to same data as Vec<Rgba<u8>>; as_raw() cleaner
            buf: rgba_image.as_raw().as_ptr(),
            buf_len: rgba_image.as_raw().len(),

            width: rgba_image.width(),
            height: rgba_image.height(),

            x_offset: frame.left(),
            y_offset: frame.top(),

            delay_numer_ms: delay_numer_ms,
            delay_denom_ms: delay_denom_ms,
        });
    }

    return image_rs_ffi_Frames {
        ffi_frames_vec_data: ffi_frames_vec.as_ptr(),
        count: ffi_frames_vec.len(),

        // move vecs to heap and get ptrs, releasing ownership
        // ptr to `Vec<Frame>`, items of which own px buffers, ptrs to which are in ffi_frames_vec_data items
        frames_vec: Box::into_raw(Box::new(frames_vec)),
        // ptr to `Vec<FfiFrame>`, which owns ffi_frames_vec_data
        ffi_frames_vec: Box::into_raw(Box::new(ffi_frames_vec)),
    }
}

#[unsafe(no_mangle)]
extern "C" fn image_rs_ffi_free_frames(ffi_frames: image_rs_ffi_Frames) {
    if !ffi_frames.frames_vec.is_null() {
        unsafe { drop(Box::from_raw(ffi_frames.frames_vec)); }
    }
    if !ffi_frames.ffi_frames_vec.is_null() {
        unsafe { drop(Box::from_raw(ffi_frames.ffi_frames_vec)); }
    }
}

// decode (as still) image, consuming decoder
// decoder_ptr is invalid after this
#[unsafe(no_mangle)]
extern "C" fn image_rs_ffi_get_rgba_image(decoder_ptr: *mut std::ffi::c_void) -> image_rs_ffi_RgbaImage {
    if decoder_ptr.is_null() { return image_rs_ffi_RgbaImage {
        buf: null_mut(),
        buf_len: 0,
        buf_vec_cap: 0,
        width: 0,
        height: 0
    }; }
    let decoder: Box<dyn ImageDecoderAny> = *unsafe { Box::from_raw(decoder_ptr as *mut Box<dyn ImageDecoderAny>) };
    let rgba_image = match image::DynamicImage::from_decoder(decoder) {
        Ok(val) => val.to_rgba8(), // image::RgbaImage
        Err(_) => return image_rs_ffi_RgbaImage {
            buf: null_mut(),
            buf_len: 0,
            buf_vec_cap: 0,
            width: 0,
            height: 0
        }
    };

    let (width, height) = rgba_image.dimensions();
    let mut rgba_image_raw_buf = rgba_image.into_raw();
    let ffi_rgba_image = image_rs_ffi_RgbaImage {
        buf: rgba_image_raw_buf.as_mut_ptr(),
        buf_len: rgba_image_raw_buf.len(),
        buf_vec_cap: rgba_image_raw_buf.capacity(),
        width: width,
        height: height,
    };
    std::mem::forget(rgba_image_raw_buf);
    return ffi_rgba_image
}

// free decoded (still) image
// it is stored as Rust vector, which can be "reconstructed" using data ptr, len and cap
#[unsafe(no_mangle)]
extern "C" fn image_rs_ffi_free_rgba_image(ffi_rgba_image: image_rs_ffi_RgbaImage) {
    if ffi_rgba_image.buf.is_null() { return; }
    unsafe { drop(Vec::from_raw_parts(ffi_rgba_image.buf, ffi_rgba_image.buf_len, ffi_rgba_image.buf_vec_cap)); }
}
