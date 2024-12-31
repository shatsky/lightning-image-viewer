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
    buildInputs = map ( x: if x == SDL2 then SDL3 else x) previousAttrs.buildInputs ++ [ libwebp libavif ];
    nativeBuildInputs = previousAttrs.nativeBuildInputs ++ [ cmake ];
    # TODO by default SDL3_image uses dynamic loading for deps like libwebp
    # in Nix env they are successfully found when building it with cmake, but later in runtime SDL3_image fails to find them
    # autoPatchelfHook and explicit patchelf --add-rpath in postFixup don't help even though readelf reports paths are added to RUNPATH
    # for now, let's just fall back to dynamic linking
    cmakeFlags = [ "-DSDLIMAGE_DEPS_SHARED=OFF" ];
    postInstall = ''
      sed -i 's\//nix/store/\/nix/store\' ''${out}/lib/pkgconfig/sdl3-image.pc
    '';
  });
};

callPackage ./derivation.nix { SDL3 = SDL3; SDL3_image = SDL3_image; }
