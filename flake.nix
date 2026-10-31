{
  description = "ZMK firmware for the ZitaoTech BB9900 USB/BLE keyboard";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

    zmk-nix = {
      url = "github:lilyinstarlight/zmk-nix";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, zmk-nix }: let
    inherit (nixpkgs) lib;
    forAllSystems = lib.genAttrs (lib.attrNames zmk-nix.packages);
  in {
    packages = forAllSystems (system: rec {
      default = firmware;

      firmware = zmk-nix.legacyPackages.${system}.buildKeyboard {
        name = "bb9900-firmware";

        src = lib.fileset.toSource {
          root = ./.;
          fileset = lib.fileset.unions [
            ./CMakeLists.txt
            ./Kconfig
            ./boards
            ./config
            ./drivers
            ./dts
            ./src
            ./zephyr
          ];
        };

        board = "bb9900//zmk";

        # Keep the resolved devicetree and Kconfig next to the firmware for debugging.
        postInstall = ''
          mkdir -p $out/debug
          cp zephyr/zephyr.dts zephyr/.config $out/debug/
        '';

        zephyrDepsHash = "sha256-zDnYk7iavZLcuPRrl8QUMROnvikwBGOrDa5prc2cFtk=";

        meta = {
          description = "ZMK firmware for the BB9900 keyboard";
          license = lib.licenses.mit;
          platforms = lib.platforms.all;
        };
      };

      flash = zmk-nix.packages.${system}.flash.override { inherit firmware; };
      update = zmk-nix.packages.${system}.update;
    });

    devShells = forAllSystems (system: {
      default = zmk-nix.devShells.${system}.default;
    });
  };
}
