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
};

// set default state values and get ready for loading image and calling view_* functions
void init_state(struct State* state) {
    // TODO current display in multi monitor setup?
    // TODO does it make sense that SDL requires window size with SDL_WINDOW_MAXIMIZED?
    if (!SDL_Init(SDL_INIT_VIDEO)){
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        exit(1);
    }
    state->displays = SDL_GetDisplays(&state->display_count); // free:
    if(!state->display_count) {
        SDL_Log("SDL_GetDisplays failed: %s", SDL_GetError());
        exit(1);
    }
    state->display_mode = SDL_GetDesktopDisplayMode(state->displays[0]); // free:
    if (NULL==state->display_mode) {
        SDL_Log("SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
        exit(1);
    }
    if (!SDL_CreateWindowAndRenderer("Lightning Image Viewer", state->display_mode->w, state->display_mode->h, SDL_WINDOW_BORDERLESS|SDL_WINDOW_MAXIMIZED|SDL_WINDOW_TRANSPARENT, &state->window, &state->renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        exit(1);
    }
    state->win_fullscreen = false;
}

static void SDLCALL file_dialog_callback(void* userdata, const char * const *filelist, int filter) {
    struct State* state = (struct State*)userdata;
    if (NULL == filelist) {
        SDL_Log("SDL_ShowOpenFileDialog failed: %s", SDL_GetError());
        exit(1);
    } else {
        for (int i=0; filelist[i]!=NULL; i++) {
            // TODO free previous?
            state->file_path = SDL_strdup(filelist[i]);
            SDL_SignalSemaphore(state->file_dialog_semaphore);
            return;
        }
        SDL_Log("SDL_ShowOpenFileDialog returned empty filelist");
        exit(1);
    }
}

void load_image(struct State *state) {
    // decode image into surface, get its dimensions and create texture from it
    // SDL surface is in RAM, texture is in VRAM (and uses less CPU for processing)
    // IMG_LoadTexture() creates tmp surface, too
    //state->image_texture = IMG_LoadTexture(state->renderer, state->file_path);
    state->image_surface = IMG_Load(state->file_path);
    if (NULL == state->image_surface) {
        SDL_Log("IMG_Load failed");
        exit(1);
    }
    state->img_w = state->image_surface->w;
    state->img_h = state->image_surface->h;
    // TODO free previous texture?
    state->image_texture = SDL_CreateTextureFromSurface(state->renderer, state->image_surface);
    SDL_DestroySurface(state->image_surface);
    // TODO default is SDL_SCALEMODE_LINEAR but it breaks pixel perfect rendering at zoom level 0 (1:1 scale)
    SDL_SetTextureScaleMode(state->image_texture, SDL_SCALEMODE_NEAREST);
}

// non-redrawing, only update scale and view_rect size
void set_zoom_level(struct State* state, int view_zoom_level) {
    state->view_zoom_level = view_zoom_level;
    // scale = sqrt(2)^zoom_level = 2^(0.5*zoom_level)
    state->view_zoom_scale = pow(2, 0.5 * view_zoom_level);
    // TODO tried to get pixel perfect rendering at integer scales with SDL_SCALEMODE_LINEAR, does not help
    //if (view_zoom_level%2 == 0) {
    //    state->view_zoom_scale = 1 << (view_zoom_level / 2);
    //}
    state->view_rect.w = state->img_w * state->view_zoom_scale;
    state->view_rect.h = state->img_h * state->view_zoom_scale;
}

// redraw window contents with current state
void render_window(struct State *state) {
    SDL_RenderClear(state->renderer);
    // copy image to presentation area in renderer backbuffer
    // TODO ensure that clipping is done correctly without overhead
    SDL_RenderTexture(state->renderer, state->image_texture, NULL, &state->view_rect);
    // copy renderer backbuffer to frontbuffer
    SDL_RenderPresent(state->renderer);
}

// reset view_rect to initial scale and position
void view_reset(struct State* state) {
    // calculate max zoom level to fit whole image
    SDL_GetWindowSize(state->window, &state->win_w, &state->win_h);
    // zoom_level = 2*log2(scale)
    set_zoom_level(state, floor(2 * log2((float)state->win_h / state->img_h)));
    if (state->img_w*state->view_zoom_scale > state->win_w) {
        set_zoom_level(state, floor(2 * log2((float)state->win_w / state->img_w)));
    }
    // centered
    state->view_rect.x = (state->win_w - state->view_rect.w) / 2;
    state->view_rect.y = (state->win_h - state->view_rect.h) / 2;
    render_window(state);
}

// non-redrawing, non-view_rect-changing, only reset window and bg
void set_fullscreen_off(struct State* state) {
    state->win_fullscreen = false;
    // TODO clear window?
    SDL_SetWindowFullscreen(state->window, false);
    SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, SDL_ALPHA_TRANSPARENT);
}

// before calling this, update win_cur_x, _y
void save_pre_mv_coords(struct State *state) {
    state->view_rect_pre_mv_x = state->view_rect.x;
    state->view_rect_pre_mv_y = state->view_rect.y;
    state->win_pre_mv_cur_x = state->win_cur_x;
    state->win_pre_mv_cur_y = state->win_cur_y;
}

// zoom with preserving current image point under cursor
void view_zoom_to_level(struct State* state, int view_zoom_level) {
    if (state->win_fullscreen) {
        set_fullscreen_off(state);
    }
    SDL_GetMouseState(&state->win_cur_x, &state->win_cur_y);
    state->img_cur_x = (state->win_cur_x - state->view_rect.x) / state->view_zoom_scale;
    state->img_cur_y = (state->win_cur_y - state->view_rect.y) / state->view_zoom_scale;
    set_zoom_level(state, view_zoom_level);
    state->view_rect.x = state->win_cur_x - state->img_cur_x * state->view_zoom_scale;
    state->view_rect.y = state->win_cur_y - state->img_cur_y * state->view_zoom_scale;
    // TODO cursor still loses current image point when zooming while dragging
    save_pre_mv_coords(state);
    render_window(state);
}

// move view_rect from pos corresponding to saved pre_mv cursor pos to current pos
void view_move(struct State* state) {
    // ignore new move events until current is processed
    SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, false);
    if (state->win_fullscreen) {
        set_fullscreen_off(state);
    }
    state->view_rect.x = state->view_rect_pre_mv_x + (state->win_cur_x - state->win_pre_mv_cur_x);
    state->view_rect.y = state->view_rect_pre_mv_y + (state->win_cur_y - state->win_pre_mv_cur_y);
    render_window(state);
    SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, true);
}

