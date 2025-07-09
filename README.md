# Lightning Image Viewer

Fast and lightweight desktop image viewer featuring minimalistic "transparent fullscreen overlay" UI/UX with controls similar to map apps, implemented in C with SDL3 and SDL3_image; pan/zoom/fullscreen controls basically replicate controls of leaflet.js which powers most web maps (but zoom and keyboard pan are 2x more granular) and Firefox&Chrome browsers. More detailed description:
- maximized transparent borderless window (creating illusion that visible image rectangle is the window, but its size and position is changed upon pan and zoom without messing with window controls)
- zoom with mouse wheel up/down (in/out) or keyboard =/-/0 (in/out/1:1); zoom is discrete with geometric series of scales with multiplier of 2^(1/2) incl. 1:1; mouse zoom preserves image point currently under mouse cursor; keyboard zoom preserves image point currently in the center of the window/screen; upon loading image it is scaled to largest scale in the series in which it fits in window/screen
- pan with left mouse button pressed or with keyboard Up/Down/Left/Right; keyboard pan delta is 40px and, like in map apps, it's "move the camera, not the object", i. e. image is moved in direction opposite to pressed key; upon loading image it is centered
- close with left mouse button click (if no pan nor zoom were performed between button press and release events), keyboard Q, Esc or Enter (latter can be counter habitual since some popular viewers use it for fullscreen, but I find it convenient to open and close with same key)
- toggle fullscreen with middle mouse button click or keyboard F or F11; in fullscreen image is scaled to fit to screen, background is black; pan and zoom in fullscreen also switch to non-fullscreen, proceeding from last non-fullscreen view state (but with rotation and mirroring state changes applied in fullscreen)
- switch to next/previous file in directory with keyboard PgDn/PgUp (sorted by file modification time descending)
- rotate clockwise/counter clockwise with keyboard R/L, mirror (horizontally) with M; changes are NOT saved, app has no code for writing to opened files (I hate viewers overwriting my files when I'm not asking for it)

There are no screenshots (and you can see they make no sense if you try the app), but you can try it right in browser at https://shatsky.github.io/lightning-image-viewer/ (no, app itself has nothing "web", but it can be compiled to WebAssembly and run in browser using Emscripten and its backend in SDL3; natively built app is as "purely native" as it goes with SDL3 native backend providing thin abstraction layer over platform's graphics and input subsystems).

I created it because I like image viewing UX available on certain websites where image is displayed in floating overlay container which can be panned and zoomed like a map and closed with a click on it; I find it very convenient for selective switching between images and looking into their details, and wanted to have it when looking through my local image collections using desktop file manager with mouse and keyboard. Existing image viewers which I know of felt slow to open image from file manager and/or uncomfortable to manipulate its display, to the point that to look through some memorable photos which I have on local storage I would rather go to website on which they are published.

Currently target platforms are Linux (Wayland, X11) and Windows, but it should be possible to make it work on any POSIX-compatible platform supported by SDL3 with minimal effort.

Image formats support depends on SDL3_image build; SDL3_image has "built-in" support for BMP, GIF, JPEG, LBM, PCX, PNG, PNM (PPM/PGM/PBM), QOI, TGA, XCF, XPM, and simple SVG format images; it also has support for AVIF, JPEG-XL, TIFF, and WebP via "external" dependencies; Linux distros normally provide SDL3_image built with all of them. There is also direct support for HEIC via libheif (and possibly for other formats supported by libheif, but I haven't tested).

Note: on Linux Wayland with XWayland SDL3 currently falls back on X11 backend if Wayland compositor lacks support for fifo-v1 protocol (important for games performance, still missing in KDE Plasma kwin_wayland as of writing this). This can be overridden via `SDL_VIDEO_DRIVER=wayland` env var

Licensed under GPLv3. Originally published at https://github.com/shatsky/lightning-image-viewer

## Building and installing

You can use Nix expression to build&install with Nix (naively via `nix-env -i -f default.nix`) or use the Makefile.

Note: Nix expression depends on sdl3 and sdl3-image in nixpkgs (both added in 24.11)

See also GitHub releases page for pre-built Windows binaries and Ubuntu packages or, in case you want to build them yourself, build steps in the workflow in .github/workflows/ which is used to build them.

Note: release artifacts are built with build provenance attestation, allowing to verify that they are built via GitHub Actions workflow on GitHub CI/CD from original source. Attestations are available at https://github.com/shatsky/lightning-image-viewer/attestations (direct link to attestation for specific release should be provided in release notes), verification is as simple (if you trust GitHub to verify its signatures for you) as comparing SHA-256 hash of downloaded file with one listed in attestation. This is particulary helpful for Windows users who might encounter Microsoft antivirus detecting "Trojan/Wacatac.B!ml" or similarly named malware in it (which it randomly detects in unknown unsigned binaries, "!ml" suffix suggests it's AI detection)

Note: Windows binary should work properly on Windows 10 version 1903 (May 2019 update, in which support for setting process code page to UTF-8 via app manifest was added) and later

Note: Ubuntu package is built on/for Ubuntu 25.04 (1st Ubuntu release with SDL3)

## Usage

`lightning-image-viewer [file]` opens file, `lightning-image-viewer` without args displays file selection dialog.

## Main issues

- (may be not true anymore, need to test after move to SDL3_image) ~1% of images (which are viewable in common browsers) fail to load (SDL2_image IMG_Load fails, probably lacks internal error handling for malformed images)
- no animated images support (only 1st frame is displayed)
- GNOME and possibly other graphical environments which have shell UI in top left display corner place invisible window at top left corner of "usable area", causing shift of visible image rectangle from intended position which is calculated with assumption that window is placed at top left display corner

## Roadmap

- fix above listed issues
- sane limits for pan and zoom
- asynchronous loading
- copying to clipboard
- better interpolation in non-1:1 scales, maybe magic kernel sharp (currently linear in <1:1 and nearest in >=1:1, yes I know a pixel is not a little square)
- background/single instance mode for opening with minimal delay (using D-Bus activation on Linux)
- some kind of border (shadow? glassy?), UI menus and dialogs, visual hints about current image (currently displayed in window title which should be visible in taskbar), current scale, controls
- (maybe) video playback (ffmpeg? libvlc?)
- (maybe) touch input support (SDL3 support for it seems not mature yet, most desktop environments translate it into mouse events for compatibility anyway), smooth transitions, continious zoom
- (maybe) some reasonable configurability
- improve EXIF rotation/mirroring tag handling (currently only for JPEG and with re-reading of every loaded file)

## Credits

Special thanks to:
- JYKITORS, for constantly supplying me with free (as in beer, not as in speech) delicious sushi (not as in Sushi file previewer for GNOME/Nautilus; by the way, this viewer can be used like Sushi/QuickLook, toggling (pre)view with Enter key, given that your file manager is configured to open images with it upon Enter key press; or with any other key, if you can configure your file manager accordingly and build the viewer with patched source to bind the key to exit action; check the event loop in `main()`)
- ChatGPT (in case it does have consciousness after all), for its time-saving advice, and all humans who created knowledge which it was trained on
- all conscious humans who don't copypaste valuable lossy-compressed images as pixmaps (which causes loss of compression and addition of new compression artifacts upon repeated lossy compression) but share them as files
