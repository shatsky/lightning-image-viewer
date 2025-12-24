/* 
 * Lightning Image Viewer
 * Copyright (c) 2021-2025 Eugene Shatsky
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h> // chdir
#include <limits.h> // INT_MAX
#include <SDL3/SDL.h>
#include <SDL3/SDL_mutex.h>

#include "image_rs_ffi.h"

#define APP_NAME "Lightning Image Viewer"
#define WIN_TITLE_TAIL " - " APP_NAME

// amount of pixels to pan when pressing an arrow key
// leaflet.js keyboardPanDelta default value is 80 https://leafletjs.com/reference.html
#define KEYBOARD_PAN_DELTA 40

// build time config
// shadow
// usually defined through exp and offset_{x, y}
// expand: 6px; offset_x: 0px; offset_y: 1px
// top, left: expand-offset_{y, x}
// right, bottom: expand+offset_{x, y}
#ifndef FRAME_WIDTH_TOP
    #define FRAME_WIDTH_TOP 5
#endif
#ifndef FRAME_WIDTH_RIGHT
    #define FRAME_WIDTH_RIGHT 6
#endif
#ifndef FRAME_WIDTH_BOTTOM
    #define FRAME_WIDTH_BOTTOM 7
#endif
#ifndef FRAME_WIDTH_LEFT
    #define FRAME_WIDTH_LEFT 6
#endif
#ifndef FRAME_COLOR
    #define FRAME_COLOR 0x00, 0x00, 0x00, 38
#endif
// non fullscreen
#ifndef IMAGE_BACKGROUND_COLOR
    #define IMAGE_BACKGROUND_COLOR 0xff, 0xff, 0xff, 0xff
#endif
#ifndef SCALEMODE_LOWER
    #define SCALEMODE_LOWER SDL_SCALEMODE_LINEAR
#endif
#ifndef SCALEMODE_EQUAL
    #define SCALEMODE_EQUAL SDL_SCALEMODE_NEAREST
#endif
#ifndef SCALEMODE_GREATER
    #define SCALEMODE_GREATER SDL_SCALEMODE_LINEAR
#endif


#ifndef _WIN32
    #define PATH_SEP '/'
#else
    #define PATH_SEP '\\'
#endif

// design
// - state obj holds most of global state incl. view_rect
// - view_* functions update view via render_window()
// - event loop in main()

// TODO don't drag or zoom if cursor out of view_rect?
// TODO scale/position limits/constraints?
// TODO ensure that at zoom_level = 0 scale is precisely 1 and rendering is pixel perfect
// TODO ensure that during scroll zoom same image pixel remains under cursor
// TODO create window after image is loaded? (but window has to exist when image texture is created?)
// TODO display GUI error msgs?
// TODO ensure SDL functions error handling
// TODO ensure SDL heap-allocated resources freeing (maybe not for process-lifelong, but image reloading can make some of them limited-liftime)
// TODO is it nessessary to call SDL_DestroyWindow() or smth else before exit()? See also SDL_Quit(), atexit()
// TODO add macro to "call function, check return val, log err and exit in case of failure" for bool SDL functions which return false in case of failure and set err

// TODO image-rs and SDL types
struct Frame {
    SDL_Texture* texture;
    uint32_t width;
    uint32_t height;
    uint32_t x_offset;
    uint32_t y_offset;
    Uint64 delay;
};

struct State {
    // cur_x, _y: coords of current point (under cursor)
    // pre_mv: at start of move (drag) action
    char* file_load_path;
    bool file_load_initial;
    bool file_load_success;
    SDL_Semaphore* file_dialog_semaphore;
    SDL_Window* window;
    int win_w;
    int win_h;
    float win_cur_x;
    float win_cur_y;
    float win_pre_mv_cur_x;
    float win_pre_mv_cur_y;
    char win_fullscreen;
    SDL_Renderer* renderer;
    // SDL has:
    // - SDL_Surface which is pixmap in process mem (in RAM) used for software manipulation and rendering
    // - SDL_Texture which references "texture" entity owned by graphics hardware driver (usually in VRAM) used for hardware accelerated rendering
    // image file is first decoded to SDL_Surface, which is then loaded to SDL_Texture via SDL_CreateTextureFromSurface()
    SDL_Texture* image_texture;
    int img_w;
    int img_h;
    float img_cur_x;
    float img_cur_y;
    // image presentation area size and position (coords of top left corner relative to window)
    SDL_FRect view_rect;
    int view_zoom_level; // 0 is for 1:1
    float view_zoom_scale;
    float view_rect_pre_mv_x;
    float view_rect_pre_mv_y;
    // init: initial after loading image/resetting view
    // rotate_angle_q: 1/4-turns
    int view_init_rotate_angle_q;
    bool view_init_mirror;
    int view_rotate_angle_q;
    bool view_mirror;
    struct dirent** filelist;
    int filelist_len;
    int filelist_load_i; // index of filelist item name of which is currently pointed by state.file_load_path
    struct Frame* anim_frames;
    size_t anim_frames_count;
    size_t anim_cur;
    Uint64 anim_next_frame_time;
    bool anim_paused;
    Uint64 anim_paused_time;
} state;

// set default state values and get ready for loading image and calling view_* functions
void init_state() {
    state.file_load_initial = true;
    // TODO current display in multi monitor setup?
    // TODO does it make sense that SDL requires window size with SDL_WINDOW_MAXIMIZED?
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        exit(1);
    }
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if (!display) {
        SDL_Log("SDL_GetPrimaryDisplay failed: %s", SDL_GetError());
        exit(1);
    }
    // TODO use SDL_GetDisplayBounds() and SDL_GetDisplayUsableBounds()?
    // on Plasma Wayland with Wayland backend SDL_GetDisplayUsableBounds() reports same size as display mode, with X11/XWayland backend it reports incorrect values which seem to be correct size (display mode size minus taskbar) divided by wrong scaling factor (1.2 when I have it set to 1.5, also with X11/XWayland backend app is not scaled by default, so applying scaling factor here makes no sense)
    const SDL_DisplayMode* display_mode = SDL_GetDesktopDisplayMode(display); // free(display_mode): never, this doesn't allocate mem, returns pointer to global
    if (display_mode == NULL) {
        SDL_Log("SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
        exit(1);
    }
    // TODO is window created on primary display?
    // SDL2 SDL_CreateWindow() had x, y position parameters seemingly assuming global desktop coordinate system so that x, y from SDL_GetDisplayBounds() could be used to request compositor to place window at the top left corner of the given display (if implemented in protocol and SDL backend); SDL3 doesn't have them anymore
    if (!SDL_CreateWindowAndRenderer(APP_NAME, display_mode->w, display_mode->h, SDL_WINDOW_BORDERLESS|SDL_WINDOW_MAXIMIZED|SDL_WINDOW_TRANSPARENT, &state.window, &state.renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        exit(1);
    }
    if (!SDL_GetWindowSize(state.window, &state.win_w, &state.win_h)) {
        SDL_Log("SDL_GetWindowSize failed: %s", SDL_GetError());
        exit(1);
    }
    state.win_fullscreen = false;
    state.image_texture = NULL;
    state.filelist = NULL;
    state.anim_frames = NULL;
}

// not to confuse SDL filelist provided by SDL here with state.filelist
static void SDLCALL file_dialog_callback(void* userdata, const char * const *filelist, int filter) {
    if (filelist == NULL) {
        SDL_Log("SDL_ShowOpenFileDialog failed: %s", SDL_GetError());
        exit(1);
    } else {
        for (int i=0; filelist[i]!=NULL; i++) {
            if (state.filelist == NULL) {
                if (state.file_load_path != NULL) {
                    free(state.file_load_path);
                }
            } else {
                while (state.filelist_len--) {
                    free(state.filelist[state.filelist_len]);
                }
                free(state.filelist);
                state.filelist = NULL;
            }
            // SDL filelist is automatically freed upon callback return
            state.file_load_path = strdup(filelist[i]); // free(state.file_load_path): when filelist is filled or new file is opened
            if (state.file_load_path == NULL) {
                SDL_Log("strdup failed");
                exit(1);
            }
            SDL_SignalSemaphore(state.file_dialog_semaphore);
            return;
        }
        SDL_Log("SDL_ShowOpenFileDialog returned empty filelist");
        exit(1);
    }
}

// fake
// image-rs image::Frame has dimensions and offsets, suggesting it can be subregion of full image area, but it seems that all 3 decoders currently implementing AnimationDecoder always return pre composed frames with full dimensions and zero offsets
// API-safe impl would need to clear image_texture subregion and blit into it
void compose_anim_frame() {
    state.image_texture = state.anim_frames[state.anim_cur].texture;
}

// set initial orientation, called upon loading image
void set_init_orient(uint8_t exif_orientation) {
    switch(exif_orientation) {
        // TODO expression? Declare array and select from it by index?
        //case 1:
        //    state.view_init_rotate_angle_q = 0;
        //    state.view_init_mirror = false;
        //    break;
        case 2:
            state.view_init_rotate_angle_q = 0;
            state.view_init_mirror = true;
            break;
        case 3:
            state.view_init_rotate_angle_q = 2;
            state.view_init_mirror = false;
            break;
        case 4:
            state.view_init_rotate_angle_q = 2;
            state.view_init_mirror = true;
            break;
        case 5:
            state.view_init_rotate_angle_q = 1;
            state.view_init_mirror = true;
            break;
        case 6:
            state.view_init_rotate_angle_q = 1;
            state.view_init_mirror = false;
            break;
        case 7:
            state.view_init_rotate_angle_q = 3;
            state.view_init_mirror = true;
            break;
        case 8:
            state.view_init_rotate_angle_q = 3;
            state.view_init_mirror = false;
            break;
        default:
            state.view_init_rotate_angle_q = 0;
            state.view_init_mirror = false;
    }
}

// non-redrawing, only update scale and view_rect size
void set_zoom_level(int view_zoom_level) {
    state.view_zoom_level = view_zoom_level;
    // scale = sqrt(2)^zoom_level = 2^(0.5*zoom_level)
    state.view_zoom_scale = pow(2, 0.5 * view_zoom_level);
    // TODO tried to get pixel perfect rendering at integer scales with SDL_SCALEMODE_LINEAR, does not help
    //if (view_zoom_level%2 == 0) {
    //    state.view_zoom_scale = 1 << (view_zoom_level / 2);
    //}
    state.view_rect.w = state.img_w * state.view_zoom_scale;
    state.view_rect.h = state.img_h * state.view_zoom_scale;
}

// redraw window contents with current state
void render_window() {
    if (!SDL_RenderClear(state.renderer)) {
        SDL_Log("SDL_RenderClear failed: %s", SDL_GetError());
        exit(1);
    }
    // for non-fullscreen simply render with state values, but for fullscreen set view_rect values to fit to screen; using temporary local view_rect because we want state.view_rect values preserved for subsequent switch to non-fullscreen, and, with only transformations available in fullscreen being mirror and rotate, setting fullscreen view_rect values doesn't depend on previous fullscreen view_rect values
    SDL_FRect view_rect;
    if (state.win_fullscreen) {
        // SDL_RenderTextureRotated() draws as if view_rect itself is rotated around its center; in non-fullscreen this is what we want, but in fullscreen we want it to fit to screen, so we have to set such view_rect values that it fits to screen when rotated; using temporary local conditionally-swapped win_w and win_h for setting view_rect.w and view_rect.h
        int win_w = state.view_rotate_angle_q%2 ? state.win_h : state.win_w;
        int win_h = state.view_rotate_angle_q%2 ? state.win_w : state.win_h;
        // start with assumption that image is more stretched vertically than window, view_rect.h = win_h
        view_rect.w = state.img_w * win_h / state.img_h;
        if (view_rect.w > win_w) {
            view_rect.w = win_w;
            view_rect.h = state.img_h * win_w / state.img_w;
        } else {
            view_rect.h = win_h;
        }
        // centered
        view_rect.x = (state.win_w - view_rect.w) / 2;
        view_rect.y = (state.win_h - view_rect.h) / 2;
    } else {
        view_rect = state.view_rect;
        // draw shadow and clear image bg
        SDL_FRect shadow_rect = {
            .x = view_rect.x - FRAME_WIDTH_LEFT,
            .y = view_rect.y - FRAME_WIDTH_TOP,
            .w = view_rect.w + FRAME_WIDTH_LEFT + FRAME_WIDTH_RIGHT,
            .h = view_rect.h + FRAME_WIDTH_TOP + FRAME_WIDTH_BOTTOM
        };
        if (!SDL_SetRenderDrawColor(state.renderer, FRAME_COLOR)) {
            SDL_Log("SDL_SetRenderDrawColor failed: %s", SDL_GetError());
            exit(1);
        }
        if (!SDL_RenderFillRect(state.renderer, &shadow_rect)) {
            SDL_Log("SDL_RenderFillRect failed: %s", SDL_GetError());
            exit(1);
        }
        if (!SDL_SetRenderDrawColor(state.renderer, IMAGE_BACKGROUND_COLOR)) {
            SDL_Log("SDL_SetRenderDrawColor failed: %s", SDL_GetError());
            exit(1);
        }
        if (!SDL_RenderFillRect(state.renderer, &view_rect)) {
            SDL_Log("SDL_RenderFillRect failed: %s", SDL_GetError());
            exit(1);
        }
        if (!SDL_SetRenderDrawColor(state.renderer, 0, 0, 0, SDL_ALPHA_TRANSPARENT)) {
            SDL_Log("SDL_SetRenderDrawColor failed: %s", SDL_GetError());
            exit(1);
        }
    }
    // TODO we absolutely need pixel perfect rendering at 1:1 scale, and we absolutely need interpolation at scales <1:1
    // default is SDL_SCALEMODE_LINEAR but it breaks pixel perfect at 1:1
    // not in set_zoom_level() because it's not called when toggling fullscreen
    if (!SDL_SetTextureScaleMode(state.image_texture, view_rect.w<state.img_w ? SCALEMODE_LOWER : view_rect.w==state.img_w ? SCALEMODE_EQUAL : SCALEMODE_GREATER)) {
        SDL_Log("SDL_SetTextureScaleMode failed: %s", SDL_GetError());
        exit(1);
    }
    // copy image to presentation area in renderer backbuffer
    // TODO ensure that clipping is done correctly without overhead
    if (!SDL_RenderTextureRotated(state.renderer, state.image_texture, NULL, &view_rect, state.view_rotate_angle_q*90, NULL, state.view_mirror ? (state.view_rotate_angle_q%2 ? SDL_FLIP_VERTICAL : SDL_FLIP_HORIZONTAL) : SDL_FLIP_NONE)) {
        SDL_Log("SDL_RenderTextureRotated failed: %s", SDL_GetError());
        exit(1);
    }
    // copy renderer backbuffer to frontbuffer
    if (!SDL_RenderPresent(state.renderer)) {
        SDL_Log("SDL_RenderPresent failed: %s", SDL_GetError());
        exit(1);
    }
}

// reset view_rect to initial scale and position
void view_reset() {
    state.view_rotate_angle_q = state.view_init_rotate_angle_q;
    state.view_mirror = state.view_init_mirror;
    // set max zoom level at which entire image fits in window
    int win_w = state.view_rotate_angle_q%2 ? state.win_h : state.win_w;
    int win_h = state.view_rotate_angle_q%2 ? state.win_w : state.win_h;
    // zoom_level = 2*log2(scale)
    set_zoom_level(floor(2 * log2((float)win_h / state.img_h)));
    if (state.view_rect.w > win_w) {
        set_zoom_level(floor(2 * log2((float)win_w / state.img_w)));
    }
    // centered
    state.view_rect.x = (state.win_w - state.view_rect.w) / 2;
    state.view_rect.y = (state.win_h - state.view_rect.h) / 2;
    render_window();
}

void load_image() {
    // free previous
    if (state.anim_frames != NULL) {
        while (state.anim_frames_count--) {
            if (state.image_texture == state.anim_frames[state.anim_frames_count].texture) {
                state.image_texture = NULL;
            }
            SDL_DestroyTexture(state.anim_frames[state.anim_frames_count].texture);
        }
        free(state.anim_frames);
        state.anim_frames = NULL;
    }
    if (state.image_texture != NULL) {
        SDL_DestroyTexture(state.image_texture);
        state.image_texture = NULL;
    }

    state.file_load_success = false;

    // only terminate upon failure of functions which shouldn't fail because of invalid image file
    // only print non-terminal errs for initial file; when switching prev/next, loading of many non-image files may be attempted
    // TODO validation of data from image-rs is probably redundant
    void *image_rs_ffi_decoder = image_rs_ffi_get_decoder(state.file_load_path);
    if (image_rs_ffi_decoder == NULL) {
        if (state.file_load_initial) {
            SDL_Log("image_rs_ffi_get_decoder failed");
        }
        return;
    }
    uint8_t exif_orientation = image_rs_ffi_get_orientation_as_exif(image_rs_ffi_decoder);
    void *image_rs_ffi_frames_iter = image_rs_ffi_get_frames_iter(image_rs_ffi_decoder);
    if (image_rs_ffi_frames_iter != NULL) {
        // anim
        image_rs_ffi_decoder = NULL;
        struct image_rs_ffi_Frames image_rs_ffi_frames = image_rs_ffi_get_frames(image_rs_ffi_frames_iter); // image_rs_ffi_free_frames(): after loading frames
        image_rs_ffi_frames_iter = NULL;
        state.anim_frames_count = image_rs_ffi_frames.count;
        if (state.anim_frames_count > 0) {
            state.anim_frames = malloc(sizeof(struct Frame)*state.anim_frames_count); // free(): upon next load_image()
            if (state.anim_frames == NULL) {
                SDL_Log("malloc failed");
                exit(1);
            }
        }
        for(int i=0; i<state.anim_frames_count; i++) {
            state.anim_frames[i].width = image_rs_ffi_frames.ffi_frames_vec_data[i].width;
            state.anim_frames[i].height = image_rs_ffi_frames.ffi_frames_vec_data[i].height;
            state.anim_frames[i].x_offset = image_rs_ffi_frames.ffi_frames_vec_data[i].x_offset;
            state.anim_frames[i].y_offset = image_rs_ffi_frames.ffi_frames_vec_data[i].y_offset;
            // TODO
            // TODO single frame?
            if (image_rs_ffi_frames.ffi_frames_vec_data[i].delay_denom_ms == 0) {
                state.anim_frames_count = i;
                break;
            }
            state.anim_frames[i].delay = (Uint64)image_rs_ffi_frames.ffi_frames_vec_data[i].delay_numer_ms / (Uint64)image_rs_ffi_frames.ffi_frames_vec_data[i].delay_denom_ms;
            // TODO
            if (image_rs_ffi_frames.ffi_frames_vec_data[i].buf == NULL) {
                state.anim_frames_count = i;
                break;
            }
            // TODO
            if (state.anim_frames[i].width>INT_MAX/4 || (state.anim_frames[i].height!=0 && state.anim_frames[i].width*4>INT_MAX/state.anim_frames[i].height)) {
                state.anim_frames_count = i;
                break;
            }
            SDL_Surface* surface = SDL_CreateSurfaceFrom((int)state.anim_frames[i].width, (int)state.anim_frames[i].height, SDL_PIXELFORMAT_ABGR8888, image_rs_ffi_frames.ffi_frames_vec_data[i].buf, (int)state.anim_frames[i].width*4);
            if (surface == NULL) {
                SDL_Log("SDL_CreateSurfaceFrom failed: %s", SDL_GetError());
                exit(1);
            }
            state.anim_frames[i].texture = SDL_CreateTextureFromSurface(state.renderer, surface);
            if (state.anim_frames[i].texture == NULL) {
                SDL_Log("SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
                exit(1);
            }
            SDL_DestroySurface(surface);
        }
        image_rs_ffi_free_frames(image_rs_ffi_frames);
        state.anim_cur = 0;
        if (state.anim_frames_count > 0) {
            compose_anim_frame(); // set state.image_texture
            state.anim_next_frame_time = SDL_GetTicks() + state.anim_frames[0].delay;
        } else {
            if (state.file_load_initial) {
                SDL_Log("image_rs_ffi_get_frames returned 0 valid frames");
            }
            return;
        }
        state.anim_paused = false;
    } else {
        // still
        struct image_rs_ffi_RgbaImage image_rs_ffi_rgba_image = image_rs_ffi_get_rgba_image(image_rs_ffi_decoder); // image_rs_ffi_free_rgba_image(): after loading texture or before any return earlier
        image_rs_ffi_decoder = NULL;
        if (image_rs_ffi_rgba_image.buf == NULL) {
            if (state.file_load_initial) {
                SDL_Log("image_rs_ffi_get_rgba_image failed");
            }
            image_rs_ffi_free_rgba_image(image_rs_ffi_rgba_image);
            return;
        }
        // TODO
        if (image_rs_ffi_rgba_image.width>INT_MAX/4 || (image_rs_ffi_rgba_image.height!=0 && image_rs_ffi_rgba_image.width*4>INT_MAX/image_rs_ffi_rgba_image.height)) {
            image_rs_ffi_free_rgba_image(image_rs_ffi_rgba_image);
            return;
        }
        SDL_Surface* surface = SDL_CreateSurfaceFrom((int)image_rs_ffi_rgba_image.width, (int)image_rs_ffi_rgba_image.height, SDL_PIXELFORMAT_ABGR8888, image_rs_ffi_rgba_image.buf, (int)image_rs_ffi_rgba_image.width*4);
        if (surface == NULL) {
            SDL_Log("SDL_CreateSurfaceFrom failed: %s", SDL_GetError());
            exit(1);
        }
        state.image_texture = SDL_CreateTextureFromSurface(state.renderer, surface);
        if (state.image_texture == NULL) {
            SDL_Log("SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
            exit(1);
        }
        SDL_DestroySurface(surface);
        image_rs_ffi_free_rgba_image(image_rs_ffi_rgba_image);
    }

    state.file_load_success = true;
    state.img_w = state.image_texture->w;
    state.img_h = state.image_texture->h;
    // update window title
    // TODO chdir early and always have bare filename in state.file_load_path?
    // TODO on Plasma Wayland window title is split by dash separator and 1st part is displayed in taskbar as filename; appname displayed in taskbar is taken from elsewhere, if app is launched via .desktop file it's Name, if app binary is launched directly it is Name from .desktop file with same binary or icon filename, if no such .desktop file found it is binary filename, if path to binary starts with '.' appname is not displayed at all
    // TODO with X11/XWayland backend if em dash is used as separator (stored as its UTF8 repr in source) title is not updated at all, XWayland bug?
    char* filename = strrchr(state.file_load_path, PATH_SEP);
    if (filename == NULL) {
        filename = state.file_load_path;
    } else {
        filename++;
    }
    char* win_title = malloc(strlen(filename)+strlen(WIN_TITLE_TAIL)+1); // free(win_title): after window title is updated
    if (win_title == NULL) {
        SDL_Log("malloc failed");
        exit(1);
    }
    strcpy(win_title, filename);
    strcat(win_title, WIN_TITLE_TAIL);
    if (!SDL_SetWindowTitle(state.window, win_title)) {
        SDL_Log("SDL_SetWindowTitle failed: %s", SDL_GetError());
        exit(1);
    }
    free(win_title);
    set_init_orient(exif_orientation);
    view_reset();
}

// non-redrawing, non-view_rect-changing, only set window state and bg color for subsequent render_window() call
void set_win_fullscreen(bool win_fullscreen) {
    state.win_fullscreen = win_fullscreen;
    // TODO clear window?
    if (!SDL_SetWindowFullscreen(state.window, win_fullscreen)) {
        SDL_Log("SDL_SetWindowFullscreen failed: %s", SDL_GetError());
        exit(1);
    }
    if (!SDL_SyncWindow(state.window)) {
        SDL_Log("SDL_SyncWindow timed out");
    }
    // assuming that window size can change because of shell UI
    if (!SDL_GetWindowSize(state.window, &state.win_w, &state.win_h)) {
        SDL_Log("SDL_GetWindowSize failed: %s", SDL_GetError());
        exit(1);
    }
    if (!SDL_SetRenderDrawColor(state.renderer, 0, 0, 0, win_fullscreen ? SDL_ALPHA_OPAQUE : SDL_ALPHA_TRANSPARENT)) {
        SDL_Log("SDL_SetRenderDrawColor failed: %s", SDL_GetError());
        exit(1);
    }
}

// save coords at start of move (drag) action which are used to update view_rect pos upon mouse motion; also has to be called if view_rect is changed by other action (zoom) during move
// before calling this, update win_cur_x, _y
void save_pre_mv_coords() {
    state.view_rect_pre_mv_x = state.view_rect.x;
    state.view_rect_pre_mv_y = state.view_rect.y;
    state.win_pre_mv_cur_x = state.win_cur_x;
    state.win_pre_mv_cur_y = state.win_cur_y;
}

// zoom with preserving image point currently under cursor
// used as mouse zoom action
void view_zoom_to_level_at_cursor(int view_zoom_level) {
    if (state.win_fullscreen) {
        set_win_fullscreen(false);
    }
    SDL_GetMouseState(&state.win_cur_x, &state.win_cur_y);
    state.img_cur_x = (state.win_cur_x - state.view_rect.x) / state.view_zoom_scale;
    state.img_cur_y = (state.win_cur_y - state.view_rect.y) / state.view_zoom_scale;
    set_zoom_level(view_zoom_level);
    state.view_rect.x = state.win_cur_x - state.img_cur_x * state.view_zoom_scale;
    state.view_rect.y = state.win_cur_y - state.img_cur_y * state.view_zoom_scale;
    // TODO does cursor still lose current image point when zooming while dragging?
    save_pre_mv_coords();
    render_window();
}

// zoom with preserving image point currently in the center of window
// used as keyboard zoom action
void view_zoom_to_level_at_center(int view_zoom_level) {
    if (state.win_fullscreen) {
        set_win_fullscreen(false);
    }
    float img_center_x = (state.win_w / 2 - state.view_rect.x) / state.view_zoom_scale;
    float img_center_y = (state.win_h / 2 - state.view_rect.y) / state.view_zoom_scale;
    set_zoom_level(view_zoom_level);
    state.view_rect.x = state.win_w / 2 - img_center_x * state.view_zoom_scale;
    state.view_rect.y = state.win_h / 2 - img_center_y * state.view_zoom_scale;
    save_pre_mv_coords();
    render_window();
}

// move view_rect from pre_mv pos by vector of cursor movement from pre_mv to current coords
// used as mouse move action
void view_move_from_pre_mv_by_cursor_mv() {
    // ignore new motion events until current one is processed (to prevent accumulation of events in queue and image movement lag behind cursor which can happen if app has to redraw for each motion event)
    SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, false);
    if (state.win_fullscreen) {
        set_win_fullscreen(false);
    }
    state.view_rect.x = state.view_rect_pre_mv_x + (state.win_cur_x - state.win_pre_mv_cur_x);
    state.view_rect.y = state.view_rect_pre_mv_y + (state.win_cur_y - state.win_pre_mv_cur_y);
    render_window();
    SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, true);
}

// move view_rect by vector
// used as keyboard move action
void view_move_by_vector(float x, float y) {
    if (state.win_fullscreen) {
        set_win_fullscreen(false);
    }
    state.view_rect.x += x;
    state.view_rect.y += y;
    render_window();
}

// helper functions for scandir() which is called in fill_filelist() to get list of image files sorted by mtime
// TODO get rid of duplicate stat() calls?

int scandir_filter_image_files(const struct dirent* dir_entry) {
    // TODO filter images by filename ext?
    struct stat dir_entry_stat_buf;
    if (stat(dir_entry->d_name, &dir_entry_stat_buf) == -1) {
        // can fail for broken links?
        SDL_Log("stat failed: %s", strerror(errno));
        return 0;
    }
    return (dir_entry_stat_buf.st_mode & S_IFMT) == S_IFREG;
}

int scandir_compar_mtime(const struct dirent** dir_entry1, const struct dirent** dir_entry2) {
    // TODO make scandir fail instead of terminating program?
    struct stat dir_entry1_stat_buf, dir_entry2_stat_buf;
    if (stat((*dir_entry1)->d_name, &dir_entry1_stat_buf) == -1 ||
        stat((*dir_entry2)->d_name, &dir_entry2_stat_buf) == -1
    ) {
        // shouldn't fail because only called for entries for which has been already successfully called in scandir_filter_image_files()
        SDL_Log("stat failed: %s", strerror(errno));
        exit(1);
    }

// TODO subsecond precision seems not available on Windows from mingw stat, need to use GetFileInformationByHandleEx info.LastWriteTime.QuadPart
#ifndef _WIN32

    if (dir_entry1_stat_buf.st_mtim.tv_sec != dir_entry2_stat_buf.st_mtim.tv_sec)
        return (dir_entry1_stat_buf.st_mtim.tv_sec < dir_entry2_stat_buf.st_mtim.tv_sec) ? 1 : -1;
    if (dir_entry1_stat_buf.st_mtim.tv_nsec != dir_entry2_stat_buf.st_mtim.tv_nsec)
        return (dir_entry1_stat_buf.st_mtim.tv_nsec < dir_entry2_stat_buf.st_mtim.tv_nsec) ? 1 : -1;

#else

    if (dir_entry1_stat_buf.st_mtime != dir_entry2_stat_buf.st_mtime)
        return (dir_entry1_stat_buf.st_mtime < dir_entry2_stat_buf.st_mtime) ? 1 : -1;

#endif

    return strcmp((*dir_entry1)->d_name, (*dir_entry2)->d_name);
}

#ifdef _WIN32

// mingw does not provide scandir(); it belongs to "C POSIX library" (superset of "C standard library") and is not available in Microsoft C library used by mingw nor in POSIX subset provided by mingw
// struct dirent ***namelist is dirent
// struct dirent* **namelist is ptr to dirent; it's ptr to single dirent, mem is allocated individually for each struct dirent* ptr, (**namelist)+1 is not addr of next dirent, *(**(namelist)+1) is not next dirent
// struct dirent** *namelist is ptr to (1st item of) arr of ptrs to dirents; (*namelist)+1 is addr of next ptr to dirent, *((*namelist)+1) or (*namelist)[1] is addr of next dirent, *(*((*namelist)+1)) or *((*namelist)[1]) is next dirent
// struct dirent*** namelist is ptr to ptr to 1st item of arr of ptrs to dirents
// last level of indirection is needed for callee to be able to pass addr of its struct dirent** ptr as arg and get result written to it
// middle level "arr of ptrs to dirents" is needed instead of just "arr of dirents" because dirent is tricky
// it's a struct, but its last member d_name, which is defined as regular fixed size char arr with some impl-specific size, is allowed to "overflow", "holding" \0-terminated str which is longer than size of d_name arr in struct dirent; of course in such case allocated mem pointed by struct dirent* ptr is larger than size of struct dirent, enough to accomodate it with this "d_name overflow"; this makes "arr of dirents" impossible
int scandir(const char* dir, struct dirent*** namelist,
            int (* sel)(const struct dirent*),
            int (* compar)(const struct dirent**, const struct dirent**)) {
    DIR* dir_stream;
    struct dirent* dir_entry;
    if ((dir_stream = opendir(dir)) == NULL) { // free closedir(dir_stream): before returns
        return -1;
    }
    int namelist_capacity = 0;
    int namelist_len = 0;
    struct dirent** namelist_realloc;
    *namelist = NULL;
    while ((dir_entry = readdir(dir_stream)) != NULL) { // free(dir_entry): via closedir(dir_stream)
        if (!sel(dir_entry)) {
            continue;
        }
        // if namelist is full, double its capacity
        if (namelist_len == namelist_capacity) {
            namelist_capacity = namelist_capacity ? namelist_capacity*2 : 1;
            if ((namelist_realloc = realloc(*namelist, namelist_capacity*sizeof(struct dirent*))) == NULL) { // free(namelist_realloc): before err returns via free(*namelist) or by callee via free(state.filelist)
                while (namelist_len--) {
                    free((*namelist)[namelist_len]);
                }
                free(*namelist);
                closedir(dir_stream);
                return -1;
            }
            *namelist = namelist_realloc;
        }
        // portable impl has to check len of "d_name overflow" and allocate mem accordingly
        // TODO guaranteed that dirent has no pointers to outer things which will be invalidated after subsequent readdir() or closedir()?
        size_t dir_entry_size = strlen(dir_entry->d_name)+1>sizeof(dir_entry->d_name) ? sizeof(struct dirent)-sizeof(dir_entry->d_name)+strlen(dir_entry->d_name)+1 : sizeof(struct dirent);
        if (((*namelist)[namelist_len] = malloc(dir_entry_size)) == NULL) { // free((*namelist)[namelist_len]): before err returns or by callee via free(state.filelist[state.filelist_len])
            while (namelist_len--) {
                free((*namelist)[namelist_len]);
            }
            free(*namelist);
            closedir(dir_stream);
            return -1;
        }
        memcpy((*namelist)[namelist_len], dir_entry, dir_entry_size);
        namelist_len++;
    }
    closedir(dir_stream);
    qsort(*namelist, namelist_len, sizeof(struct dirent*), (int (*)(const void*, const void*))compar);
    return namelist_len;
}

#endif

// fills filelist with filepaths of image files in parent dir of file state.file_load_path, enabling prev/next switch logic
// before calling this, state.filelist must be NULL and state.file_load_path must point to allocated mem which can be freed via it
// in case of success, mem pointed by state.file_load_path is freed and it's overwritten with address of same file path within state.filelist
// in case of failure, state.file_load_path is preserved and state.filelist remains NULL
// TODO what if state.file_load_path or filename is empty? For now it shouldn't be problem because program will be terminated earlier
void fill_filelist() {
    char* filename = strrchr(state.file_load_path, PATH_SEP); // used a bit hacky
    // TODO also skip if state.file_load_path dir is current workdir
    if (filename != NULL) {
        *filename = '\0'; // undo after chdir
        if (chdir(state.file_load_path) == -1) {
            SDL_Log("chdir failed: %s", strerror(errno));
            *filename = PATH_SEP;
            return;
        }
        *filename = PATH_SEP;
        filename++;
        // strip state.file_load_path to filename to keep it valid after chdir
        // TODO strcpy not safe with possible overlap?
        memmove(state.file_load_path, filename, strlen(filename)+1);
    }
    filename = state.file_load_path;

    state.filelist_len = scandir(".", &state.filelist, scandir_filter_image_files, scandir_compar_mtime); // free(state.filelist): if file not found in directory or when new file is opened
    if (state.filelist_len == -1) {
        SDL_Log("scandir failed: %s", strerror(errno));
    } else {

        // verify that file exists in dir and change state.file_load_path to point to its name in filelist
        state.filelist_load_i = 0;
        for (int i=0; i<state.filelist_len; i++) {
            if (strcmp(filename, state.filelist[i]->d_name)==0) {
                state.filelist_load_i = i;
                free(state.file_load_path);
                state.file_load_path = state.filelist[state.filelist_load_i]->d_name;
                break;
            }
        }
        if (state.filelist_len==0 || state.file_load_path!=state.filelist[state.filelist_load_i]->d_name) {
            SDL_Log("file not found in directory");
            while (state.filelist_len--) {
                free(state.filelist[state.filelist_len]);
            }
            free(state.filelist);
            state.filelist = NULL;
        }
    }
}

void load_next_image(bool reverse) {
    // TODO more generic solution for prevention of events accumulation?
    SDL_SetEventEnabled(SDL_EVENT_KEY_DOWN, false);

    // filelist is not filled until load_next_image() is called 1st time, to display initial image quicker
    if (state.filelist == NULL) {
        fill_filelist();
        if (state.filelist == NULL) {
            SDL_Log("failed to fill filelist");
            load_image();
            if (!state.file_load_success) {
                SDL_Log("load_image failed; failed to reload file");
                exit(1);
            }
            SDL_SetEventEnabled(SDL_EVENT_KEY_DOWN, true);
            return;
        }
    }

    int filelist_load_i_saved = state.filelist_load_i;
    do {
        state.filelist_load_i = (state.filelist_load_i + (reverse ? state.filelist_len - 1 : 1)) % state.filelist_len;
        state.file_load_path = state.filelist[state.filelist_load_i]->d_name;
        load_image();
    } while (!state.file_load_success && state.filelist_load_i!=filelist_load_i_saved);
    if (!state.file_load_success) {
        SDL_Log("load_image failed; wrapped around filelist and failed to load any file");
        exit(1);
    }

    SDL_SetEventEnabled(SDL_EVENT_KEY_DOWN, true);
}

// TODO consider moving to SDL3 callbacks
int main(int argc, char** argv)
{
    if (!SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_SCALE_TO_DISPLAY, "1")) {
        SDL_Log("SDL_SetHint failed: %s", SDL_GetError());
        exit(1);
    }
    init_state();
    if( argc < 2 ) {
        state.file_dialog_semaphore = SDL_CreateSemaphore(0);
        if (!state.file_dialog_semaphore) {
            SDL_Log("SDL_CreateSemaphore failed: %s", SDL_GetError());
            exit(1);
        }
        SDL_ShowOpenFileDialog(&file_dialog_callback, NULL, state.window, NULL, 0, NULL, false);
        //SDL_WaitSemaphore(state.file_dialog_semaphore);
        // on some platforms incl. Linux with xdg-desktop-portal it requires event loop to call the callback
        // SDL_WaitEvent(NULL) doesn't work here, it won't run loop body until there's actual SDL event
        // TODO events while waiting for dialog? App exit?
        while (true) {
            SDL_Delay(10);
            SDL_PumpEvents();
            if (SDL_TryWaitSemaphore(state.file_dialog_semaphore)) {
                break;
            }
        }
    } else {
        state.file_load_path = strdup(argv[1]); // free(state.file_load_path): when filelist is filled or new file is opened
        if (state.file_load_path == NULL) {
            SDL_Log("strdup failed");
            exit(1);
        }
    }
    load_image();
    if (!state.file_load_success) {
        SDL_Log("load_image failed; failed to load initial file");
        exit(1);
    }
    state.file_load_initial = false;
    // event loop
    SDL_Event event;
    // TODO consider moving all state to state obj
    char lmousebtn_pressed = false;
    char should_exit_on_lmousebtn_release = false;
    Uint32 now;
    while(true) {
        if (state.anim_frames == NULL || state.anim_frames_count<2 || state.anim_paused) {
            if (!SDL_WaitEvent(&event)) {
                SDL_Log("SDL_WaitEvent failed: %s", SDL_GetError());
                break;
            }
        } else {
            now = SDL_GetTicks();
            // if next frame time missed or if event queue empty and next frame time arrives before event while waiting for event
            if (state.anim_next_frame_time<now ||
                !SDL_WaitEventTimeout(&event, state.anim_next_frame_time-now)) {
                state.anim_cur = (state.anim_cur + 1) % state.anim_frames_count;
                // display next frame if in time, else skip
                if (state.anim_next_frame_time >= now) {
                    compose_anim_frame();
                    render_window();
                }
                state.anim_next_frame_time += state.anim_frames[state.anim_cur].delay;
                continue;
            }

        }
        switch (event.type) {
            case SDL_EVENT_MOUSE_WHEEL:
                if (event.wheel.y != 0) {
                    view_zoom_to_level_at_cursor(event.wheel.y>0 ? state.view_zoom_level+1 : state.view_zoom_level-1);
                    should_exit_on_lmousebtn_release = false;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (lmousebtn_pressed) {
                    state.win_cur_x = event.motion.x;
                    state.win_cur_y = event.motion.y;
                    view_move_from_pre_mv_by_cursor_mv();
                    should_exit_on_lmousebtn_release = false;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                switch (event.button.button) {
                    case SDL_BUTTON_LEFT:
                        lmousebtn_pressed = true;
                        SDL_GetMouseState(&state.win_cur_x, &state.win_cur_y);
                        save_pre_mv_coords();
                        should_exit_on_lmousebtn_release = true;
                        break;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                switch (event.button.button) {
                    case SDL_BUTTON_LEFT:
                        if (should_exit_on_lmousebtn_release) {
                            exit(0);
                        }
                        lmousebtn_pressed = false;
                        break;
                    case SDL_BUTTON_MIDDLE:
                        set_win_fullscreen(!state.win_fullscreen);
                        render_window();
                        break;
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                switch(event.key.scancode) {
                    case SDL_SCANCODE_F:
                        // toggle fullscreen
                        set_win_fullscreen(!state.win_fullscreen);
                        render_window();
                        break;
                    case SDL_SCANCODE_L:
                        // rotate counter clockwise
                        state.view_rotate_angle_q = (state.view_rotate_angle_q + (state.view_mirror ? 1 : 3)) % 4;
                        render_window();
                        break;
                    case SDL_SCANCODE_M:
                        // mirror horizontally
                        state.view_mirror = !state.view_mirror;
                        render_window();
                        break;
                    case SDL_SCANCODE_Q:
                        // quit
                        exit(0);
                    case SDL_SCANCODE_R:
                        // rotate clockwise
                        state.view_rotate_angle_q = (state.view_rotate_angle_q + (state.view_mirror ? 3 : 1)) % 4;
                        render_window();
                        break;
                    case SDL_SCANCODE_0:
                        // zoom 1:1
                        view_zoom_to_level_at_center(0);
                        break;
                    case SDL_SCANCODE_RETURN:
                        // quit
                        exit(0);
                    case SDL_SCANCODE_ESCAPE:
                        // quit
                        exit(0);
                    case SDL_SCANCODE_SPACE:
                        // toggle animation playback
                        if (!state.anim_paused) {
                            state.anim_paused_time = SDL_GetTicks();
                        } else {
                            state.anim_next_frame_time += SDL_GetTicks() - state.anim_paused_time;
                        }
                        state.anim_paused = !state.anim_paused;
                        break;
                    case SDL_SCANCODE_MINUS:
                        // zoom out
                        view_zoom_to_level_at_center(state.view_zoom_level-1);
                        break;
                    case SDL_SCANCODE_EQUALS:
                        // zoom in
                        view_zoom_to_level_at_center(state.view_zoom_level+1);
                        break;
                    case SDL_SCANCODE_F11:
                        // toggle fullscreen
                        set_win_fullscreen(!state.win_fullscreen);
                        render_window();
                        break;
                    case SDL_SCANCODE_PAGEUP:
                        // prev
                        load_next_image(true);
                        break;
                    case SDL_SCANCODE_PAGEDOWN:
                        // next
                        load_next_image(false);
                        break;
                    case SDL_SCANCODE_RIGHT:
                        // move right
                        view_move_by_vector(-KEYBOARD_PAN_DELTA, 0);
                        break;
                    case SDL_SCANCODE_LEFT:
                        // move left
                        view_move_by_vector(KEYBOARD_PAN_DELTA, 0);
                        break;
                    case SDL_SCANCODE_DOWN:
                        // move down
                        view_move_by_vector(0, -KEYBOARD_PAN_DELTA);
                        break;
                    case SDL_SCANCODE_UP:
                        // move up
                        view_move_by_vector(0, KEYBOARD_PAN_DELTA);
                        break;
                }
                break;
            case SDL_EVENT_QUIT:
                exit(0);
        }
    }
    return 0;
}
