# Lightning Image Viewer

Fast and lightweight desktop image viewer featuring minimalistic "transparent fullscreen overlay" UI/UX with controls similar to map apps, implemented in C and Rust with SDL3 and image-rs; pan/zoom/fullscreen controls basically replicate controls of leaflet.js which powers most web maps (but zoom and keyboard pan are 2x more granular) and Firefox&Chrome browsers. More detailed description:
- maximized transparent borderless window (creating illusion that visible image rectangle is the window, but its size and position is changed upon pan and zoom without messing with window controls)
- zoom with mouse wheel up/down (in/out) or keyboard =/-/0 (in/out/1:1); zoom is discrete with geometric series of scales with multiplier of 2^(1/2) incl. 1:1; mouse zoom preserves image point currently under mouse cursor; keyboard zoom preserves image point currently in the center of the window/screen; upon loading image it is scaled to largest scale in the series in which it fits in window/screen
- pan with left mouse button pressed or with keyboard Up/Down/Left/Right; keyboard pan delta is 40px and, like in map apps, it's "move the camera, not the object", i. e. image is moved in direction opposite to pressed key; upon loading image it is centered
- close with left mouse button click (if no pan nor zoom were performed between button press and release events), keyboard Q, Esc or Enter (latter can be counter habitual since some popular viewers use it for fullscreen, but I find it convenient to open and close with same key)
- toggle fullscreen with middle mouse button click or keyboard F or F11; in fullscreen image is scaled to fit to screen, background is black; pan and zoom in fullscreen also switch to non-fullscreen, proceeding from last non-fullscreen view state (but with rotation and mirroring state changes applied in fullscreen)
- switch to next/previous file in directory with keyboard PgDn/PgUp (sorted by file modification time descending)
- rotate clockwise/counter clockwise with keyboard R/L, mirror (horizontally) with M; changes are NOT saved, app has no code for writing to opened files (I hate viewers overwriting my files when I'm not asking for it)
- toggle animation pause/playback with keyboard Space

You can see screenshot in [Microsoft Store](https://apps.microsoft.com/detail/9np4j8k90smk) or try the app right in browser at https://shatsky.github.io/lightning-image-viewer/ (no, it has nothing "web" itself, but it can be compiled to WebAssembly and run in browser using Emscripten and its backend in SDL3; natively built app is as "purely native" as it goes with SDL3 native backend providing thin abstraction layer over platform's graphics and input subsystems).

I created it because I like image viewing UX available on certain websites where image is displayed in floating overlay container which can be panned and zoomed like a map and closed with a click on it; I find it very convenient for selective switching between images and looking into their details, and wanted to have it when looking through my local image collections using desktop file manager with mouse and keyboard. Existing image viewers which I know of felt slow to open image from file manager and/or uncomfortable to manipulate its display, to the point that to look through some memorable photos which I have on local storage I would rather go to website on which they are published.

Currently target platforms are Linux (Wayland, X11) and Windows, but it should be possible to make it work on any POSIX-compatible platform supported by SDL3 with minimal effort.

Supported image formats: all formats supported by image-rs enabled by default: AVIF, BMP, DDS, EXR, FF, GIF, HDR, ICO, JPEG, PNG, PNM, QOI, ~~TGA~~, TIFF, WEBP (w/ animation support for GIF, PNG aka APNG and WEBP; w/ orientation metadata support incl. EXIF; see https://docs.rs/image/latest/image/codecs/index.html#supported-formats ); JXL (via jxl-oxide) and HEIC (via libheif-rs).

Note: some image formats, esp. newest ones based on modern video codecs keyframes (AVIF, HEIC) are very complex and it's likely that not all possible variants are supported; also TGA decoding seems to fail for unknown reason.

Note: on Linux Wayland with XWayland SDL3 currently falls back on X11 backend if Wayland compositor lacks support for fifo-v1 protocol (important for games performance, seems to be supported by all major compositors now). This can be overridden via `SDL_VIDEO_DRIVER=wayland` env var

Licensed under GPLv3. Originally published at https://github.com/shatsky/lightning-image-viewer

## Building and installing

You can use Nix expression to build&install with Nix or use the Makefile (deps: C and Rust toolchains, SDL3, libheif). Nix build&install commands reference:
- into user profile from release snapshot: `nix-env -i -f https://github.com/shatsky/lightning-image-viewer/archive/refs/tags/{release_tag}.tar.gz` (check https://github.com/shatsky/lightning-image-viewer/releases/latest for latest release tag/snapshot URI)
- into user profile from development snapshot (most recent commit on main branch): `nix-env -i -f https://github.com/shatsky/lightning-image-viewer/archive/refs/heads/main.zip`
- into user profile from locally cloned repo: `nix-env -i -f .`
- just produce `lightning-image-viewer` binary in repo dir (useful during development): `nix-shell` and then `make`
- there's also Nix flake, but I don't care much about it for now, though I make sure to test it too with `nix --extra-experimental-features 'nix-command flakes' run .`

See GitHub releases page for pre-built Windows binaries and Ubuntu packages or, in case you want to build them yourself, build steps in the workflow in .github/workflows/ which is used to build them.

Windows binaries are also published in [Microsoft Store](https://apps.microsoft.com/detail/9np4j8k90smk) .

Note: release artifacts are built with build provenance attestation, allowing to verify that they are built via GitHub Actions workflow on GitHub CI/CD from original source. Attestations are available at https://github.com/shatsky/lightning-image-viewer/attestations (direct link to attestation for specific release should be provided in release notes), verification is as simple (if you trust GitHub to verify its signatures for you) as comparing SHA-256 hash of downloaded file with one listed in attestation.

Note: Windows binary should work properly on Windows 10 version 1903 (May 2019 update, in which support for setting process code page to UTF-8 via app manifest was added) or later

Note: Ubuntu package is built on/for Ubuntu 25.04 (1st Ubuntu release with SDL3)

Note: on Windows, installing from Microsoft Store allows to manage file types associations conveniently; Settings -> Apps -> Default Apps -> Lightning Image Viewer page lists file types supported by app

## Usage

`lightning-image-viewer [file]` opens file, `lightning-image-viewer` without args displays file selection dialog.

## Roadmap

- fix issue which occurs in GNOME and possibly other graphical environments which have shell UI in top left display corner and place invisible window at top left corner of "usable area", causing shift of visible image rectangle from intended position which is calculated with assumption that window is placed at top left display corner
- sane limits for pan and zoom
- display ahead of load (decode) completion (1st frame of animation, partial frame decoding, embedded thumbnail, external thumbnail used by file manager)
- copying to clipboard
- better interpolation in non-1:1 scales, maybe magic kernel sharp (currently linear)
- background/single instance mode for opening with minimal delay (using D-Bus activation on Linux)
- proper shadow (currently primitive semi transparent rect) and possibly some kind of border, UI menus and dialogs (currently only open file dialog and message explaining exit behavior upon 1st left click or Enter key press if launched without file cmdline arg), visual hints about current file and zoom (currently only filename displayed in window title which should be visible in taskbar), controls
- some reasonable runtime configurability
- ICC profile support (and other metadata which should affect resulting pixels)
- test suite covering non trivial variants of supported image formats (currently tested on trivial image set generated with imagemagick)
- (maybe) video playback (ffmpeg? libvlc? gstreamer?)
- (maybe) touch input support (SDL3 support for it seems not mature yet, most desktop environments translate it into mouse events for compatibility anyway), smooth transitions, continuous zoom
- (maybe) vector graphics (SVG) support
- (maybe) rewrite remaining parts in Rust

## Credits

Special thanks to:
- JYKITORS, for constantly supplying me with free (as in beer, not as in speech) delicious sushi (not as in Sushi file previewer for GNOME/Nautilus; by the way, this viewer can be used like Sushi/QuickLook, toggling (pre)view with Enter key, given that your file manager is configured to open images with it upon Enter key press; or with any other key, if you can configure your file manager accordingly and build the viewer with patched source to bind the key to exit action; check the event loop in `main()`)
- ChatGPT (in case it does have consciousness after all), for its time-saving advice, and all humans who created knowledge which it was trained on
- all conscious humans who don't copypaste valuable lossy-compressed images as pixmaps (which causes loss of compression and addition of new compression artifacts upon repeated lossy compression) but share them as files
