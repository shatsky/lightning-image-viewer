{ stdenv, sdl3, sdl3-image, libexif }:

stdenv.mkDerivation {
  name = "lightning-image-viewer";
  src = builtins.fetchGit {
    url = ./.;
  };
  buildInputs = [ sdl3 sdl3-image libexif ];
  makeFlags = [ "PREFIX=$(out)" ];
}
