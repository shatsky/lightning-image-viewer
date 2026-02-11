use sdl3_sys::everything::*;
use std::process::exit;

// & str because const strings cannot be owned
const APP_NAME: &std::ffi::CStr = c"Lightning Image Viewer";
const WIN_TITLE_TAIL: &str = " - Lightning Image Viewer"; // native Rust str for convenient concat

// amount of pixels to pan when pressing an arrow key
// leaflet.js keyboardPanDelta default value is 80 https://leafletjs.com/reference.html
const KEYBOARD_PAN_DELTA: f32 = 40.;

// build time config
// shadow
// usually defined through exp and offset_{x, y}
// expand: 6px; offset_x: 0px; offset_y: 1px
// top, left: expand-offset_{y, x}
// right, bottom: expand+offset_{x, y}
const FRAME_WIDTH_TOP: i32 = 5;
const FRAME_WIDTH_RIGHT: i32 = 6;
const FRAME_WIDTH_BOTTOM: i32 = 7;
const FRAME_WIDTH_LEFT: i32 = 6;
const FRAME_COLOR: (u8, u8, u8, u8) = (0x00, 0x00, 0x00, 38);
// non fullscreen
const IMAGE_BACKGROUND_COLOR: (u8, u8, u8, u8) = (0xff, 0xff, 0xff, 0xff);

static CONTEXT_MENU_MSG: &std::ffi::CStr =
c"Context menu is not implemented. Controls summary:\n\
- zoom with scroll or keyboard +=/-/0\n\
- pan with left mouse button pressed or keyboard arrows\n\
- toggle fullscreen with middle click or F/F11\n\
- toggle animation playback with Space\n\
- rotate with R/L, mirror with M\n\
- switch prev/next with PgUp/PgDn\n\
- close with left click or Enter/Esc/Q\n\
\n\
Detailed controls description is in README.\n\
\n\
Lightning Image Viewer (ɔ) Eugene Shatsky 2021-2026\n\
\n\
Licensed under GPLv3+. Originally published at\n\
https://github.com/shatsky/lightning-image-viewer";

static EXIT_EXPL_MSG: &std::ffi::CStr =
c"Normal behavior of this app is to exit upon left mouse click (if no movement\n\
happened between press and release) or keyboard Enter press. This is a feature\n\
allowing toggling between file manager and image view. However, this instance\n\
of app was launched without file cmdline arg, which happens if app is launched\n\
via app launcher; it is not useful way to use this app, but new users often do\n\
it; new users are also often confused by this exit behavior; therefore this\n\
message is shown instead on 1st occurrence of any of specified input events.\n\
Upon next occurrence app will exit.\n\
\n\
Please associate app with supported image file types and launch it via opening\n\
image files from file manager to use it as intended.";

// design
// - state obj holds (most of) persistent state incl. view_rect which defines current image size and position on display
// - view_* functions redraw via render_window()
// - event loop in main()
// - both still and anim images are treated as vec of frames; still image vec has single frame with delay=0; anim playback code in event loop is skipped if vec len<2

// review notes
// - rewrite from C, intentionally close to original C source; I don't like sdl3-rs discarding SDL original entity naming, using unsafe sdl3-sys for now
// - things with process lifetimes incl. SDL window and renderer are not managed, exit() and rely on OS to clean up; why not?
// - SDL_CreateTextureFromSurface and SDL_SetWindowTitle don't need surface and str after call, ok to drop immediately
// - I prefer explicit SDL event loop

// TODO print errors provided in Result enums when loading image, incl. partial anim load
// TODO atomic image view state, in image_load() create new and replace/switch upon success, loaded_images vec for cache/preload?
// TODO ensure that at zoom_level = 0 scale is precisely 1 and rendering is pixel perfect; floor rect (x, y), avoid pow() loss of precision for even zoom levels?
// TODO don't drag or zoom if cursor out of view_rect?
// TODO scale/position limits/constraints?
// TODO ensure that during scroll zoom same image pixel remains under cursor; active point?
// TODO create window after image is loaded? (but window has to exist when image texture is created?)
// TODO display GUI error msgs?
// TODO add macro to "call function, check return val, log err and exit in case of failure" for bool SDL functions which return false in case of failure and set err

// TODO image-rs and SDL types
struct Frame {
    texture: *mut SDL_Texture,
    //width: i32,
    //height: i32,
    //x_offset: i32,
    //y_offset: i32,
    delay: u64
}

impl Drop for Frame {
    fn drop(&mut self) {
        unsafe{SDL_DestroyTexture(self.texture);} // frame texture ptr always valid, frame only pushed to vec if SDL_CreateTextureFromSurface() succeeds
    }
}

// TODO c_int = i32, c_float = f32? https://doc.rust-lang.org/beta/core/ffi/index.html
struct State {
    // cur_x, _y: coords of current point (under cursor)
    // pre_mv: at start of move (drag) action
    show_exit_expl: bool, // need to show explain msg on left click or Enter instead of exit
    file_load_path: std::ffi::OsString,
    file_load_initial: bool,
    file_load_success: bool,
    file_dialog_semaphore: *mut SDL_Semaphore,
    window: *mut SDL_Window,
    win_w: i32,
    win_h: i32,
    win_cur_x: f32,
    win_cur_y: f32,
    win_pre_mv_cur_x: f32,
    win_pre_mv_cur_y: f32,
    win_fullscreen: bool,
    renderer: *mut SDL_Renderer,
    // SDL has:
    // - SDL_Surface which is pixmap in process mem (in RAM) used for software manipulation and rendering
    // - SDL_Texture which references "texture" entity owned by graphics hardware driver (usually in VRAM) used for hardware accelerated rendering
    // image file is first decoded to SDL_Surface, which is then loaded to SDL_Texture via SDL_CreateTextureFromSurface()
    img_w: i32,
    img_h: i32,
    img_cur_x: f32,
    img_cur_y: f32,
    // image presentation area size and position (coords of top left corner relative to window)
    view_rect: SDL_FRect,
    view_zoom_level: i32, // 0 is for 1:1
    view_zoom_scale: f32,
    view_rect_pre_mv_x: f32,
    view_rect_pre_mv_y: f32,
    // init: initial after loading image/resetting view
    // rotate_angle_q: 1/4-turns
    view_init_rotate_angle_q: i32,
    view_init_mirror: bool,
    view_rotate_angle_q: i32,
    view_mirror: bool,
    filelist: Vec<std::ffi::OsString>, // list of filenames in dir
    filelist_cur: usize, // index of filelist item name of which is currently pointed by state.file_load_path
    anim_frames: Vec<Frame>,
    anim_cur: usize,
    anim_next_frame_time: u64,
    anim_paused: bool,
    anim_paused_time: u64,
}

