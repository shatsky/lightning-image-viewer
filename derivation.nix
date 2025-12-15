# cargo libheif-sys uses pkg-config to find libheif
{ stdenv, sdl3, libheif, cargo, pkg-config }:

stdenv.mkDerivation {
  name = "lightning-image-viewer";
  src = builtins.fetchGit {
    url = ./.;
  };
  buildInputs = [ sdl3 libheif cargo pkg-config ];
  makeFlags = [ "PREFIX=$(out)" ];
}
