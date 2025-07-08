{ stdenv, sdl3, sdl3-image, libexif, libheif }:

stdenv.mkDerivation {
  name = "lightning-image-viewer";
  src = builtins.fetchGit {
    url = ./.;
  };
  buildInputs = [ sdl3 sdl3-image libexif libheif ];
  makeFlags = [ "PREFIX=$(out)" ];
}