// construct state with default values and get ready for loading image and calling view_* functions
fn new_state() -> State {
    // TODO current display in multi monitor setup?
    // TODO does it make sense that SDL requires window size with SDL_WINDOW_MAXIMIZED?
    if !unsafe{SDL_Init(SDL_INIT_VIDEO)} {
        unsafe{SDL_Log(c"SDL_Init failed: %s".as_ptr(), SDL_GetError());}
        exit(1);
    }
    let display: SDL_DisplayID = unsafe{SDL_GetPrimaryDisplay()};
    if display == 0 {
        unsafe{SDL_Log(c"SDL_GetPrimaryDisplay failed: %s".as_ptr(), SDL_GetError());}
        exit(1);
    }
    // TODO use SDL_GetDisplayBounds() and SDL_GetDisplayUsableBounds()?
    // on Plasma Wayland with Wayland backend SDL_GetDisplayUsableBounds() reports same size as display mode, with X11/XWayland backend it reports incorrect values which seem to be correct size (display mode size minus taskbar) divided by wrong scaling factor (1.2 when I have it set to 1.5, also with X11/XWayland backend app is not scaled by default, so applying scaling factor here makes no sense)
    let display_mode: *const SDL_DisplayMode = unsafe{SDL_GetDesktopDisplayMode(display)}; // free(display_mode): never, this doesn't allocate mem, returns pointer to global
    if display_mode.is_null() {
        unsafe{SDL_Log(c"SDL_GetDesktopDisplayMode failed: %s".as_ptr(), SDL_GetError());}
        exit(1);
    }
    // TODO is window created on primary display?
    // SDL2 SDL_CreateWindow() had x, y position parameters seemingly assuming global desktop coordinate system so that x, y from SDL_GetDisplayBounds() could be used to request compositor to place window at the top left corner of the given display (if implemented in protocol and SDL backend); SDL3 doesn't have them anymore
    let mut window: *mut SDL_Window = std::ptr::null_mut();
    let mut renderer: *mut SDL_Renderer = std::ptr::null_mut();
    if !unsafe{SDL_CreateWindowAndRenderer(APP_NAME.as_ptr(), (*display_mode).w, (*display_mode).h, SDL_WINDOW_BORDERLESS|SDL_WINDOW_MAXIMIZED|SDL_WINDOW_TRANSPARENT, &mut window, &mut renderer)} {
        unsafe{SDL_Log(c"SDL_CreateWindowAndRenderer failed: %s".as_ptr(), SDL_GetError());}
        exit(1);
    }
    let mut win_w: i32 = 0;
    let mut win_h: i32 = 0;
    if !unsafe{SDL_GetWindowSize(window, &mut win_w, &mut win_h)} {
        unsafe{SDL_Log(c"SDL_GetWindowSize failed: %s".as_ptr(), SDL_GetError());}
        exit(1);
    }
    State {
        show_exit_expl: false,
        file_load_path: std::ffi::OsString::new(),
        file_load_initial: true,
        file_load_success: false,
        file_dialog_semaphore: std::ptr::null_mut(),
        window: window,
        win_w: win_w,
        win_h: win_h,
        img_cur_x: 0.,
        img_cur_y: 0.,
        win_cur_x: 0.,
        win_cur_y: 0.,
        win_pre_mv_cur_x: 0.,
        win_pre_mv_cur_y: 0.,
        win_fullscreen: false,
        renderer: renderer,
        img_w: 0,
        img_h: 0,
        view_rect: SDL_FRect::default(),
        view_zoom_level: 0,
        view_zoom_scale: 1.,
        view_rect_pre_mv_x: 0.,
        view_rect_pre_mv_y: 0.,
        view_init_rotate_angle_q: 0,
        view_init_mirror: false,
        view_rotate_angle_q: 0,
        view_mirror: false,
        filelist: Vec::<std::ffi::OsString>::new(),
        filelist_cur: 0,
        anim_frames: Vec::<Frame>::new(),
        anim_cur: 0,
        anim_next_frame_time: 0,
        anim_paused: true,
        anim_paused_time: 0,
    }
}

// not to confuse SDL filelist provided by SDL here with state.filelist
// SDL C examples use `static` which makes fn visibility scope limited to file and is irrelevant in Rust
extern "C" fn file_dialog_callback(userdata: *mut std::ffi::c_void, filelist: *const *const std::ffi::c_char, _filter: i32) {
    if filelist.is_null() {
        unsafe{SDL_Log(c"SDL_ShowOpenFileDialog failed: %s".as_ptr(), SDL_GetError());}
        exit(1);
    } else {
        if !unsafe{*filelist}.is_null() {
            let state = unsafe{&mut *(userdata as *mut State)};
            // SDL3 provides path as ptr to C str transcoded to UTF8, need to transcode back to platform str
            state.file_load_path = std::ffi::OsString::from(unsafe{std::ffi::CStr::from_ptr(*filelist)}.to_str().expect("failed to decode UTF8 C string from SDL_ShowOpenFileDialog callback"));
            state.filelist.clear();
            unsafe{SDL_SignalSemaphore(state.file_dialog_semaphore);}
        } else {
            eprintln!("SDL_ShowOpenFileDialog returned empty filelist");
            exit(1);
        }
    }
}

