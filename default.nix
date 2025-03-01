with (import <nixpkgs> { });

# TODO remove this once SDL3_image is in nixpkgs
with rec {
  sdl3_image = SDL2_image.overrideAttrs (finalAttrs: previousAttrs: {
    pname = "sdl3_image";
    version = "3.2.0";
    src = fetchFromGitHub {
      owner = "libsdl-org";
      repo = "SDL_image";
      tag = "release-${finalAttrs.version}";
      hash = "sha256-2R46H7j/cEygevMY15xeLzTutddeCWO5gqUE9wJfbyk=";
    };
    buildInputs = map ( x: if x == SDL2 then sdl3 else x) previousAttrs.buildInputs ++ [ libwebp libavif ];
    nativeBuildInputs = map ( x: if x == SDL2 then sdl3 else x) previousAttrs.nativeBuildInputs ++ [ cmake ];
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

callPackage ./derivation.nix { sdl3_image = sdl3_image; }
