# cargo libheif-sys uses pkg-config to find libheif
{ rustPlatform, sdl3, libheif, cargo, pkg-config }:

rustPlatform.buildRustPackage {
  name = "lightning-image-viewer";
  src = ./.;
  # fetch Rust deps before entering offline build env
  cargoLock.lockFile = ./Cargo.lock;
  buildInputs = [ sdl3 libheif ];
  nativeBuildInputs = [ cargo pkg-config ];
  # default phases are aware only about cargo stuff, of course
  buildPhase = "make";
  installPhase = "make install PREFIX=$out";
}
