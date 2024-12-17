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
#include "SDL.h"
#include "SDL_shape.h"
#include "SDL_image.h"

// TODO don't drag or zoom if cursor out of img
// TODO ensure that at zoom_level = 0 scale is precisely 1.
// TODO ensure that during scroll zoom same image pixel remains under cursor
// TODO create window after image is loaded (but window has to exist when image texture is created?)

struct State {
    int zoom_level;
    float scale;
    SDL_Window *window;
    SDL_Surface *window_shape_surface;
    SDL_Renderer *renderer;
    int mv_initial_box_x;
    int mv_initial_box_y;
    int mv_initial_ptr_x;
    int mv_initial_ptr_y;
    // viewbox size (absolute) and offset (relative to window)
    SDL_Rect box_in_window_rect;
    int img_w;
    int img_h;
    SDL_Texture *image_texture;
};

void render_window(struct State *state) {
    // update window shape (clear with transparent color and then fill viewbox rect)
    SDL_FillRect(state->window_shape_surface, NULL, 0);
    SDL_FillRect(state->window_shape_surface, &state->box_in_window_rect, 1);
    // set window surface as window shape
    // TODO seems that non-transparent area can only become transparent again after resize (which breaks fluency), SDL2 or X11 Shape issue?
    //  https://github.com/libsdl-org/SDL/issues/3140
    //  seems that on X11 shape bitmap is window->shaper->driverdata->bitmap and is cleared here:
    //   https://github.com/libsdl-org/SDL/blob/c59d4dcd38c382a1e9b69b053756f1139a861574/src/video/x11/SDL_x11shape.c#L74
    // TODO get rid of explicit mode
    //  it's default mode but single line in SDL_shape.c doesn't allow NULL here
    //  https://github.com/libsdl-org/SDL/blob/d854ba99c2c0f4534231df14bdba77e7dd32bcba/src/video/SDL_shape.c#L270
    // this is where transparency value is actually set for a px:
    //  https://github.com/libsdl-org/SDL/blob/d854ba99c2c0f4534231df14bdba77e7dd32bcba/src/video/SDL_shape.c#L105
    // TODO investigate bug which causes shape not to be applied sometimes
    SDL_SetWindowShape(state->window, state->window_shape_surface, &(SDL_WindowShapeMode){.mode=ShapeModeDefault});
    
    SDL_RenderClear(state->renderer); // default black
    // copy image to presentation area in renderer backbuffer
    // TODO ensure that clipping is done correctly without overhead
    SDL_RenderCopy(state->renderer, state->image_texture, NULL, &state->box_in_window_rect);
    // copy renderer backbuffer to frontbuffer
    SDL_RenderPresent(state->renderer);
}

float get_scale(int zoom_level) {
    return pow(0.5, 0.5*(-zoom_level));
}

void load_image(struct State *state, char *filepath) {
    int img_init_flags = IMG_INIT_JPG|IMG_INIT_PNG|IMG_INIT_TIF;
    if (IMG_Init(img_init_flags)&img_init_flags != img_init_flags) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "IMG_Init failed: %s\n", IMG_GetError());
        // TODO ensure that we can contiue without some formats
    }
    // SDL surface is in RAM, texture is in VRAM (and uses less CPU for processing)
    // IMG_LoadTexture() creates tmp surface, too
    SDL_Surface *image_surface = IMG_Load(filepath);
    if (NULL == image_surface) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "IMG_Load failed: %s", IMG_GetError());
        // TODO launch accompanying app to display error message
        SDL_DestroyWindow(state->window);
        SDL_VideoQuit();
        exit(1);
    }
    state->img_w = image_surface->w;
    state->img_h = image_surface->h;
    // TODO free previous texture?
    //state->image_texture = IMG_LoadTexture(state->renderer, filepath);
    state->image_texture = SDL_CreateTextureFromSurface(state->renderer, image_surface);
    SDL_FreeSurface(image_surface);
    // decode image into surface and get its dimensions
    // calculate initial zoom and viewbox dimensions
    // zoom_level = -(logN(0.5, box_h/img_h)/0.5)
    state->zoom_level = -(log((float)state->window_shape_surface->h/state->img_h)/log(0.5)/0.5)-1;
    state->scale = get_scale(state->zoom_level);
    //SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loaded image, w:%d, h:%d, zoom_level:%d", state->img_w, state->img_h, state->zoom_level);
    state->box_in_window_rect.w = state->img_w * state->scale;
    state->box_in_window_rect.h = state->img_h * state->scale;
    // calculate initial box position
    state->box_in_window_rect.x = (state->window_shape_surface->w - state->box_in_window_rect.w) / 2;
    state->box_in_window_rect.y = (state->window_shape_surface->h - state->box_in_window_rect.h) / 2;
    render_window(state);
}

void update_move_state(struct State *state, int cur_win_x, int cur_win_y) {
    state->mv_initial_box_x = state->box_in_window_rect.x;
    state->mv_initial_box_y = state->box_in_window_rect.y;
    state->mv_initial_ptr_x = cur_win_x;
    state->mv_initial_ptr_y = cur_win_y;
}

