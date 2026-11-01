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
    packages = forAllSystems (system: let
      buildFirmware = args: zmk-nix.legacyPackages.${system}.buildKeyboard ({
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

        # Live keymap editing over USB and BLE: https://zmk.studio
        enableZmkStudio = true;

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
      } // args);
    in rec {
      default = firmware;

      firmware = buildFirmware {
        name = "bb9900-firmware";
      };

      # Logs over USB serial (/dev/ttyACM*), with debug output from the trackpad driver.
      firmware-debug = buildFirmware {
        name = "bb9900-firmware-debug";
        inherit (firmware) westDeps;
        snippets = [ "zmk-usb-logging" ];
        extraCmakeFlags = [
          "-DCONFIG_INPUT_LOG_LEVEL_DBG=y"
          "-DCONFIG_LOG_BUFFER_SIZE=8192"
          "-DCONFIG_ZMK_BB9900_SLEEP_DIAG=y"
        ];
      };

      flash = zmk-nix.packages.${system}.flash.override { inherit firmware; };
      flash-debug = zmk-nix.packages.${system}.flash.override { firmware = firmware-debug; };
      update = zmk-nix.packages.${system}.update;
    });

    devShells = forAllSystems (system: {
      default = zmk-nix.devShells.${system}.default;
    });
  };
}