impl State {
    // set initial orientation, called upon loading image
    fn set_init_orient(&mut self, exif_orientation: u8) {
        match exif_orientation {
            // TODO expression? Declare array and select from it by index?
            //1 => {
            //    self.view_init_rotate_angle_q = 0;
            //    self.view_init_mirror = false;
            //}
            2 => {
                self.view_init_rotate_angle_q = 0;
                self.view_init_mirror = true;
            }
            3 => {
                self.view_init_rotate_angle_q = 2;
                self.view_init_mirror = false;
            }
            4 => {
                self.view_init_rotate_angle_q = 2;
                self.view_init_mirror = true;
            }
            5 => {
                self.view_init_rotate_angle_q = 1;
                self.view_init_mirror = true;
            }
            6 => {
                self.view_init_rotate_angle_q = 1;
                self.view_init_mirror = false;
            }
            7 => {
                self.view_init_rotate_angle_q = 3;
                self.view_init_mirror = true;
            }
            8 => {
                self.view_init_rotate_angle_q = 3;
                self.view_init_mirror = false;
            }
            _ => {
                self.view_init_rotate_angle_q = 0;
                self.view_init_mirror = false;
            }
        }
    }

    // non-redrawing, only update scale and view_rect size
    fn set_zoom_level(&mut self, view_zoom_level: i32) {
        self.view_zoom_level = view_zoom_level;
        // scale = sqrt(2)^zoom_level = 2^(0.5*zoom_level)
        self.view_zoom_scale = (0.5 * view_zoom_level as f32).exp2();
        self.view_rect.w = self.img_w as f32 * self.view_zoom_scale;
        self.view_rect.h = self.img_h as f32 * self.view_zoom_scale;
    }

    // redraw window contents with current state
    fn render_window(&self) {
        if !unsafe{SDL_RenderClear(self.renderer)} {
            unsafe{SDL_Log(c"SDL_RenderClear failed: %s".as_ptr(), SDL_GetError());}
            exit(1);
        }
        let mut view_rect;
        if self.win_fullscreen {
            // for fullscreen, construct view_rect without using state.view_rect
            view_rect = SDL_FRect::default();
            // rotate and scale to fit width, if height does not fit scale to fit height
            let (img_w, img_h) = if self.view_rotate_angle_q%2==1 {(self.img_h, self.img_w)} else {(self.img_w, self.img_h)};
            view_rect.w = self.win_w as f32;
            view_rect.h = (img_h * self.win_w) as f32 / img_w as f32;
            if view_rect.h > self.win_h as f32 {
                view_rect.w = (img_w * self.win_h) as f32 / img_h as f32;
                view_rect.h = self.win_h as f32;
            }
            // center
            view_rect.x = (self.win_w as f32 - view_rect.w) / 2.;
            view_rect.y = (self.win_h as f32 - view_rect.h) / 2.;
            // align to display pixels
            view_rect.x = view_rect.x.floor();
            view_rect.y = view_rect.y.floor();
        } else {
            view_rect = self.view_rect;
            // rotate
            if self.view_rotate_angle_q%2==1 {
                view_rect.x += (view_rect.w - view_rect.h) / 2.;
                view_rect.y += (view_rect.h - view_rect.w) / 2.;
                (view_rect.w, view_rect.h) = (view_rect.h, view_rect.w);
            }
            // align to display pixels
            view_rect.x = view_rect.x.floor();
            view_rect.y = view_rect.y.floor();
            // draw shadow and image bg
            let view_rect_saved = view_rect;
            view_rect.x -= FRAME_WIDTH_LEFT as f32;
            view_rect.y -= FRAME_WIDTH_TOP as f32;
            view_rect.w += (FRAME_WIDTH_LEFT + FRAME_WIDTH_RIGHT) as f32;
            view_rect.h += (FRAME_WIDTH_TOP + FRAME_WIDTH_BOTTOM) as f32;
            if !unsafe{SDL_SetRenderDrawColor(self.renderer, FRAME_COLOR.0, FRAME_COLOR.1, FRAME_COLOR.2, FRAME_COLOR.3)} {
                unsafe{SDL_Log(c"SDL_SetRenderDrawColor failed: %s".as_ptr(), SDL_GetError());}
                exit(1);
            }
            if !unsafe{SDL_RenderFillRect(self.renderer, &view_rect)} {
                unsafe{SDL_Log(c"SDL_RenderFillRect failed: %s".as_ptr(), SDL_GetError());}
                exit(1);
            }
            view_rect = view_rect_saved;
            if !unsafe{SDL_SetRenderDrawColor(self.renderer, IMAGE_BACKGROUND_COLOR.0, IMAGE_BACKGROUND_COLOR.1, IMAGE_BACKGROUND_COLOR.2, IMAGE_BACKGROUND_COLOR.3)} {
                unsafe{SDL_Log(c"SDL_SetRenderDrawColor failed: %s".as_ptr(), SDL_GetError());}
                exit(1);
            }
            if !unsafe{SDL_RenderFillRect(self.renderer, &view_rect)} {
                unsafe{SDL_Log(c"SDL_RenderFillRect failed: %s".as_ptr(), SDL_GetError());}
                exit(1);
            }
            if !unsafe{SDL_SetRenderDrawColor(self.renderer, 0, 0, 0, SDL_ALPHA_TRANSPARENT)} {
                unsafe{SDL_Log(c"SDL_SetRenderDrawColor failed: %s".as_ptr(), SDL_GetError())};
                exit(1);
            }
            view_rect = view_rect_saved;
        }
        // rotate again to restore non rotated because SDL_RenderTextureRotated needs non rotated
        // rotation was nessessary in any case to get correct coords to align to display pixels for pixel perfect rendering
        if self.view_rotate_angle_q%2==1 {
            view_rect.x += (view_rect.w - view_rect.h) / 2.;
            view_rect.y += (view_rect.h - view_rect.w) / 2.;
            (view_rect.w, view_rect.h) = (view_rect.h, view_rect.w);
        }
        // copy image to presentation area in renderer backbuffer
        // TODO image-rs image::Frame has dimensions and offsets, suggesting it can be subregion of full image area, but it seems that all 3 decoders currently implementing AnimationDecoder always return pre composed frames with full dimensions and zero offsets
        // API-safe impl would need to clear texture subregion and blit into it
        // use intermediate composed texture or compose directly into window buf? Latter would require re-composing frame starting with last full frame at least if view_rect has changed
        if !unsafe{SDL_RenderTextureRotated(self.renderer, self.anim_frames[self.anim_cur].texture, std::ptr::null(), &view_rect, (self.view_rotate_angle_q*90) as f64, std::ptr::null(), if self.view_mirror {if self.view_rotate_angle_q%2==1 {SDL_FLIP_VERTICAL} else {SDL_FLIP_HORIZONTAL}} else {SDL_FLIP_NONE})} {
            unsafe{SDL_Log(c"SDL_RenderTextureRotated failed: %s".as_ptr(), SDL_GetError());}
            exit(1);
        }
        // copy renderer backbuffer to frontbuffer
        if !unsafe{SDL_RenderPresent(self.renderer)} {
            unsafe{SDL_Log(c"SDL_RenderPresent failed: %s".as_ptr(), SDL_GetError());}
            exit(1);
        }
    }

