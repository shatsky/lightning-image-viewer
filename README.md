# Lightning Image Viewer

This is a simple minimalistic lightweight image viewer with UX optimized for workflow with common file managers, mouse & non-retina display, replicating image viewing UX of some websites:

- maximized shaped (if available) borderless window
- zoom with wheel (geometric series with 2^(1/2) multiplier)
- drag with left mouse button pressed
- close with left mouse button click (if no drag nor zoom were performed between button press and release events)

This results in minimal required movements and distraction when opening random images from file manager, zooming into some detail and closing them.

Implemented with SDL2 and SDL2_image.

## Building

You can use Nix expression to build&install with Nix (via `nix-env -i -f default.nix`), or use it as a reference to build&install manually (this is currently a single file project).
SDL2 currently has an issue with window shaping on X11 (not sure about other supported platforms): on repeated SDL_SetWindowShape() shape bitmask isn't cleared, so pixel which has became opaque cannot become transparent again (will stay black, as that's default color for clearing SDL2 render buffer). Nix expression builds custom SDL2 with micro patch fixing this.

### Building for Windows

This is how I currently build self-sufficient Windows binary on NixOS:
- env with x86_64-w64-mingw32-gcc: `nix-shell -p pkgsCross.mingwW64.pkgsBuildHost.gcc`
- download and extract https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-mingw.tar.gz and https://github.com/libsdl-org/SDL/releases https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.2/SDL2_image-devel-2.8.2-mingw.tar.gz
- build cmd (hacked this together with help of chatgpt without full understanding of Windows-specific opts, will leave here for now): `x86_64-w64-mingw32-gcc -v src/viewer.c -ISDL2-2.30.9/x86_64-w64-mingw32/include/SDL2 -ISDL2_image-2.8.2/x86_64-w64-mingw32/include/SDL2 -LSDL2-2.30.9/x86_64-w64-mingw32/lib -LSDL2_image-2.8.2/x86_64-w64-mingw32/lib -lmingw32 -l:libSDL2main.a -l:libSDL2.a -l:libSDL2_image.a -lwinmm -ldxguid -lsetupapi -lole32 -limm32 -lversion -loleaut32 -mwindows`

Note: it might be possible to have unified Nix expr for cross building without using pre-built SDL, but `pkgsCross.mingwW64.SDL2_image` currently fails to build (seems that most pkgs are broken for mingwW64, though SDL2 itself builds successfully)

Note: nixpkgs has concept of 3 platforms:
- "buildPlatform" (on which program is to be built, i. e. on which Nix runs)
- "hostPlatform" (on which built program is to be executed)
- "targetPlatform" (for which built program emits code, only relevant for compilers)

On x86_64 NixOS:
- for any pkg, buildPlatform is "x86_64-linux" (overriding it doesn't make sense)
- for "usual" pkgs (which are to be executed on same platform), hostPlatform is also "x86_64-linux"
- for pkgs in `pkgsCross.mingwW64` set, hostPlatform is "x86_64-w64-mingw32"
- for pkgs in `pkgsCross.mingwW64.pkgsBuildHost` set, hostPlatform is "x86_64-linux", targetPlatform is "x86_64-w64-mingw32" (`pkgs<host><target>` pkgs sets have overridden "host" and "target" platforms, in case of `pkgsBuildHost` hostPlatform -> buildPlatform, targetPlatform -> hostPlatform)

Note: "x86_64-w64-mingw32" and "x86_64-linux" are "target triplets" describing target for which compiler produces binary, see https://wiki.osdev.org/Target_Triplet

## Main issues:

- ~1% of images (which are viewable in common browsers) fail to load (SDL2_image fails to recognize format, probably not designed to decode malformed images)
- EXIF rotation info is not supported
- Window shape is not updated sometimes on X11, on Wayland (Weston) it doesn't work at all (falls back to usual fullscreen window with black backround, window shaping is not implemented in SDL2 for Wayland, thought it seems possible to implement with wl_surface_set_input_region)
