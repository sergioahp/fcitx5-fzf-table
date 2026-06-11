{
  description = "fcitx5 fuzzy table input methods + libfzfmatch (fzf algorithm port)";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };

      libfzfmatch = pkgs.callPackage ./nix/packages/libfzfmatch.nix { };

      mkTable = name: pkgs.runCommand "fzftable-${name}-seed" { } ''
        mkdir -p $out
        cp ${./data}/${name}/seed.tab $out/${name}.tab
      '';

      tablesEmoji   = pkgs.callPackage ./nix/packages/tables-emoji.nix { };
      tablesKaomoji = mkTable "kaomoji";
      tablesLatex   = pkgs.callPackage ./nix/packages/tables-latex.nix { };
      tablesIpa     = mkTable "ipa";
      tablesTypst   = pkgs.callPackage ./nix/packages/tables-typst.nix { };

      fcitx5FzfTable = pkgs.callPackage ./nix/packages/fcitx5-fzf-table.nix {
        inherit tablesEmoji tablesKaomoji tablesLatex tablesIpa tablesTypst;
      };
    in
    {
      packages.${system} = {
        # The fuzzy-match library plus the fzftable CLI live in one derivation.
        inherit libfzfmatch;
        fzftable = libfzfmatch;
        fcitx5-fzf-table = fcitx5FzfTable;

        # Convenience package for local testing with stock fcitx5.
        fcitx5-with-fzf-table = pkgs.qt6Packages.fcitx5-with-addons.override {
          addons = with pkgs; [
            fcitx5-mozc
            fcitx5-gtk
            kdePackages.fcitx5-qt
            fcitx5FzfTable
          ];
        };

        # Emoji, LaTeX, and Typst tables are generated from upstream sources.
        # Kaomoji and IPA are still seed copies pending their own pipelines.
        tables-emoji   = tablesEmoji;
        tables-kaomoji = tablesKaomoji;
        tables-latex   = tablesLatex;
        tables-ipa     = tablesIpa;
        tables-typst   = tablesTypst;

        default = fcitx5FzfTable;
      };

      apps.${system} = {
        fzftable = {
          type    = "app";
          program = "${libfzfmatch}/bin/fzftable";
        };
        # `nix run .#emoji -- --query='cat'` style helpers.
        emoji = {
          type    = "app";
          program = toString (pkgs.writeShellScript "fzftable-emoji" ''
            exec ${libfzfmatch}/bin/fzftable \
              --table=${self.packages.${system}.tables-emoji}/emoji.tab "$@"
          '');
        };
        kaomoji = {
          type    = "app";
          program = toString (pkgs.writeShellScript "fzftable-kao" ''
            exec ${libfzfmatch}/bin/fzftable \
              --table=${self.packages.${system}.tables-kaomoji}/kaomoji.tab "$@"
          '');
        };
        latex = {
          type    = "app";
          program = toString (pkgs.writeShellScript "fzftable-latex" ''
            exec ${libfzfmatch}/bin/fzftable \
              --table=${self.packages.${system}.tables-latex}/latex.tab "$@"
          '');
        };
        ipa = {
          type    = "app";
          program = toString (pkgs.writeShellScript "fzftable-ipa" ''
            exec ${libfzfmatch}/bin/fzftable \
              --table=${self.packages.${system}.tables-ipa}/ipa.tab "$@"
          '');
        };
        typst = {
          type    = "app";
          program = toString (pkgs.writeShellScript "fzftable-typst" ''
            exec ${libfzfmatch}/bin/fzftable \
              --table=${self.packages.${system}.tables-typst}/typst.tab "$@"
          '');
        };
      };

      checks.${system} = {
        # Meson runs the gtest suite as part of `doCheck = true`, so the
        # library derivation itself is a passing check.
        unit = libfzfmatch;
        fcitx5-fzf-table = fcitx5FzfTable;
      };

      devShells.${system}.default = pkgs.mkShell {
        nativeBuildInputs = with pkgs; [
          meson ninja pkg-config gcc14 clang-tools
        ];
        buildInputs = with pkgs; [
          gtest
          fcitx5
        ];
        shellHook = ''
          echo "fcitx5-fzf-tables devshell"
          echo "  meson setup build -Dfcitx5_addon=true && meson test -C build"
          echo "  meson compile -C build && ./build/bin/fzftable/fzftable \\"
          echo "      --table=data/kaomoji/seed.tab --query='cry'"
          echo "  # for the full CLDR emoji table:"
          echo "  nix run .#emoji -- --query='cat'"
        '';
      };
    };
}