    // reset view_rect to initial scale and position
    fn view_reset(&mut self) {
        self.view_rotate_angle_q = self.view_init_rotate_angle_q;
        self.view_mirror = self.view_init_mirror;
        // set max zoom level at which entire image fits in window
        // scale to fit width, if height does not fit scale to fit height
        let (img_w, img_h) = if self.view_rotate_angle_q%2==1 {(self.img_h, self.img_w)} else {(self.img_w, self.img_h)};
        // zoom_level = 2*log2(scale)
        self.set_zoom_level((2. * (self.win_w as f32 / img_w as f32).log2()).floor() as i32);
        if self.view_rect.h > self.win_h as f32 {
            self.set_zoom_level((2. * (self.win_h as f32 / img_h as f32).log2()).floor() as i32);
        }
        // center
        self.view_rect.x = (self.win_w as f32 - self.view_rect.w) / 2.;
        self.view_rect.y = (self.win_h as f32 - self.view_rect.h) / 2.;
        self.render_window();
    }

    // image loading is split into several functions primarily because of image-rs lack of generic animation decoding API, requiring to compose execution flow differently for different formats
    // only terminate upon failure of functions which shouldn't fail because of invalid image file
    // only print non-terminal errs for initial file; when switching prev/next, loading of many non-image files may be attempted

    fn load_image_metadata(&mut self, decoder: &mut impl image::ImageDecoder) {
        // TODO image/frame dimensions can be obtained on several steps of pipeline; should be consistent, but what if not? For now, I use dimensions from ImageDecoder for state fields here, and dimensions from ImageBuffer for SDL_CreateSurfaceFrom() args when decoding frames; might be better to update state fields with every frame if frame dimensions are greater than state fields values; to account for something like those broken videos sticking out of initial frame rect
        let (width, height) = decoder.dimensions();
        self.img_w = width as i32;
        self.img_h = height as i32;
        let exif_orientation = decoder.orientation().unwrap_or(image::metadata::Orientation::NoTransforms).to_exif();
        self.set_init_orient(exif_orientation);
    }

