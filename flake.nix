{
  description = "ZXC - Lossless compression C library built for ultra-fast decode.";

  inputs.nixpkgs.url = "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.zst";

  outputs =
    { self, nixpkgs, ... }:
    let
      supportedPackages = nixpkgs.lib.filterAttrs (
        system: _: system != "x86_64-darwin"
      ) nixpkgs.legacyPackages;
    in
    {
      packages = builtins.mapAttrs (system: pkgs: {
        zxc = pkgs.callPackage ./package.nix { };

        default = self.packages.${system}.zxc;
      }) supportedPackages;

      apps = builtins.mapAttrs (system: _: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/zxc";
        };
      }) supportedPackages;
    };
}
