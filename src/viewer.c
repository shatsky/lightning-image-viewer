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
#include <SDL3/SDL.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3_image/SDL_image.h>

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

struct State {
    // cur x, y: coords of current point (under cursor)
    // pre mv: at start of move (drag) action
    char* file_path;
    SDL_Semaphore* file_dialog_semaphore;
    int display_count;
    SDL_DisplayID* displays;
    const SDL_DisplayMode* display_mode;
    SDL_Window* window;
    int win_w;
    int win_h;
    float win_cur_x;
    float win_cur_y;
    float win_pre_mv_cur_x;
    float win_pre_mv_cur_y;
    char win_fullscreen;
    SDL_Renderer* renderer;
    SDL_Surface* image_surface;
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
} state;

// set default state values and get ready for loading image and calling view_* functions
void init_state() {
    // TODO current display in multi monitor setup?
    // TODO does it make sense that SDL requires window size with SDL_WINDOW_MAXIMIZED?
    if (!SDL_Init(SDL_INIT_VIDEO)){
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        exit(1);
    }
    state.displays = SDL_GetDisplays(&state.display_count); // free:
    if(!state.display_count) {
        SDL_Log("SDL_GetDisplays failed: %s", SDL_GetError());
        exit(1);
    }
    state.display_mode = SDL_GetDesktopDisplayMode(state.displays[0]); // free:
    if (NULL==state.display_mode) {
        SDL_Log("SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
        exit(1);
    }
    if (!SDL_CreateWindowAndRenderer("Lightning Image Viewer", state.display_mode->w, state.display_mode->h, SDL_WINDOW_BORDERLESS|SDL_WINDOW_MAXIMIZED|SDL_WINDOW_TRANSPARENT, &state.window, &state.renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        exit(1);
    }
    SDL_GetWindowSize(state.window, &state.win_w, &state.win_h);
    state.win_fullscreen = false;
}

static void SDLCALL file_dialog_callback(void* userdata, const char * const *filelist, int filter) {
    if (NULL == filelist) {
        SDL_Log("SDL_ShowOpenFileDialog failed: %s", SDL_GetError());
        exit(1);
    } else {
        for (int i=0; filelist[i]!=NULL; i++) {
            // TODO free previous?
            state.file_path = SDL_strdup(filelist[i]);
            SDL_SignalSemaphore(state.file_dialog_semaphore);
            return;
        }
        SDL_Log("SDL_ShowOpenFileDialog returned empty filelist");
        exit(1);
    }
}

void load_image() {
    // decode image into surface, get its dimensions and create texture from it
    // SDL surface is in RAM, texture is in VRAM (and uses less CPU for processing)
    // IMG_LoadTexture() creates tmp surface, too
    //state.image_texture = IMG_LoadTexture(state.renderer, state.file_path);
    state.image_surface = IMG_Load(state.file_path);
    if (NULL == state.image_surface) {
        SDL_Log("IMG_Load failed");
        exit(1);
    }
    state.img_w = state.image_surface->w;
    state.img_h = state.image_surface->h;
    // TODO free previous texture?
    state.image_texture = SDL_CreateTextureFromSurface(state.renderer, state.image_surface);
    SDL_DestroySurface(state.image_surface);
    // TODO default is SDL_SCALEMODE_LINEAR but it breaks pixel perfect rendering at zoom level 0 (1:1 scale)
    SDL_SetTextureScaleMode(state.image_texture, SDL_SCALEMODE_NEAREST);
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
    // copy image to presentation area in renderer backbuffer
    // TODO ensure that clipping is done correctly without overhead
    // for non-fullscreen simply render with state values, but for fullscreen
    if (state.win_fullscreen) {
        // use temporary local view_rect
        SDL_FRect view_rect;
        // assuming that fullscreen window can be bigger because of shell
        // TODO use state.display_mode->w, state.display_mode->h ?
        view_rect.w = state.win_w;
        view_rect.x = 0;
        view_rect.h = state.img_h * state.win_w / state.img_w;
        if (view_rect.h > state.win_h) {
            view_rect.h = state.win_h;
            view_rect.y = 0;
            view_rect.w = state.img_w * state.win_h / state.img_h;
            view_rect.x = (state.win_w - view_rect.w) / 2;
        }
        else {
            view_rect.y = (state.win_h - view_rect.h) / 2;
        }
        SDL_RenderTexture(state.renderer, state.image_texture, NULL, &view_rect);
    } else {
        SDL_RenderTexture(state.renderer, state.image_texture, NULL, &state.view_rect);
    }
    // copy renderer backbuffer to frontbuffer
    SDL_RenderPresent(state.renderer);
}

// reset view_rect to initial scale and position
void view_reset() {
    // calculate max zoom level to fit whole image
    // zoom_level = 2*log2(scale)
    set_zoom_level(floor(2 * log2((float)state.win_h / state.img_h)));
    if (state.img_w*state.view_zoom_scale > state.win_w) {
        set_zoom_level(floor(2 * log2((float)state.win_w / state.img_w)));
    }
    // centered
    state.view_rect.x = (state.win_w - state.view_rect.w) / 2;
    state.view_rect.y = (state.win_h - state.view_rect.h) / 2;
    render_window();
}

// non-redrawing, non-view_rect-changing, only reset window and bg
void set_win_fullscreen(bool win_fullscreen) {
    state.win_fullscreen = win_fullscreen;
    // TODO clear window?
    // TODO in Plasma Wayland, shell UI doesn't hide/show after this, instead upon next render_window() call, but not if immediately
    if (win_fullscreen) {
        SDL_SetWindowFullscreen(state.window, true);
        SDL_GetWindowSize(state.window, &state.win_w, &state.win_h);
        SDL_SetRenderDrawColor(state.renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    } else {
        SDL_SetWindowFullscreen(state.window, false);
        SDL_GetWindowSize(state.window, &state.win_w, &state.win_h);
        SDL_SetRenderDrawColor(state.renderer, 0, 0, 0, SDL_ALPHA_TRANSPARENT);
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

// TODO consider moving to SDL3 callbacks
int main(int argc, char** argv)
{
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
        state.file_path = argv[1];
    }
    load_image();
    view_reset();
    // event loop
    SDL_Event event;
    // TODO consider moving all state to state obj
    char lmousebtn_pressed = false;
    char should_exit_on_lmousebtn_release;
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
                    case SDL_SCANCODE_Q:
                        // quit
                        exit(0);
                    case SDL_SCANCODE_ESCAPE:
                        // quit
                        exit(0);
                }
                break;
            case SDL_EVENT_QUIT:
                exit(0);
        }
    }
    return 0;
}