    fn load_image_anim<'a>(&mut self, decoder: impl image::AnimationDecoder<'a>) {
        let frames = decoder.into_frames();
        for frame in frames {
            let Ok(frame) = frame else {
                if self.file_load_initial && self.anim_frames.len()==0 {
                    eprintln!("failed to get image from decoder");
                }
                break;
            };
            let (delay_numer_ms, delay_denom_ms) = frame.delay().numer_denom_ms();
            if delay_denom_ms == 0 {
                if self.file_load_initial && self.anim_frames.len()==0 {
                    eprintln!("bad image frame delay");
                }
                break;
            }
            let buffer = frame.into_buffer();
            let (width, height) = buffer.dimensions();
            let mut raw = buffer.into_raw();
            if width>i32::MAX as u32/4 || (height!=0 && width*4>i32::MAX as u32/height) || (width*4*height) as usize>raw.len() {
                if self.file_load_initial && self.anim_frames.len()==0 {
                    eprintln!("bad image dimensions");
                }
                break;
            }
            let surface: *mut SDL_Surface = unsafe{SDL_CreateSurfaceFrom(width as i32, height as i32, SDL_PIXELFORMAT_ABGR8888, raw.as_mut_ptr() as *mut std::ffi::c_void, width as i32*4)};
            if surface.is_null() {
                unsafe{SDL_Log(c"SDL_CreateSurfaceFrom failed: %s".as_ptr(), SDL_GetError());}
                exit(1);
            }
            let texture: *mut SDL_Texture = unsafe{SDL_CreateTextureFromSurface(self.renderer, surface)};
            if texture.is_null() {
                unsafe{SDL_Log(c"SDL_CreateTextureFromSurface failed: %s".as_ptr(), SDL_GetError());}
                exit(1);
            }
            unsafe{SDL_DestroySurface(surface);}
            self.anim_frames.push(Frame {
                texture: texture,
                delay: (delay_numer_ms / delay_denom_ms) as u64
            });
        }
    }

    fn load_image_still(&mut self, decoder: impl image::ImageDecoder) {
        let Ok(image_) = image::DynamicImage::from_decoder(decoder) else {
            if self.file_load_initial {
                eprintln!("failed to get image from decoder");
            }
            return;
        };
        let rgba8 = image_.to_rgba8();
        let (width, height) = (rgba8.width(), rgba8.height());
        let mut raw = rgba8.into_raw();
        if width>i32::MAX as u32/4 || (height!=0 && width*4>i32::MAX as u32/height) || (width*4*height) as usize>raw.len() {
            if self.file_load_initial {
                eprintln!("bad image dimensions");
            }
            return;
        }
        let surface: *mut SDL_Surface = unsafe{SDL_CreateSurfaceFrom(width as i32, height as i32, SDL_PIXELFORMAT_ABGR8888, raw.as_mut_ptr() as *mut std::ffi::c_void, width as i32*4)};
        if surface.is_null() {
            unsafe{SDL_Log(c"SDL_CreateSurfaceFrom failed: %s".as_ptr(), SDL_GetError());}
            exit(1);
        }
        let texture = unsafe{SDL_CreateTextureFromSurface(self.renderer, surface)};
        if texture.is_null() {
            unsafe{SDL_Log(c"SDL_CreateTextureFromSurface failed: %s".as_ptr(), SDL_GetError());}
            exit(1);
        }
        unsafe{SDL_DestroySurface(surface);}
        self.anim_frames.push(Frame {
            texture: texture,
            delay: 0
        });
    }

    // load image from current file_load_path
    fn load_image(&mut self) {
        self.file_load_success = false;

        // free previous if loaded
        // after this we can't render until we load an image again; should be ok for single threaded program in which code which calls load_image() does not pass control to event loop until image is loaded successfully
        self.anim_frames.clear();

        let Ok(file) = std::fs::File::open(&self.file_load_path) else {
            if self.file_load_initial {
                eprintln!("failed to open file");
            }
            return;
        };
        let Ok(reader) = image::ImageReader::new(std::io::BufReader::new(file)).with_guessed_format() else {
            if self.file_load_initial {
                eprintln!("failed to get reader from file object");
            }
            return;
        };
        match reader.format() {
            // TODO check format like it is checked inside lib when choosing decoder
            Some(image::ImageFormat::Gif) => {
                let Ok(mut decoder) = image::codecs::gif::GifDecoder::new(reader.into_inner()) else {
                    if self.file_load_initial {
                        eprintln!("failed to get decoder from reader");
                    }
                    return;
                };
                self.load_image_metadata(&mut decoder);
                self.load_image_anim(decoder);
            },
            Some(image::ImageFormat::WebP) => {
                let Ok(mut decoder) = image::codecs::webp::WebPDecoder::new(reader.into_inner()) else {
                    if self.file_load_initial {
                        eprintln!("failed to get decoder from reader");
                    }
                    return;
                };
                self.load_image_metadata(&mut decoder);
                if decoder.has_animation() {
                    self.load_image_anim(decoder);
                } else {
                    self.load_image_still(decoder);
                }
            },
            Some(image::ImageFormat::Png) => {
                let Ok(mut decoder) = image::codecs::png::PngDecoder::new(reader.into_inner()) else {
                    if self.file_load_initial {
                        eprintln!("failed to get decoder from reader");
                    }
                    return;
                };
                self.load_image_metadata(&mut decoder);
                if decoder.is_apng().unwrap_or(false) {
                    let Ok(decoder) = decoder.apng() else {
                        if self.file_load_initial {
                            eprintln!("failed to get decoder adapter from decoder");
                        }
                        return;
                    };
                    self.load_image_anim(decoder);
                } else {
                    self.load_image_still(decoder);
                }
            },
            // for all other formats: reader.into_decoder() returns decoder of opaque type `impl ImageDecoder + '_`; actually internally it's Box<ImageDecoder>
            _ => {
                let Ok(mut decoder) = reader.into_decoder() else {
                    if self.file_load_initial {
                        eprintln!("failed to get decoder from reader");
                    }
                    return;
                };
                self.load_image_metadata(&mut decoder);
                self.load_image_still(decoder);
            }
        };
        if self.anim_frames.len()==0 {
            if self.file_load_initial {
                eprintln!("got and tried to use decoder but failed to get any frames, check preceding messages");
            }
            return;
        }
        self.anim_cur = 0;
        self.anim_next_frame_time = unsafe{SDL_GetTicks()} + self.anim_frames[0].delay;
        self.anim_paused = false;

        self.file_load_success = true;

        // update window title
        // TODO chdir early and always have bare filename in state.file_load_path?
        // TODO on Plasma Wayland window title is split by dash separator and 1st part is displayed in taskbar as filename; appname displayed in taskbar is taken from elsewhere, if app is launched via .desktop file it's Name, if app binary is launched directly it is Name from .desktop file with same binary or icon filename, if no such .desktop file found it is binary filename, if path to binary starts with '.' appname is not displayed at all
        // TODO with X11/XWayland backend if em dash is used as separator (stored as its UTF8 repr in source) title is not updated at all, XWayland bug?
        // TODO #rust more elegant split?
        let file_load_path = std::path::Path::new(&self.file_load_path);
        let file_name = file_load_path.file_name().expect("failed to get filename from file path");
        // SDL needs UTF8 but in some (exotic) envs filename platform string cannot be losslessly transcoded
        let win_title = file_name.to_string_lossy().into_owned() + WIN_TITLE_TAIL;
        if !unsafe{SDL_SetWindowTitle(self.window, std::ffi::CString::new(win_title).expect("failed to encode UTF8 C string for SDL_SetWindowTitle").as_ptr())} { // SDL_SetWindowTitle copies str internally
            unsafe{SDL_Log(c"SDL_SetWindowTitle failed: %s".as_ptr(), SDL_GetError());}
            exit(1);
        }
        self.view_reset()
    }

    // non-redrawing, non-view_rect-changing, only set window state and bg color for subsequent render_window() call
    fn set_win_fullscreen(&mut self, win_fullscreen: bool) {
        self.win_fullscreen = win_fullscreen;
        // TODO clear window?
        if !unsafe{SDL_SetWindowFullscreen(self.window, win_fullscreen)} {
            unsafe{SDL_Log(c"SDL_SetWindowFullscreen failed: %s".as_ptr(), SDL_GetError());}
            exit(1);
        }
        if !unsafe{SDL_SyncWindow(self.window)} {
            unsafe{SDL_Log(c"SDL_SyncWindow timed out".as_ptr());}
        }
        // assuming that window size can change because of shell UI
        if !unsafe{SDL_GetWindowSize(self.window, &mut self.win_w, &mut self.win_h)} {
            unsafe{SDL_Log(c"SDL_GetWindowSize failed: %s".as_ptr(), SDL_GetError());}
            exit(1);
        }
        if !unsafe{SDL_SetRenderDrawColor(self.renderer, 0, 0, 0, if win_fullscreen {SDL_ALPHA_OPAQUE} else {SDL_ALPHA_TRANSPARENT})} {
            unsafe{SDL_Log(c"SDL_SetRenderDrawColor failed: %s".as_ptr(), SDL_GetError());}
            exit(1);
        }
    }

    // save coords at start of move (drag) action which are used to update view_rect pos upon mouse motion; also has to be called if view_rect is changed by other action (zoom) during move
    // before calling this, update win_cur_x, _y
    fn save_pre_mv_coords(&mut self) {
        self.view_rect_pre_mv_x = self.view_rect.x;
        self.view_rect_pre_mv_y = self.view_rect.y;
        self.win_pre_mv_cur_x = self.win_cur_x;
        self.win_pre_mv_cur_y = self.win_cur_y;
    }

    // zoom with preserving image point currently under cursor
    // used as mouse zoom action
    fn view_zoom_to_level_at_cursor(&mut self, view_zoom_level: i32) {
        if self.win_fullscreen {
            self.set_win_fullscreen(false);
        }
        unsafe{SDL_GetMouseState(&mut self.win_cur_x, &mut self.win_cur_y);}
        self.img_cur_x = (self.win_cur_x - self.view_rect.x) / self.view_zoom_scale;
        self.img_cur_y = (self.win_cur_y - self.view_rect.y) / self.view_zoom_scale;
        self.set_zoom_level(view_zoom_level);
        self.view_rect.x = self.win_cur_x - self.img_cur_x * self.view_zoom_scale;
        self.view_rect.y = self.win_cur_y - self.img_cur_y * self.view_zoom_scale;
        // TODO does cursor still lose current image point when zooming while dragging?
        self.save_pre_mv_coords();
        self.render_window();
    }

    // zoom with preserving image point currently in the center of window
    // used as keyboard zoom action
    fn view_zoom_to_level_at_center(&mut self, view_zoom_level: i32) {
        if self.win_fullscreen {
            self.set_win_fullscreen(false);
        }
        let img_center_x: f32 = (self.win_w as f32 / 2. - self.view_rect.x) / self.view_zoom_scale;
        let img_center_y: f32 = (self.win_h as f32 / 2. - self.view_rect.y) / self.view_zoom_scale;
        self.set_zoom_level(view_zoom_level);
        self.view_rect.x = self.win_w as f32 / 2. - img_center_x * self.view_zoom_scale;
        self.view_rect.y = self.win_h as f32 / 2. - img_center_y * self.view_zoom_scale;
        self.save_pre_mv_coords();
        self.render_window();
    }

    // move view_rect from pre_mv pos by vector of cursor movement from pre_mv to current coords
    // used as mouse move action
    fn view_move_from_pre_mv_by_cursor_mv(&mut self) {
        // ignore new motion events until current one is processed (to prevent accumulation of events in queue and image movement lag behind cursor which can happen if app has to redraw for each motion event)
        unsafe{SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION.into(), false);}
        if self.win_fullscreen {
            self.set_win_fullscreen(false);
        }
        self.view_rect.x = self.view_rect_pre_mv_x + (self.win_cur_x - self.win_pre_mv_cur_x);
        self.view_rect.y = self.view_rect_pre_mv_y + (self.win_cur_y - self.win_pre_mv_cur_y);
        self.render_window();
        unsafe{SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION.into(), true);}
    }

    // move view_rect by vector
    // used as keyboard move action
    fn view_move_by_vector(&mut self, x: f32, y: f32) {
        if self.win_fullscreen {
            self.set_win_fullscreen(false);
        }
        self.view_rect.x += x;
        self.view_rect.y += y;
        self.render_window();
    }

    // prev/next image switch

    fn populate_filelist(&mut self) {
        if self.filelist.len() > 0 {
            return;
        }
        let file_load_path = std::path::Path::new(&self.file_load_path);
        // TODO #rust more elegant split?
        let dir_path = file_load_path.parent().expect("failed to get dir path from file path");
        if std::env::set_current_dir(dir_path).is_err() {
            self.filelist.push(self.file_load_path.clone());
            self.filelist_cur = 0;
            return;
        }
        let file_name = file_load_path.file_name().expect("failed to get filename from file path");
        let mut filelist_with_mtimes = Vec::<(std::time::SystemTime, std::ffi::OsString)>::new();
        if let Ok(entries) = std::fs::read_dir(".") {
            for entry in entries.flatten() {
                let Ok(entry_metadata) = entry.metadata() else {continue;};
                if !entry_metadata.is_file() {
                    continue;
                }
                let Ok(entry_mtime) = entry_metadata.modified() else {continue;};
                let entry_mtime_and_filename = (entry_mtime, entry.file_name());
                // find pos to insert to produce ordered list
                // TODO if same mtime, secondary sort by +filename or -filename? (low importance)
                if let Err(i) = filelist_with_mtimes.binary_search(&entry_mtime_and_filename) {
                    filelist_with_mtimes.insert(i, entry_mtime_and_filename)
                }
            }
        }
        for entry_mtime_and_filename in filelist_with_mtimes {
            if entry_mtime_and_filename.1 == file_name {
                self.filelist_cur = self.filelist.len();
            }
            self.filelist.push(entry_mtime_and_filename.1);
        }
        // if no valid entries or current file not found in dir
        if self.filelist[self.filelist_cur] != file_name {
            self.filelist.clear();
        }
        if self.filelist.len() == 0 {
            self.filelist.push(self.file_load_path.clone());
            self.filelist_cur = 0;
        }
    }

    fn load_next_image(&mut self, reverse: bool) {
        // TODO more generic solution for prevention of events accumulation?
        unsafe{SDL_SetEventEnabled(SDL_EVENT_KEY_DOWN.into(), false);}

        self.populate_filelist();

        let filelist_cur_saved = self.filelist_cur;
        loop {
            // filelist is sorted by +mtime but UX/fn semantics assumes -mtime, therefore `!reverse`
            self.filelist_cur = (self.filelist_cur + (if !reverse {self.filelist.len() - 1} else {1})) % self.filelist.len();
            self.file_load_path = self.filelist[self.filelist_cur].clone();
            self.load_image();
            if self.file_load_success {
                break;
            }
            if self.filelist_cur==filelist_cur_saved {
                eprintln!("load_image failed; wrapped around filelist and failed to load any file");
                exit(1);
            }
        }

        unsafe{SDL_SetEventEnabled(SDL_EVENT_KEY_DOWN.into(), true);}
    }
}

