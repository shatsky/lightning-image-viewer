{ stdenv, sdl3, sdl3_image }:

stdenv.mkDerivation {
  name = "lightning-image-viewer";
  src = builtins.fetchGit {
    url = ./.;
  };
  buildInputs = [ sdl3 sdl3_image ];
  buildPhase = ''

mkdir -p $out/bin
gcc -lSDL3 -lSDL3_image -lm src/viewer.c -o $out/bin/lightning-image-viewer

  '';
  installPhase = ''

cp -R share $out

  '';
}
