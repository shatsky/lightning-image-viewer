{
  description = "Fast and lightweight desktop image (pre)viewer featuring unusual minimalistic \"transparent fullscreen overlay\" UI/UX with controls similar to map apps";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let pkgs = nixpkgs.legacyPackages.${system}; in
      {
        packages = rec {
          lightning-image-viewer = pkgs.callPackage ./derivation.nix {};
          default = lightning-image-viewer;
        };
      }
    );
}
