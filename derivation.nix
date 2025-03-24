{ stdenv, sdl3, sdl3-image, libexif }:

stdenv.mkDerivation {
  name = "lightning-image-viewer";
  src = builtins.fetchGit {
    url = ./.;
  };
  buildInputs = [ sdl3 sdl3-image libexif ];
  buildPhase = ''

mkdir -p $out/bin
gcc -DWITH_LIBEXIF -lSDL3 -lSDL3_image -lexif -lm src/viewer.c -o $out/bin/lightning-image-viewer

  '';
  installPhase = ''

cp -R share $out

  '';
}
