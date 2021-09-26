# Lightning Image Viewer

This is a simple image viewer optimized for mouse workflow. It's a prototype/usability experiment, but a working one, I'm currently using it as a daily driver. Key features:

- always fullscreen shaped (if available) borderless window
- zoom with wheel (geometric series with 2^(1/2) multiplier)
- drag with left mouse button pressed
- close with left mouse button click (if no drag nor zoom were performed between button press and release events)

I mostly did it because of 1st and last ones; I hate making extra moves to change image presentation area or close it; shaped window also means that window looks and feels like it has size and position of the image as it is currently laid out on the screen (but changes as needed immediately on zoom, without messing with window manager controls), so it's easier not to lose context (current file manager view of the directory from which it has been opened). Implemented with SDL2 and SDL2_image.

## Building

You can use Nix expression to build&install with Nix (via `nix-env -i -f default.nix`), or use it as a reference to build&install manually (this is currently a single file project).
SDL2 currently has an issue with window shaping on X11 (not sure about other supported platforms): on repeated SDL_SetWindowShape() shape bitmask isn't cleared, so pixel which has became opaque cannot become transparent again (will stay black, as that's default color for clearing SDL2 render buffer). Nix expression builds custom SDL2 with micro patch fixing this.

## Main issues:

- ~5% of images (which are viewable in common browsers) fail to load (SDL2_image fails to recognize format, probably not designed to decode malformed images)
- EXIF rotation info is not supported
- Window shape is not updated sometimes on X11, on Wayland (Weston) it doesn't work at all (falls back to usual fullscreen window with black backround, window shaping is not implemented in SDL2 for Wayland, thought it seems possible to implement with wl_surface_set_input_region)
