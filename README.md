# Lightning Image Viewer

![Screenshot](https://github.com/user-attachments/assets/eec3a227-2afb-4249-ae61-b0775d228884)

Fast and lightweight desktop image (pre)viewer with 3 key UX features:
- transparent fullscreen overlay; this means nothing but display borders limits visible image surface during pan&zoom, but at the same time underlying windows are visible outside of it (unless you toggle non transparent fit to display view)
- pan&zoom into detail in one move; just move cursor onto the detail and drag it towards center with left button pressed while scrolling to zoom simultaneously (like in map apps; also possible to pan with keyboard arrows and zoom with +=/-/0)
- toggle between file manager and image view; it can be closed with left click anywhere or keyboard Enter, so that image can be opened and closed by pressing same button/key repeatedly (and it opens typical "web optimized" images almost instantly)
Detailed controls description is in "Usage" section.

Implemented in ~~C and~~ Rust with SDL3 and image-rs. It does not currently do any magic to display images "lightning fast", but it's significantly faster than "common" desktop image viewers; just because it's small bloat-free native (as in "native code") app. Image loading speed should be on par with other lightweight viewers like feh; but "Lightning" in its name refers primarily to its UX, which allows to randomly open/close images from file manager and zoom/pan into details almost instantly (and also to something which inspired me to create it). You will probably like it if you have lots of downloaded images and photos from gadgets in your desktop computer, prefer to organize/browse them with file manager, like to look into details (pan&zoom to make certain object of composition fill your field of view/display, especially relevant for art) and you've got feeling that it's more comfortable to view images online with some webapp embedded viewers than local ones with local apps.

If screenshot and description above are not enough, you can try the app right in browser at https://shatsky.github.io/lightning-image-viewer/ (no, it's NOT a webapp, but it can be compiled to WebAssembly and run in common browsers, rendering to canvas via SDL3 backend).

Platforms: currently targeting Linux (Wayland, X11) and Windows, but it should be possible to make it work on any POSIX-compatible platform supported by SDL3 with minimal effort.

Image formats: all formats supported by image-rs enabled by default: AVIF, BMP, DDS, EXR, FF, GIF, HDR, ICO, JPEG, PNG, PNM, QOI, ~~TGA~~, TIFF, WEBP (w/ animation support for GIF, PNG aka APNG and WEBP; w/ orientation metadata support incl. EXIF; see https://docs.rs/image/latest/image/codecs/index.html#supported-formats ); JXL (via jxl-oxide) and HEIC (via libheif-rs).

Note: some image formats, esp. newest ones based on modern video codecs keyframes (AVIF, HEIC) are very complex and it's likely that not all possible variants are supported; also TGA decoding seems to fail for unknown reason

Licensed under GPLv3+. Originally published at https://github.com/shatsky/lightning-image-viewer

## Building and installing

You can use Nix expression to build&install with Nix or use the Makefile (deps: C and Rust toolchains, SDL3, libheif). Nix build&install commands reference:
- into user profile from release snapshot: `nix-env -i -f https://github.com/shatsky/lightning-image-viewer/archive/refs/tags/{release_tag}.tar.gz` (check https://github.com/shatsky/lightning-image-viewer/releases/latest for latest release tag/snapshot URI)
- into user profile from development snapshot (most recent commit on main branch): `nix-env -i -f https://github.com/shatsky/lightning-image-viewer/archive/refs/heads/main.zip`
- into user profile from locally cloned repo: `nix-env -i -f .`
- just produce `target/release/lightning-image-viewer` binary in repo dir (useful during development): `nix-shell` and then `make`
- there's also Nix flake, but I don't care much about it for now, though I make sure to test it too with `nix --extra-experimental-features 'nix-command flakes' run .`

See GitHub releases page for pre-built Windows binaries and Ubuntu packages or, in case you want to build them yourself, build steps in the workflow in .github/workflows/ which is used to build them.

Windows binaries are also published in [Microsoft Store](https://apps.microsoft.com/detail/9np4j8k90smk) ; it's recommended for Windows users to get the app from there, because it allows to manage file types associations conveniently; Windows Settings -> Apps -> Default Apps -> Lightning Image Viewer page lists file types supported by app.

Note: release artifacts are built with build provenance attestation, allowing to verify that they are built via GitHub Actions workflow on GitHub CI/CD from original source. Attestations are available at https://github.com/shatsky/lightning-image-viewer/attestations (direct link to attestation for specific release should be provided in release notes), verification is as simple (if you trust GitHub to verify its signatures for you) as comparing SHA-256 hash of downloaded file with one listed in attestation.

Note: Windows binary should work properly on Windows 10 version 1903 (May 2019 update, in which support for setting process code page to UTF-8 via app manifest was added) or later

Note: Ubuntu package is built on/for Ubuntu 25.04 (1st Ubuntu release with SDL3)

Note: on Linux Wayland with XWayland SDL3 currently falls back on X11 backend if Wayland compositor lacks support for fifo-v1 protocol (important for games performance, seems to be supported by all major compositors now), which can cause flickering. Wayland backend can be forced via `SDL_VIDEO_DRIVER=wayland` env var

## Usage

`lightning-image-viewer [file]` opens file, `lightning-image-viewer` without args displays file selection dialog. This app is intended to be launched via opening image files from file manager, being set as default app for image files of supported types; however it doesn't provide means to set file type associations, except for platform-specific metadata which lets platform know that is supports specific file types; please check your platform or file manager file type associations settings.

Controls:
- zoom: mouse scroll (into detail under cursor) or keyboard +=/-/0 (into detail in center of display, if left mouse button is not pressed, otherwise into detail under cursor, 0 for 1:1); zoom is discrete with geometric series of scales with multiplier of 2^(1/2) incl. 1:1; initially image is scaled to the largest scale in the series in which it fits in window/screen
- pan: mouse move with left button pressed or keyboard arrows; keyboard pan delta is 40px and, like in map apps, it's "move the camera, not the object", i. e. image is moved in direction opposite to pressed key; initially image is centered
- close: left mouse button click (if no action happened between press and release events), keyboard Enter (allowing "quick toggle" between file manager and image view by pressing same button/key repeatedly), Q or Esc
- toggle non transparent fit to display view aka fullscreen: middle mouse button click or keyboard F or F11; in fullscreen image is scaled to fit to screen, background is black; it's not pannable/zoomable, pan and zoom in fullscreen switch back to non-fullscreen, proceeding from last non-fullscreen view state (but rotation and mirroring state changes applied in fullscreen persist)
- toggle animation pause/playback: keyboard Space
- switch next/previous file in directory: keyboard PgDn/PgUp (sorted by file modification time descending)
- rotate and mirror: keyboard R/L (rotate 90 degrees clockwise/counter clockwise), M (mirror horizontally); changes are NOT saved, app has NO code for writing to files (I hate viewers overwriting my files when I'm not asking for it); initially image is rotated and mirrored as per EXIF and other supported metadata
- switch scalemode (interpolation): keyboard S; currently toggles between "linear" and "nearest"; initially "linear"

Note: pan/zoom/fullscreen controls basically replicate controls of leaflet.js which powers most web maps (but zoom and keyboard pan are 2x more granular) and Firefox&Chrome browsers

## Development

Roadmap:
- fix issue which occurs in GNOME and possibly other graphical environments which have shell UI in top left display corner and place invisible window at top left corner of "usable area", causing shift of visible image rectangle from intended position which is calculated with assumption that window is placed at top left display corner
- sane limits for pan and zoom
- display ahead of load (decode) completion (1st frame of animation, partial frame decoding, embedded thumbnail, external thumbnail used by file manager), preload next/prev, loading in background thread
- copying to clipboard
- better interpolation in non-1:1 scales, maybe magic kernel sharp (currently linear with possibility to toggle nearest)
- background/single instance mode for opening with minimal delay (using D-Bus activation on Linux)
- proper shadow (currently primitive semi transparent rect) and possibly some kind of border, UI menus and dialogs (currently only open file dialog and message explaining exit behavior upon 1st left click or Enter key press if launched without file cmdline arg), visual hints about current file and zoom (currently only filename displayed in window title which should be visible in taskbar), controls
- some reasonable runtime configurability
- ICC profile support (and other metadata which should affect resulting pixels)
- test suite covering non trivial variants of supported image formats (currently tested on trivial image set generated with imagemagick)
- possibility to map keys for running external tools for interactively moving to subdir (for sorting images) and for displaying specified text file containing lines starting with ISO datetime in chronological order with highlighted line having latest datetime preceding current image mtime (for viewing personal diary text file in context of photos or vice versa)
- (maybe) video playback (ffmpeg? libmpv? gstreamer?)
- (maybe) touch input support (SDL3 support for it seems not mature yet, most desktop environments translate it into mouse events for compatibility anyway), smooth transitions, continuous zoom
- (maybe) vector graphics (SVG) support

Non-goals:
- support lots of rare formats; the point of project is to satisfy usecase of viewing collections of downloaded images and photos from gadgets, which means primarily popular image publishing formats; it can possibly be extended to support formats needed by CG artists and photographers using popular professional software and hardware
- support high configurability which would allow to get totally different UX; the point of the project is to provide concrete UX which I find most convenient, with limited configurability to capture its "close neighborhood" in "UX space" to make it best viewer for specific group of users which prefer this or similar UX

Development notes: https://shatsky.github.io/notes/2025-03-07_sdl3-image-viewer.html

## Credits

Special thanks to:
- JYKITORS, for constantly supplying me with free (as in beer, not as in speech) delicious sushi (not as in Sushi file previewer for GNOME/Nautilus; by the way, this viewer can be used like Sushi/QuickLook, toggling (pre)view with Enter key, given that your file manager is configured to open images with it upon Enter key press; or with any other key, if you can configure your file manager accordingly and build the viewer with patched source to bind the key to exit action; check the event loop in `main()`)
- ChatGPT (in case it does have consciousness after all), for its time-saving advice, and all humans who created knowledge which it was trained on
- all conscious humans who don't copypaste valuable lossy-compressed images as pixmaps (which causes loss of compression and addition of new compression artifacts upon repeated lossy compression) but share them as files
