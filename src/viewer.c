/* 
 * Lightning Image Viewer
 * Copyright (c) 2021 Eugene Shatsky
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
#include <SDL3/SDL.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3_image/SDL_image.h>

#ifdef WITH_LIBEXIF

#include <libexif/exif-data.h>

#endif
#ifdef WITH_LIBHEIF

#include <libheif/heif.h>

#endif

#define APP_NAME "Lightning Image Viewer"
#define WIN_TITLE_TAIL " - " APP_NAME

// amount of pixels to pan when pressing an arrow key
// leaflet.js keyboardPanDelta default value is 80 https://leafletjs.com/reference.html
#define KEYBOARD_PAN_DELTA 40

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

struct State {
    // cur_x, _y: coords of current point (under cursor)
    // pre_mv: at start of move (drag) action
    char* file_load_path;
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
} state;

// set default state values and get ready for loading image and calling view_* functions
void init_state() {
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

#ifdef WITH_LIBHEIF

// SDL_image does not support HEIC, so if its IMG_LoadTexture() fails, we also try this
SDL_Texture* load_image_texture_libheif() {
    struct heif_context* ctx = heif_context_alloc();
    if (ctx == NULL) {
        SDL_Log("heif_context_alloc failed");
        return NULL;
    }

    struct heif_error err;
    err = heif_context_read_from_file(ctx, state.file_load_path, NULL);
    if (err.code != heif_error_Ok) {
        //SDL_Log("heif_context_read_from_file failed: %s", err.message);
        heif_context_free(ctx);
        return NULL;
    }

    struct heif_image_handle *handle = NULL;
    err = heif_context_get_primary_image_handle(ctx, &handle);
    if (err.code!=heif_error_Ok || handle==NULL) {
        //SDL_Log("heif_context_get_primary_image_handle failed: %s", err.message);
        if (handle != NULL) {
            heif_image_handle_release(handle);
        }
        heif_context_free(ctx);
        return NULL;
    }

    struct heif_image *img = NULL;
    err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, NULL);
    heif_image_handle_release(handle);
    if (err.code!=heif_error_Ok || img==NULL) {
        //SDL_Log("heif_decode_image failed: %s", err.message);
        if (img != NULL) {
            heif_image_release(img);
        }
        heif_context_free(ctx);
        return NULL;
    }

    int width = heif_image_get_width(img, heif_channel_interleaved);
    int height = heif_image_get_height(img, heif_channel_interleaved);
    int stride;
    const uint8_t *pixels = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
    if (pixels == NULL) {
        //SDL_Log("heif_image_get_plane_readonly failed");
        heif_image_release(img);
        heif_context_free(ctx);
        return NULL;
    }

    SDL_Surface *surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ABGR8888);
    if (surface == NULL) {
        SDL_Log("SDL_CreateSurface failed: %s", SDL_GetError());
        heif_image_release(img);
        heif_context_free(ctx);
        return NULL;
    }

    for (int y=0; y<height; y++) {
        memcpy(surface->pixels+y*surface->pitch, pixels+y*stride, width*4);
    }
    heif_image_release(img);
    heif_context_free(ctx);

    SDL_Texture *texture = SDL_CreateTextureFromSurface(state.renderer, surface);
    if (texture == NULL) {
        SDL_Log("SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
    }

    SDL_DestroySurface(surface);

    return texture;
}

#endif

// set initial orientation, called upon loading image
// SDL3_image doesn't use JPEG EXIF orientation metadata, nor does it provide access to it
// TODO turn this into metadata loader capable of displaying thumbnail ahead of full image loading
void set_init_orient() {
    state.view_init_rotate_angle_q = 0;
    state.view_init_mirror = false;

#ifdef WITH_LIBEXIF

    // TODO avoid re-reading file, or at least doing this for formats for which it is not relevant
    // TODO libexif seems to be able to load only from JPEG (of relevant formats), see ExifLoaderDataFormat. Is  it relevant for any other relevant format? TIFF has support for embedding EXIF metadata defined in EXIF spec itself, PNG and most "post-JPEG" lossy formats have it in their specs, but browsers and viewers support for using it for orientation for non-JPEG formats seems very inconsistent, also most "post-JPEG" lossy formats have their own header fields for orientation, and "external" deps of SDL3_image might handle it internally
    // note: there's huge mess about meaning of words JPEG (which can mean JPEG compression method itself or be synonym for JFIF and EXIF file format), JFIF (original file format for storing JPEG compressed image) and EXIF (metadata format or another file format for storing JPEG, which is described in EXIF spec in such strange way that it's unclear if it's separate file format or incorrect description of embedding EXIF metadata in JFIF)
    //ExifData *exif_data = exif_data_new_from_data(data, data_len);
    ExifData *exif_data = exif_data_new_from_file(state.file_load_path); // free(exif_data): after exit_orientation is checked
    if (exif_data != NULL)
    {
        ExifEntry *exif_entry = exif_data_get_entry(exif_data, EXIF_TAG_ORIENTATION); // free(exif_entry): never, this does not allocate, returns ptr to offset in exif_data
        if (exif_entry != NULL) {
            ExifByteOrder exif_byte_order = exif_data_get_byte_order(exif_data);
            int exif_orientation = exif_get_short(exif_entry->data, exif_byte_order);
            switch(exif_orientation) {
                // TODO expression? Declare array and select from it by index?
                //case 1:
                //    state.view_init_rotate_angle_q = 0;
                //    state.view_init_mirror = false;
                //    break;
                case 2:
                    //state.view_init_rotate_angle_q = 0;
                    state.view_init_mirror = true;
                    break;
                case 3:
                    state.view_init_rotate_angle_q = 2;
                    //state.view_init_mirror = false;
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
                    //state.view_init_mirror = false;
                    break;
                case 7:
                    state.view_init_rotate_angle_q = 3;
                    state.view_init_mirror = true;
                    break;
                case 8:
                    state.view_init_rotate_angle_q = 3;
                    //state.view_init_mirror = false;
                    break;
            }
        }
        exif_data_free(exif_data);
    }

#endif

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
    }
    // TODO we absolutely need pixel perfect rendering at 1:1 scale, and we absolutely need interpolation at scales <1:1
    // default is SDL_SCALEMODE_LINEAR but it breaks pixel perfect at 1:1
    // for now just set SDL_SCALEMODE_NEAREST for scale >=1:1
    // not in set_zoom_level() because it's not called when toggling fullscreen
    if (!SDL_SetTextureScaleMode(state.image_texture, view_rect.w < state.img_w ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST)) {
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
    // SDL has SDL_Surface which is pixmap in process mem used for software manipulation and rendering and SDL_Texture which describes entity owned by graphics hardware driver used for hardware accelerated rendering
    // IMG_LoadTexture() shortcut fuction internally decodes image into SDL_Surface and calls SDL_CreateTextureFromSurface(); we can use it as long as we don't need to manipulate intermediate SDL_Surface
    if (state.image_texture != NULL) {
        // assuming it can only be non-NULL after successfull SDL_CreateTexture*() call
        SDL_DestroyTexture(state.image_texture);
    }
    state.image_texture = IMG_LoadTexture(state.renderer, state.file_load_path);

#ifdef WITH_LIBHEIF

    if (state.image_texture == NULL) {
        // do not pollute logs; currently load_image() can be called unsuccessfully for many irrelevant files when handling prev/next
        //SDL_Log("IMG_LoadTexture failed");
        state.image_texture = load_image_texture_libheif();
    }

#endif

    if (state.image_texture == NULL) {
        state.file_load_success = false;
        return;
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
    set_init_orient();
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
    return dir_entry2_stat_buf.st_mtime - dir_entry1_stat_buf.st_mtime;
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
                view_reset();
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
        view_reset();
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
    // event loop
    SDL_Event event;
    // TODO consider moving all state to state obj
    char lmousebtn_pressed = false;
    char should_exit_on_lmousebtn_release = false;
    while(SDL_WaitEvent(&event)) {
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
    SDL_Log("SDL_WaitEvent failed: %s", SDL_GetError());
    return 0;
}
