# Lightning Image Viewer

Just another simple minimalistic lightweight image viewer. Why? Because I like image viewing UX available on certain websites where image is displayed in floating overlay container which can be dragged and zoomed like a map; I wanted to have it when looking through my local image collections using desktop file manager with mouse and non-retina display. More detailed UX description:

- maximized transparent borderless window
- zoom with wheel (geometric series of scales with multiplier of 2^(1/2) incl. 1:1)
- drag with left mouse button pressed
- close with left mouse button click (if no drag nor zoom were performed between button press and release events)
- toggle fullscreen with middle mouse button click (in fullscreen image is scaled to fit the screen)

Implemented in C with SDL3 and SDL3_image. Should work on any platform supported by SDL3, tested on Linux (Wayland, X11) and Windows

Licensed under GPLv3. Originally published at https://github.com/shatsky/lightning-image-viewer

## Building

You can use Nix expression to build&install with Nix (via `nix-env -i -f default.nix`), or use it as a reference to build&install manually (this is currently a single file project, see buildPhase in derivation.nix).

Note: Nix expression tested against stable nixpkgs 24.11, broken against previous 24.05 (SDL3 is not even in 24.11 yet, so I copied Nix expression for SDL3 from open PR which depends on nixpkgs changes added between these)

Note: image formats support depends on SDL3_image build, which can have it disabled for some common formats incl. WebP

### Building for Windows

If you are normal Windows developer using normal Windows toolchain, this section is just for facepalm^W reference.

This is how I currently manually build Windows binary on NixOS:
- env with x86_64-w64-mingw32-gcc: `nix-shell -p pkgsCross.mingwW64.pkgsBuildHost.gcc`
- download and extract https://github.com/libsdl-org/SDL/releases/download/preview-3.1.6/SDL3-devel-3.1.6-mingw.tar.gz and https://github.com/libsdl-org/SDL_image/releases/download/preview-3.1.0/SDL3_image-devel-3.1.0-mingw.zip
- convert icon to .ico: `magick share/icons/hicolor/scalable/apps/lightning-image-viewer.svg lightning-image-viewer.ico`
- build .o with icon: `x86_64-w64-mingw32-windres src/viewer.rc icon.o`
- build .exe: `x86_64-w64-mingw32-gcc src/viewer.c icon.o -ISDL3-3.1.6/x86_64-w64-mingw32/include -ISDL3_image-3.1.0/x86_64-w64-mingw32/include -LSDL3-3.1.6/x86_64-w64-mingw32/lib -LSDL3_image-3.1.0/x86_64-w64-mingw32/lib -l:libSDL3.dll.a -l:libSDL3_image.dll.a -mwindows -o lightning-image-viewer.exe`
- download and extract https://github.com/libsdl-org/SDL_image/releases/download/preview-3.1.0/SDL3_image-devel-3.1.0-VC.zip (because mingw build is built without "optional formats" support incl. WebP)
- put SDL3-3.1.6/x86_64-w64-mingw32/bin/SDL3.dll , ~~SDL3_image-3.1.0/x86_64-w64-mingw32/bin/SDL3_image.dll~~ SDL3_image-3.1.0/lib/x64/SDL3_image.dll and SDL3_image-3.1.0/lib/x64/optional/*.dll in same dir with .exe

Note: I guess mixing DLLs built with mingw and VC in single process is not safe in general, but for SDL3 which is written in C and has stable ABI it should be fine and seems to work without issues, correct me if I'm wrong

Note: it might be possible to have unified Nix expr for cross building without using pre-built SDL, but ~~`pkgsCross.mingwW64.SDL2_image` currently fails to build (seems that most pkgs are broken for mingwW64, though SDL2 itself builds successfully)~~ might return to this after stable SDL3 version is released and added to nixpkgs

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

## Main issues

- (may be not true anymore, need to test after move to SDL3_image) ~1% of images (which are viewable in common browsers) fail to load (SDL2_image IMG_Load fails, probably lacks internal error handling for malformed images)
- EXIF rotation info is not taken into account

## Roadmap

- keyboard controls (consistent with Firefox and map apps)
- switching to next/previous image in directory
- rotation and mirroring
- copying to clipboard
- interpolation in non-1:1 scales, maybe magic kernel sharp (currently nearest, yes I know a pixel is not a little square)
- background/single instance mode for opening with minimal delay (using D-Bus activation on Linux)
- some kind of border (shadow? glassy?), visual hints about current image, scale, controls

## Credits

Special thanks to:
- JYKITORS, for constantly supplying me with free (as in beer, not as in speech) delicious sushi
- ChatGPT (in case it does have consciousness after all), for its time-saving advice, and all humans who created knowledge which it was trained on
- all conscious humans who don't copypaste valuable lossy-compressed images as pixmaps (which causes loss of compression and addition of new compression artifacts upon repeated lossy compression) but share them as files
