# Lightning Image Viewer

This is a simple minimalistic lightweight image viewer with UX optimized for mouse workflow with common file managers and non-retina displays, replicating image viewing UX of some websites:

- maximized transparent borderless window
- zoom with wheel (geometric series with 2^(1/2) multiplier)
- drag with left mouse button pressed
- close with left mouse button click (if no drag nor zoom were performed between button press and release events)

I. e. it feels like window is always sized and placed as current image presentation area, resizes and moves automatically when zooming and dragging image, and clicking on it anywhere closes it; resulting in minimal required movements and distraction when opening random images from file manager, zooming into some detail and closing them.

Implemented in C with SDL3 and SDL3_image.

## Building

You can use Nix expression to build&install with Nix (via `nix-env -i -f default.nix`), or use it as a reference to build&install manually (this is currently a single file project, see buildPhase in derivation.nix).

Note: Nix expression tested against stable nixpkgs 24.11, broken against previous 24.05 (SDL3 is not even in 24.11 yet, so I copied Nix expression for SDL3 from open PR which depends on nixpkgs changes added between these)

### Building for Windows

This is how I currently build Windows binary on NixOS:
- env with x86_64-w64-mingw32-gcc: `nix-shell -p pkgsCross.mingwW64.pkgsBuildHost.gcc`
- download and extract https://github.com/libsdl-org/SDL/releases/download/preview-3.1.6/SDL3-devel-3.1.6-mingw.tar.gz and https://github.com/libsdl-org/SDL_image/releases/download/preview-3.1.0/SDL3_image-devel-3.1.0-mingw.zip
- build cmd: `x86_64-w64-mingw32-gcc src/viewer.c -ISDL3-3.1.6/x86_64-w64-mingw32/include -ISDL3_image-3.1.0/x86_64-w64-mingw32/include -LSDL3-3.1.6/x86_64-w64-mingw32/lib -LSDL3_image-3.1.0/x86_64-w64-mingw32/lib -l:libSDL3.dll.a -l:libSDL3_image.dll.a -mwindows`
- put SDL3-3.1.6/x86_64-w64-mingw32/bin/SDL3.dll and SDL3_image-3.1.0/x86_64-w64-mingw32/bin/SDL3_image.dll in same dir with binary

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

- (may be not true anymore, need to test after move to SDL3_image) ~1% of images (which are viewable in common browsers) fail to load (SDL2_image IMG_Load fails, probably lacks internal error handling for malformed images)
- EXIF rotation info is not supported