// TODO ideas for consistent view state on subsequent switch to non-fullscreen:
// a) (current impl) save copy of view_rect before changing and restore it after render_window() call; next time view_rect will be pre-fullscreen
// b) change zoom_scale to match fullscreen view_rect, leave zoom_level unchanged, next time update view as in zoom_to_level(zoom_level) first; view_rect size will be pre-fullscreen, position will change so that image point under cursor will be one that will be in fullscreen view_rect then
// c) save img_cur coords before changing view_rect, next time update view_rect like in zoom_to_level(zoom_level) first, but without updating img_cur coords; view_rect size will be pre-fullscreen, position will change so that image point under cursor will be one that is in pre-fullscreen view_rect (like hidden pre-fullscreen view_rect is dragged while in fullscreen)
void view_set_fullscreen_on(struct State* state) {
    state->win_fullscreen = true;
    SDL_FRect view_rect_saved = state->view_rect;
    // TODO clear window?
    // TODO in Plasma Wayland, shell UI doesn't hide after this, instead hides upon next render_window() call after switching back to non-fullscreen
    SDL_SetWindowFullscreen(state->window, true);
    SDL_GetWindowSize(state->window, &state->win_w, &state->win_h);
    state->view_rect.w = state->win_w;
    state->view_rect.x = 0;
    state->view_rect.h = state->img_h * state->win_w / state->img_w;
    if (state->view_rect.h > state->win_h) {
        state->view_rect.h = state->win_h;
        state->view_rect.y = 0;
        state->view_rect.w = state->img_w * state->win_h / state->img_h;
        state->view_rect.x = (state->win_w - state->view_rect.w) / 2;
        //state->view_zoom_scale = (float)state->win_h / state->img_h;
    }
    else {
        state->view_rect.y = (state->win_h - state->view_rect.h) / 2;
        //state->view_zoom_scale = (float)state->win_w / state->img_w;
    }
    SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    render_window(state);
    state->view_rect = view_rect_saved;
}

void view_set_fullscreen_off(struct State* state) {
    set_fullscreen_off(state);
    render_window(state);
}

void toggle_fullscreen(struct State* state) {
    if (!state->win_fullscreen) {
        view_set_fullscreen_on(state);
    } else {
        view_set_fullscreen_off(state);
    }
}

// TODO consider moving to SDL3 callbacks
int main(int argc, char** argv)
{
    struct State state;
    init_state(&state);
    if( argc < 2 ) {
        state.file_dialog_semaphore = SDL_CreateSemaphore(0);
        if (!state.file_dialog_semaphore) {
            SDL_Log("SDL_CreateSemaphore failed: %s", SDL_GetError());
            exit(1);
        }
        SDL_ShowOpenFileDialog(&file_dialog_callback, (void*)&state, state.window, NULL, 0, NULL, false);
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
    load_image(&state);
    view_reset(&state);
    // event loop
    SDL_Event event;
    // TODO consider moving all state to state obj
    char lmousebtn_pressed = false;
    char should_exit_on_lmousebtn_release;
    while(SDL_WaitEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_MOUSE_WHEEL:
                if (event.wheel.y != 0) {
                    view_zoom_to_level(&state, event.wheel.y>0 ? state.view_zoom_level+1 : state.view_zoom_level-1);
                    should_exit_on_lmousebtn_release = false;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (lmousebtn_pressed) {
                    state.win_cur_x = event.motion.x;
                    state.win_cur_y = event.motion.y;
                    view_move(&state);
                    should_exit_on_lmousebtn_release = false;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                switch (event.button.button) {
                    case SDL_BUTTON_LEFT:
                        lmousebtn_pressed = true;
                        SDL_GetMouseState(&state.win_cur_x, &state.win_cur_y);
                        save_pre_mv_coords(&state);
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
                        toggle_fullscreen(&state);
                        break;
                }
                break;
            case SDL_EVENT_QUIT:
                exit(0);
        }
    }
    return 0;
}
