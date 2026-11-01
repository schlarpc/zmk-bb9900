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

    # Renders the keymap with keymap-drawer. Run from the repository root.
    drawKeymap = pkgs: pkgs.writeShellApplication {
      name = "draw-keymap";
      runtimeInputs = [ pkgs.keymap-drawer ];
      text = ''
        cfg=config/keymap_drawer.yaml
        parsed="$(mktemp)"
        trap 'rm -f "$parsed"' EXIT
        keymap -c "$cfg" parse -z config/bb9900.keymap > "$parsed"
        keymap -c "$cfg" draw \
          --dts-layout boards/zitaotech/bb9900/bb9900-layouts.dtsi \
          -s BASE SYM UPPER -- "$parsed"
      '';
    };
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

      # Regenerates docs/keymap.svg from the keymap.
      draw = let pkgs = nixpkgs.legacyPackages.${system}; in pkgs.writeShellApplication {
        name = "draw";
        runtimeInputs = [ pkgs.git (drawKeymap pkgs) ];
        text = ''
          cd "$(git rev-parse --show-toplevel)"
          draw-keymap > docs/keymap.svg
          echo "Wrote docs/keymap.svg"
        '';
      };
    });

    checks = forAllSystems (system: let pkgs = nixpkgs.legacyPackages.${system}; in {
      # Fails when docs/keymap.svg is out of date; fix with `nix run .#draw`.
      keymap-image = pkgs.runCommand "keymap-image-check" {
        nativeBuildInputs = [ (drawKeymap pkgs) ];
      } ''
        cp -r --no-preserve=mode ${lib.fileset.toSource {
          root = ./.;
          fileset = lib.fileset.unions [ ./config ./boards ./docs/keymap.svg ];
        }} src
        cd src
        draw-keymap > expected.svg
        if ! cmp -s expected.svg docs/keymap.svg; then
          echo "docs/keymap.svg is out of date with the keymap; run: nix run .#draw" >&2
          exit 1
        fi
        touch $out
      '';
    });

    devShells = forAllSystems (system: {
      default = zmk-nix.devShells.${system}.default;
    });
  };
}