fn main() {
    // pre-configuration of libs
    if !unsafe{SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_SCALE_TO_DISPLAY, c"1".as_ptr())} {
        unsafe{SDL_Log(c"SDL_SetHint failed: %s".as_ptr(), SDL_GetError());}
        exit(1);
    }
    jxl_oxide::integration::register_image_decoding_hook();
    // TODO https://github.com/Cykooz/libheif-rs/blob/master/src/integration/image.rs has 4 hook register fns: heif, heic and avif and all, latter registering other 3
    // - what is heif hook? Isn't heif container used by both avif and heic?
    // - does avif hook override image-rs own avif support?
    #[cfg(feature = "libheif")]
    libheif_rs::integration::image::register_heic_decoding_hook();
    // TODO image-rs own avif support seems disabled even with libheif-rs only declared as optional dep and not enabled
    #[cfg(feature = "libheif")]
    libheif_rs::integration::image::register_avif_decoding_hook();

    let mut state = new_state();
    // TODO #rust more elegant? `Some(existing_var) = expr else {...};` (destructure into existing var) seems impossible
    match std::env::args_os().nth(1) {
        Some(val) => state.file_load_path = val,
        None => {
            state.file_dialog_semaphore = unsafe{SDL_CreateSemaphore(0)};
            if state.file_dialog_semaphore.is_null() {
                unsafe{SDL_Log(c"SDL_CreateSemaphore failed: %s".as_ptr(), SDL_GetError());}
                exit(1);
            }
            // 2-step cast because Rust does not allow cast ref to c_void ptr directly
            unsafe{SDL_ShowOpenFileDialog(Some(file_dialog_callback), &mut state as *mut _ as *mut std::ffi::c_void, state.window, std::ptr::null(), 0, std::ptr::null(), false);}
            //unsafe{SDL_WaitSemaphore(state.file_dialog_semaphore);}
            // on some platforms incl. Linux with xdg-desktop-portal it requires event loop to call the callback
            // SDL_WaitEvent(NULL) doesn't work here, it won't run loop body until there's actual SDL event
            // TODO events while waiting for dialog? App exit?
            loop {
                unsafe{SDL_Delay(10);}
                unsafe{SDL_PumpEvents();}
                if unsafe{SDL_TryWaitSemaphore(state.file_dialog_semaphore)} {
                    break;
                }
            }
            state.show_exit_expl = true;
        }
    }
    state.load_image();
    if !state.file_load_success {
        eprintln!("load_image failed; failed to load initial file");
        exit(1);
    }
    state.file_load_initial = false;
    let mut event: SDL_Event = SDL_Event::default();
    let mut lmousebtn_pressed = false;
    let mut should_exit_on_lmousebtn_release = false;
    let mut now: u64;
    loop {
        if state.anim_frames.len()<2 || state.anim_paused {
            if !unsafe{SDL_WaitEvent(&mut event)} {
                unsafe{SDL_Log(c"SDL_WaitEvent failed: %s".as_ptr(), SDL_GetError());}
                break;
            }
        } else {
            // anim
            now = unsafe{SDL_GetTicks()};
            // if next frame time missed or if event queue empty and next frame time arrives before event while waiting for event or next frame time
            // TODO type mismatch: SDL_GetTicks() returns ms as u32 but SDL_WaitEventTimeout() takes ms as i32
            if state.anim_next_frame_time<now ||
               !unsafe{SDL_WaitEventTimeout(&mut event, (state.anim_next_frame_time-now) as i32)} {
                state.anim_cur = (state.anim_cur + 1) % state.anim_frames.len();
                // display next frame if in time, else skip
                if state.anim_next_frame_time >= now as u64 {
                    state.render_window();
                }
                state.anim_next_frame_time += state.anim_frames[state.anim_cur].delay;
                continue;
            }
        }
        match event.event_type() {
            SDL_EVENT_MOUSE_WHEEL => {
                if unsafe{event.wheel}.y != 0. {
                    state.view_zoom_to_level_at_cursor(if unsafe{event.wheel}.y>0. {state.view_zoom_level+1} else {state.view_zoom_level-1});
                    should_exit_on_lmousebtn_release = false;
                }
            }
            SDL_EVENT_MOUSE_MOTION => {
                if lmousebtn_pressed {
                    state.win_cur_x = unsafe{event.motion}.x;
                    state.win_cur_y = unsafe{event.motion}.y;
                    state.view_move_from_pre_mv_by_cursor_mv();
                    should_exit_on_lmousebtn_release = false;
                }
            }
            // TODO type mismatch: SDL_Event.button.button is u8 but SDL_BUTTON_* constants are i32
            // also it's not allowed to use `as` in match patterns
            SDL_EVENT_MOUSE_BUTTON_DOWN => {
                match unsafe{event.button}.button as i32 {
                    SDL_BUTTON_LEFT => {
                        lmousebtn_pressed = true;
                        unsafe{SDL_GetMouseState(&mut state.win_cur_x, &mut state.win_cur_y);}
                        state.save_pre_mv_coords();
                        should_exit_on_lmousebtn_release = true;
                    }
                    SDL_BUTTON_RIGHT => {
                        unsafe{SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags(0), APP_NAME.as_ptr(), CONTEXT_MENU_MSG.as_ptr(), state.window);}
                    }
                    _ => {}
                }
            }
            SDL_EVENT_MOUSE_BUTTON_UP => {
                match unsafe{event.button}.button as i32 {
                    SDL_BUTTON_LEFT => {
                        if should_exit_on_lmousebtn_release {
                            if state.show_exit_expl {
                                state.show_exit_expl = false;
                                unsafe{SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags(0), APP_NAME.as_ptr(), EXIT_EXPL_MSG.as_ptr(), state.window);}
                            } else {
                                exit(0);
                            }
                        }
                        lmousebtn_pressed = false;
                    }
                    SDL_BUTTON_MIDDLE => {
                        state.set_win_fullscreen(!state.win_fullscreen);
                        state.render_window();
                    }
                    _ => {}
                }
            }
            SDL_EVENT_KEY_DOWN => {
                //eprintln!{"key scancode: {}", unsafe{event.key}.scancode);}
                match unsafe{event.key}.scancode {
                    SDL_SCANCODE_F |
                    SDL_SCANCODE_F11 => {
                        // toggle fullscreen
                        state.set_win_fullscreen(!state.win_fullscreen);
                        state.render_window();
                    }
                    SDL_SCANCODE_L => {
                        // rotate counter clockwise
                        state.view_rotate_angle_q = (state.view_rotate_angle_q + (if state.view_mirror {1} else {3})) % 4;
                        state.render_window();
                    }
                    SDL_SCANCODE_M => {
                        // mirror horizontally
                        state.view_mirror = !state.view_mirror;
                        state.render_window();
                    }
                    SDL_SCANCODE_Q |
                    SDL_SCANCODE_ESCAPE => {
                        // quit
                        exit(0);
                    }
                    SDL_SCANCODE_R => {
                        // rotate clockwise
                        state.view_rotate_angle_q = (state.view_rotate_angle_q + (if state.view_mirror {3} else {1})) % 4;
                        state.render_window();
                    }
                    SDL_SCANCODE_0 |
                    SDL_SCANCODE_KP_0 => {
                        // zoom 1:1
                        if lmousebtn_pressed {
                            should_exit_on_lmousebtn_release = false;
                            state.view_zoom_to_level_at_cursor(0);
                        } else {
                            state.view_zoom_to_level_at_center(0);
                        }
                    }
                    SDL_SCANCODE_RETURN |
                    SDL_SCANCODE_KP_ENTER => {
                        // quit
                        if state.show_exit_expl {
                            state.show_exit_expl = false;
                            unsafe{SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags(0), APP_NAME.as_ptr(), EXIT_EXPL_MSG.as_ptr(), state.window);}
                        } else {
                            exit(0);
                        }
                    }
                    SDL_SCANCODE_SPACE => {
                        // toggle animation playback
                        if !state.anim_paused {
                            state.anim_paused_time = unsafe{SDL_GetTicks()};
                        } else {
                            state.anim_next_frame_time += unsafe{SDL_GetTicks()} - state.anim_paused_time;
                        }
                        state.anim_paused = !state.anim_paused;
                    }
                    SDL_SCANCODE_MINUS |
                    SDL_SCANCODE_KP_MINUS => {
                        // zoom out
                        if lmousebtn_pressed {
                            should_exit_on_lmousebtn_release = false;
                            state.view_zoom_to_level_at_cursor(state.view_zoom_level-1);
                        } else {
                            state.view_zoom_to_level_at_center(state.view_zoom_level-1);
                        }
                    }
                    SDL_SCANCODE_EQUALS |
                    SDL_SCANCODE_KP_PLUS => {
                        // zoom in
                        if lmousebtn_pressed {
                            should_exit_on_lmousebtn_release = false;
                            state.view_zoom_to_level_at_cursor(state.view_zoom_level+1);
                        } else {
                            state.view_zoom_to_level_at_center(state.view_zoom_level+1);
                        }
                    }
                    SDL_SCANCODE_PAGEUP |
                    SDL_SCANCODE_KP_9 => {
                        // prev
                        state.load_next_image(true);
                    }
                    SDL_SCANCODE_PAGEDOWN |
                    SDL_SCANCODE_KP_3 => {
                        // next
                        state.load_next_image(false);
                    }
                    SDL_SCANCODE_RIGHT |
                    SDL_SCANCODE_KP_6 => {
                        // move right
                        state.view_move_by_vector(-KEYBOARD_PAN_DELTA, 0.);
                    }
                    SDL_SCANCODE_LEFT |
                    SDL_SCANCODE_KP_4 => {
                        // move left
                        state.view_move_by_vector(KEYBOARD_PAN_DELTA, 0.);
                    }
                    SDL_SCANCODE_DOWN |
                    SDL_SCANCODE_KP_2 => {
                        // move down
                        state.view_move_by_vector(0., -KEYBOARD_PAN_DELTA);
                    }
                    SDL_SCANCODE_UP |
                    SDL_SCANCODE_KP_8 => {
                        // move up
                        state.view_move_by_vector(0., KEYBOARD_PAN_DELTA);
                    }
                    _ => {}
                }
            }
            SDL_EVENT_QUIT => {
                exit(0);
            }
            _ => {}
        }
    }
}
