{ stdenv, SDL3, SDL3_image }:

stdenv.mkDerivation {
  name = "lightning-image-viewer";
  src = builtins.fetchGit {
    url = ./.;
  };
  nativeBuildInputs = [ ];
  buildInputs = [ SDL3 SDL3_image ];
  buildPhase = ''

mkdir -p $out/bin
gcc -lSDL3 -lSDL3_image -lm src/viewer.c -o $out/bin/lightning-image-viewer

  '';
  installPhase = ''

cp -R share $out

  '';
}
