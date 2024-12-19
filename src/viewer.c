/* 
 * Lightning Image Viewer
 * Copyright (c) 2015 Eugene Shatsky
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
#include <SDL3_image/SDL_image.h>

// TODO don't drag or zoom if cursor out of img
// TODO ensure that at zoom_level = 0 scale is precisely 1.
// TODO ensure that during scroll zoom same image pixel remains under cursor
// TODO create window after image is loaded (but window has to exist when image texture is created?)
// TODO is it nessessary to call SDL_DestroyWindow() or smth else before exit()?
// TODO display GUI error msgs

struct State {
    int zoom_level;
    float scale;
    SDL_Window *window;
    SDL_Renderer *renderer;
    float mv_initial_box_x;
    float mv_initial_box_y;
    float mv_initial_ptr_x;
    float mv_initial_ptr_y;
    // viewbox size (absolute) and offset (relative to window)
    SDL_FRect box_in_window_rect;
    int img_w;
    int img_h;
    SDL_Texture *image_texture;
};

void render_window(struct State *state) {
    SDL_RenderClear(state->renderer); // default black (transparent for transparent window)
    // copy image to presentation area in renderer backbuffer
    // TODO ensure that clipping is done correctly without overhead
    SDL_RenderTexture(state->renderer, state->image_texture, NULL, &state->box_in_window_rect);
    // copy renderer backbuffer to frontbuffer
    SDL_RenderPresent(state->renderer);
}

float get_scale(int zoom_level) {
    return pow(0.5, 0.5*(-zoom_level));
}

void load_image(struct State *state, char *filepath) {
    // SDL surface is in RAM, texture is in VRAM (and uses less CPU for processing)
    // IMG_LoadTexture() creates tmp surface, too
    //state->image_texture = IMG_LoadTexture(state->renderer, filepath);
    SDL_Surface *image_surface = IMG_Load(filepath);
    if (NULL == image_surface) {
        SDL_Log("IMG_Load failed");
        exit(1);
    }
    state->img_w = image_surface->w;
    state->img_h = image_surface->h;
    // TODO free previous texture?
    state->image_texture = SDL_CreateTextureFromSurface(state->renderer, image_surface);
    SDL_DestroySurface(image_surface);
    // decode image into surface and get its dimensions
    // calculate initial zoom and viewbox dimensions
    int win_w, win_h;
    SDL_GetWindowSize(state->window, &win_w, &win_h);
    // TODO calculate with respect to both w and h
    // zoom_level = -(logN(0.5, box_h/img_h)/0.5)
    state->zoom_level = -(log((float)win_h/state->img_h)/log(0.5)/0.5)-1;
    state->scale = get_scale(state->zoom_level);
    state->box_in_window_rect.w = state->img_w * state->scale;
    state->box_in_window_rect.h = state->img_h * state->scale;
    // calculate initial box position
    state->box_in_window_rect.x = (win_w - state->box_in_window_rect.w) / 2;
    state->box_in_window_rect.y = (win_h - state->box_in_window_rect.h) / 2;
    render_window(state);
}

void update_move_state(struct State *state, float cur_win_x, float cur_win_y) {
    state->mv_initial_box_x = state->box_in_window_rect.x;
    state->mv_initial_box_y = state->box_in_window_rect.y;
    state->mv_initial_ptr_x = cur_win_x;
    state->mv_initial_ptr_y = cur_win_y;
}

void handle_move(struct State *state, float cur_win_x, float cur_win_y) {
    SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, false);
    // update viewbox position relative to window
    // we must have saved initial coords of move when mouse button was pressed
    state->box_in_window_rect.x = state->mv_initial_box_x + (cur_win_x - state->mv_initial_ptr_x);
    state->box_in_window_rect.y = state->mv_initial_box_y + (cur_win_y - state->mv_initial_ptr_y);
    render_window(state);
    SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, true);
}

void handle_scroll(struct State *state, float cur_win_x, float cur_win_y, char up) {
    // calculate new viewbox size and position
    float cur_img_x = (cur_win_x - state->box_in_window_rect.x) / state->scale;
    float cur_img_y = (cur_win_y - state->box_in_window_rect.y) / state->scale;
    up ? state->zoom_level++ : state->zoom_level--;
    state->scale = get_scale(state->zoom_level);
    state->box_in_window_rect.w = state->img_w * state->scale;
    state->box_in_window_rect.h = state->img_h * state->scale;
    state->box_in_window_rect.x = cur_win_x - cur_img_x * state->scale;
    state->box_in_window_rect.y = cur_win_y - cur_img_y * state->scale;
    update_move_state(state, cur_win_x, cur_win_y);
    render_window(state);
}

// TODO consider moving to SDL3 callbacks
// TODO consider moving all state to state obj
int main(int argc, char** argv)
{
    if( argc < 2 ) {
        SDL_Log("Filepath argument missing");
        exit(1);
    }
    struct State state;
    // TODO current display in multi monitor setup?
    // TODO SDL improvement: not require window size for SDL_WINDOW_MAXIMIZED
    if (!SDL_Init(SDL_INIT_VIDEO)){
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        exit(1);
    }
    int display_count;
    SDL_DisplayID *displays = SDL_GetDisplays(&display_count); // free:
    if(!display_count) {
        SDL_Log("SDL_GetDisplays failed: %s", SDL_GetError());
        exit(1);
    }
    const SDL_DisplayMode* display_mode = SDL_GetDesktopDisplayMode(displays[0]); // free:
    if (NULL==display_mode) {
        SDL_Log("SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
        exit(1);
    }
    if (!SDL_CreateWindowAndRenderer("Lightning Image Viewer", display_mode->w, display_mode->h, SDL_WINDOW_BORDERLESS|SDL_WINDOW_MAXIMIZED|SDL_WINDOW_TRANSPARENT, &state.window, &state.renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        exit(1);
    }
    load_image(&state, argv[1]);
    // event loop
    SDL_Event event;
    char should_exit = false;
    char lmousebtn_pressed = false;
    char should_exit_on_lmousebtn_release;
    while(should_exit == false) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_MOUSE_WHEEL:
                    if (event.wheel.y != 0) {
                        float cur_win_x, cur_win_y;
                        SDL_GetMouseState(&cur_win_x, &cur_win_y);
                        handle_scroll(&state, cur_win_x, cur_win_y, event.wheel.y>0);
                        should_exit_on_lmousebtn_release = false;
                    }
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    if (lmousebtn_pressed) {
                        // coords relative to window
                        handle_move(&state, event.motion.x, event.motion.y);
                        should_exit_on_lmousebtn_release = false;
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        lmousebtn_pressed = true;
                        float cur_win_x, cur_win_y;
                        SDL_GetMouseState(&cur_win_x, &cur_win_y);
                        update_move_state(&state, cur_win_x, cur_win_y);
                        should_exit_on_lmousebtn_release = true;
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        float cur_win_x, cur_win_y;
                        SDL_GetMouseState(&cur_win_x, &cur_win_y);
                        lmousebtn_pressed = false;
                        if (should_exit_on_lmousebtn_release) {
                            should_exit = true;
                        }
                    }
                    break;
                case SDL_EVENT_QUIT:
                    should_exit = true;
                    break;
            }
        }
        SDL_Delay(10);
    }
    return 0;
}
