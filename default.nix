with (import <nixpkgs> { });

# TODO remove this once SDL3 is in nixpkgs
with rec {
  SDL3 = callPackage ./sdl3.nix { };
  SDL3_image = SDL2_image.overrideAttrs (finalAttrs: previousAttrs: {
    pname = "sdl3_image";
    version = "3.1.0";
    src = fetchFromGitHub {
      owner = "libsdl-org";
      repo = "SDL_image";
      tag = "preview-${finalAttrs.version}";
      hash = "sha256-awLiDNJwrciF5wfAuawb4xJFc1y/rhVX5iv2U1CWU/8=";
    };
    buildInputs = map ( x: if x == SDL2 then SDL3 else x) previousAttrs.buildInputs;
    nativeBuildInputs = previousAttrs.nativeBuildInputs ++ [ cmake ];
    postInstall = ''
      sed -i 's\//nix/store/\/nix/store\' ''${out}/lib/pkgconfig/sdl3-image.pc
    '';
  });
};

callPackage ./derivation.nix { SDL3 = SDL3; SDL3_image = SDL3_image; }