void handle_move(struct State *state, int cur_win_x, int cur_win_y) {
    SDL_EventState(SDL_MOUSEMOTION, SDL_IGNORE);
    // update viewbox position relative to window
    // we must have saved initial coords of move when mouse button was pressed
    state->box_in_window_rect.x = state->mv_initial_box_x + (cur_win_x - state->mv_initial_ptr_x);
    state->box_in_window_rect.y = state->mv_initial_box_y + (cur_win_y - state->mv_initial_ptr_y);
    render_window(state);
    SDL_EventState(SDL_MOUSEMOTION, SDL_ENABLE);
}

void handle_scroll(struct State *state, int cur_win_x, int cur_win_y, char up) {
    // calculate new viewbox size and position
    int cur_img_x = (cur_win_x - state->box_in_window_rect.x) / state->scale + 0.5;
    int cur_img_y = (cur_win_y - state->box_in_window_rect.y) / state->scale + 0.5;
    up ? state->zoom_level++ : state->zoom_level--;
    state->scale = get_scale(state->zoom_level);
    state->box_in_window_rect.w = state->img_w * state->scale;
    state->box_in_window_rect.h = state->img_h * state->scale;
    state->box_in_window_rect.x = cur_win_x - cur_img_x * state->scale;
    state->box_in_window_rect.y = cur_win_y - cur_img_y * state->scale;
    update_move_state(state, cur_win_x, cur_win_y);
    render_window(state);
}

void init(struct State *state) {
    // TODO handle multiple displays?
    SDL_DisplayMode display_mode;
    if (SDL_GetDesktopDisplayMode(0, &display_mode) != 0) {
        SDL_Log("SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
        SDL_VideoQuit();
        exit(1);
    }
    state->window = SDL_CreateShapedWindow("Lightning Image Viewer",
        0, 0,
        display_mode.w, display_mode.h,
        0);
    // fallback on usual window
    if(state->window == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateShapedWindow failed");
        state->window = SDL_CreateWindow("Lightning Image Viewer",
            0, 0,
            display_mode.w, display_mode.h,
            0);
            if(state->window == NULL) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed");
                SDL_VideoQuit();
                exit(-1);
            }
        }
    
    state->renderer = SDL_CreateRenderer(state->window, -1, 0);
    
    // TODO still need to set fullscreen if we want to stay over shell
    //SDL_SetWindowFullscreen(state->window, SDL_WINDOW_FULLSCREEN_DESKTOP);

    // TODO 4bytes per mask px is a waste
    // SDL2 doesn't seem to allow 8Bpp surface with alpha, w/ different depth and masks it either fails to create surface
    //  or reads alpha=255 for any px value
    // it might be a better idea to switch use the ColorKey shaping mode
    // rmask, gmask, bmask, amask
    // TODO endianness safe?
    state->window_shape_surface = SDL_CreateRGBSurface(0, display_mode.w, display_mode.h, 32, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff);
    
    state->zoom_level = 0;
}

int main(int argc,char** argv)
{
    SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);
    if(SDL_VideoInit(NULL) == -1) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_VideoInit failed");
        exit(1);
    }
    if( argc < 2 ) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Filepath argument missing");
        SDL_VideoQuit();
        exit(1);
    }

    struct State state;
    init(&state);
    load_image(&state, argv[1]);

    SDL_Event event;
    char should_exit = 0;
    char lmousebtn_pressed = 0;
    char should_exit_on_lmousebtn_release;
    while(should_exit == 0) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_MOUSEWHEEL:
                    if (event.wheel.y != 0) {
                        int cur_win_x, cur_win_y;
                        SDL_GetMouseState(&cur_win_x, &cur_win_y);
                        handle_scroll(&state, cur_win_x, cur_win_y, event.wheel.y>0);
                        should_exit_on_lmousebtn_release = 0;
                    }
                    break;
                case SDL_MOUSEMOTION:
                    if (lmousebtn_pressed) {
                        // coords relative to window
                        handle_move(&state, event.motion.x, event.motion.y);
                        should_exit_on_lmousebtn_release = 0;
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        lmousebtn_pressed = 1;
                        int cur_win_x, cur_win_y;
                        SDL_GetMouseState(&cur_win_x, &cur_win_y);
                        update_move_state(&state, cur_win_x, cur_win_y);
                        should_exit_on_lmousebtn_release = 1;
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        int cur_win_x, cur_win_y;
                        SDL_GetMouseState(&cur_win_x, &cur_win_y);
                        lmousebtn_pressed = 0;
                        if (should_exit_on_lmousebtn_release) {
                            should_exit = 1;
                        }
                    }
                    break;
                case SDL_QUIT:
                    should_exit = 1;
                    break;
            }
        }
        SDL_Delay(10);
    }

    SDL_DestroyWindow(state.window);
    SDL_VideoQuit();
    return 0;
}
