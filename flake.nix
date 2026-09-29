{
  inputs = {
    nixpkgs-unstable.url = "github:NixOS/nixpkgs/nixos-unstable";
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";

    zephyr = {
      url = "github:zephyrproject-rtos/zephyr/v4.4.0";
      flake = false;
    };

    zephyr-nix = {
      url = "github:nix-community/zephyr-nix";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.zephyr.follows = "zephyr";
    };
  };

  outputs =
    {
      nixpkgs-unstable,
      nixpkgs,
      zephyr-nix,
      ...
    }:
    let
      system = "x86_64-linux";
      pkgs-unstable = nixpkgs-unstable.legacyPackages.${system};
      pkgs = nixpkgs.legacyPackages.${system};
      zephyr = zephyr-nix.packages.${system};
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          # Gateway dependencies
          (zephyr."sdk-1_0_0".override {
            targets = [ "arm-zephyr-eabi" ];
          })
          zephyr.pythonEnv
          zephyr.hosttools

          # Parser dependencies
          gcc

          # Common dependencies
          cmake
          ninja

          # Misc util
          pkgs-unstable.just
        ];
      };
    };
}
