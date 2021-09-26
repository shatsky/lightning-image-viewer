{ stdenv, SDL2, SDL2_image }:

with {
  SDL2_patched = SDL2.overrideAttrs (attrs: {
    postPatch = attrs.postPatch + ''

#sed -i "99i\    memset(data->bitmap,0,data->bitmapsize);" src/video/x11/SDL_x11shape.c
echo "diff -ru3 SDL2-2.0.14/src/video/x11/SDL_x11shape.c SDL2-2.0.14-new/src/video/x11/SDL_x11shape.c 
--- SDL2-2.0.14/src/video/x11/SDL_x11shape.c    2020-12-21 19:44:36.000000000 +0200
+++ SDL2-2.0.14-new/src/video/x11/SDL_x11shape.c        2021-09-25 14:46:02.504904675 +0300
@@ -96,6 +96,7 @@
         return -3;
     data = shaper->driverdata;
 
+    memset(data->bitmap,0,data->bitmapsize);
     /* Assume that shaper->alphacutoff already has a value, because SDL_SetWindowShape() should have given it one. */
     SDL_CalculateShapeBitmap(shaper->mode,shape,data->bitmap,8);"|patch -p1

    '';
  });
};
stdenv.mkDerivation {
  name = "lightning-image-viewer";
  src = builtins.fetchGit {
    url = ./.;
  };
  nativeBuildInputs = [ ];
  buildInputs = [ SDL2_patched SDL2_image ];
  buildPhase = ''

mkdir -p $out/bin
gcc -I${SDL2_patched.dev}/include/SDL2 -I${SDL2_image}/include/SDL2 -lSDL2 -lSDL2_image -lm src/viewer.c -o $out/bin/lightning-image-viewer

  '';
  installPhase = ''

cp -R share $out

  '';
}
