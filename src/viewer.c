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

#define APP_NAME "Lightning Image Viewer"
#define WIN_TITLE_TAIL " - " APP_NAME

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
    // cur x, y: coords of current point (under cursor)
    // pre mv: at start of move (drag) action
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
    int view_rotate_angle_q; // 1/4-turns
    bool view_mirror;
    struct dirent** filelist;
    int filelist_len;
    int filelist_load_i; // index of filelist item name of which is currently pointed by state.file_load_path
} state;

// set default state values and get ready for loading image and calling view_* functions
void init_state() {
    // TODO current display in multi monitor setup?
    // TODO does it make sense that SDL requires window size with SDL_WINDOW_MAXIMIZED?
    if (!SDL_Init(SDL_INIT_VIDEO)){
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        exit(1);
    }
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if(!display) {
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
    SDL_RenderClear(state.renderer);
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
    state.view_rotate_angle_q = 0;
    state.view_mirror = false;
    // set max zoom level at which entire image fits in window
    // zoom_level = 2*log2(scale)
    set_zoom_level(floor(2 * log2((float)state.win_h / state.img_h)));
    if (state.view_rect.w > state.win_w) {
        set_zoom_level(floor(2 * log2((float)state.win_w / state.img_w)));
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
        SDL_DestroyTexture(state.image_texture);
    }
    state.image_texture = IMG_LoadTexture(state.renderer, state.file_load_path);
    if (state.image_texture == NULL) {
        // do not pollute logs; currently load_image() can be called unsuccessfully for many irrelevant files when handling prev/next
        //SDL_Log("IMG_LoadTexture failed");
        state.file_load_success = false;
        return;
    }
    state.file_load_success = true;
    state.img_w = state.image_texture->w;
    state.img_h = state.image_texture->h;
    // TODO default is SDL_SCALEMODE_LINEAR but it breaks pixel perfect rendering at zoom level 0 (1:1 scale)
    if (!SDL_SetTextureScaleMode(state.image_texture, SDL_SCALEMODE_NEAREST)) {
        SDL_Log("SDL_SetTextureScaleMode failed: %s", SDL_GetError());
        exit(1);
    }
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
    view_reset();
}

// non-redrawing, non-view_rect-changing, only set window state and bg color for subsequent render_window() call
void set_win_fullscreen(bool win_fullscreen) {
    state.win_fullscreen = win_fullscreen;
    // TODO clear window?
    // TODO on Plasma Wayland, shell UI isn't hidden/shown after this; happens upon render_window() call after another event
    if (!SDL_SetWindowFullscreen(state.window, win_fullscreen)) {
        SDL_Log("SDL_SetWindowFullscreen failed: %s", SDL_GetError());
        exit(1);
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

// before calling this, update win_cur_x, _y
void save_pre_mv_coords() {
    state.view_rect_pre_mv_x = state.view_rect.x;
    state.view_rect_pre_mv_y = state.view_rect.y;
    state.win_pre_mv_cur_x = state.win_cur_x;
    state.win_pre_mv_cur_y = state.win_cur_y;
}

// zoom with preserving current image point under cursor
void view_zoom_to_level(int view_zoom_level) {
    if (state.win_fullscreen) {
        set_win_fullscreen(false);
    }
    SDL_GetMouseState(&state.win_cur_x, &state.win_cur_y);
    state.img_cur_x = (state.win_cur_x - state.view_rect.x) / state.view_zoom_scale;
    state.img_cur_y = (state.win_cur_y - state.view_rect.y) / state.view_zoom_scale;
    set_zoom_level(view_zoom_level);
    state.view_rect.x = state.win_cur_x - state.img_cur_x * state.view_zoom_scale;
    state.view_rect.y = state.win_cur_y - state.img_cur_y * state.view_zoom_scale;
    // TODO cursor still loses current image point when zooming while dragging
    save_pre_mv_coords();
    render_window();
}

// move view_rect from pos corresponding to saved pre_mv cursor pos to current pos
void view_move() {
    // ignore new move events until current is processed
    SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, false);
    if (state.win_fullscreen) {
        set_win_fullscreen(false);
    }
    state.view_rect.x = state.view_rect_pre_mv_x + (state.win_cur_x - state.win_pre_mv_cur_x);
    state.view_rect.y = state.view_rect_pre_mv_y + (state.win_cur_y - state.win_pre_mv_cur_y);
    render_window();
    SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, true);
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
    // filelist is not filled until load_next_image() is called 1st time, to display initial image quicker
    if (state.filelist == NULL) {
        fill_filelist();
        if (state.filelist == NULL) {
            SDL_Log("failed to fill filelist");
            load_image();
            if (!state.file_load_success) {
                SDL_Log("IMG_LoadTexture failed; failed to reload file");
                view_reset();
            }
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
        SDL_Log("IMG_LoadTexture failed; wrapped around filelist and failed to load any file");
        view_reset();
    }
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
        SDL_Log("IMG_LoadTexture failed; failed to load initial file");
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
                    view_zoom_to_level(event.wheel.y>0 ? state.view_zoom_level+1 : state.view_zoom_level-1);
                    should_exit_on_lmousebtn_release = false;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (lmousebtn_pressed) {
                    state.win_cur_x = event.motion.x;
                    state.win_cur_y = event.motion.y;
                    view_move();
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
                    case SDL_SCANCODE_RETURN:
                        // quit
                        exit(0);
                    case SDL_SCANCODE_ESCAPE:
                        // quit
                        exit(0);
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
                }
                break;
            case SDL_EVENT_QUIT:
                exit(0);
        }
    }
    // TODO fails on Wayland upon long keyboard key press without error
    SDL_Log("SDL_WaitEvent failed: %s", SDL_GetError());
    return 0;
}
