# fcitx5-fzf-table

Out-of-tree fcitx5 addons for fuzzy symbol input. The project builds against
stock fcitx5 headers and installs normal fcitx addon files, so users do not
need a custom fcitx build.

## Features

- `fzf-emoji`, backed by CLDR emoji annotations and `emoji-test.txt`.
- `fzf-latex`, generated from `fcitx5-table-other`'s LaTeX table.
- `fzf-typst`, generated from `typst-symbols`.
- `fzf-kaomoji` and `fzf-ipa`, currently backed by seed tables.
- Hotkey-triggered picker addon for emoji, kaomoji, LaTeX, IPA, and Typst.
- Clipboard picker addon backed by `wl-paste`.
- Standalone `fzftable` CLI for testing table ranking outside fcitx.

## Build

```sh
nix build .#fcitx5-fzf-table
```

The package installs:

- `bin/fzftable`
- `lib/fcitx5/libfzftable.so`
- `lib/fcitx5/libfzfpicker.so`
- `lib/fcitx5/libfzfclipboard.so`
- `share/fcitx5/addon/*.conf`
- `share/fcitx5/inputmethod/fzf-*.conf`
- `share/fcitx5/fzf-table/*.tab`

For local testing with a stock fcitx bundle:

```sh
nix build .#fcitx5-with-fzf-table
```

## NixOS

Add the package as an fcitx5 addon, the same way as other out-of-tree input
method packages:

```nix
i18n.inputMethod.fcitx5.addons = with pkgs; [
  fcitx5-mozc
  fcitx5-gtk
  kdePackages.fcitx5-qt
  fcitx5-fzf-table
];
```

When using this flake directly, expose `packages.${system}.fcitx5-fzf-table`
through an overlay or pass it into your home/system config.

## CLI checks

```sh
nix run .#emoji -- --query='cat'
nix run .#latex -- --query='alpha'
nix run .#typst -- --query='arrow'
```

## Layout

- `addons/`: fcitx5 shared-library addons and input method metadata.
- `libs/fzfmatch/`: fzf-style matching engine.
- `libs/fzftable/`: table parser and search session core.
- `data/`: table seed files and source-data builders.
- `nix/packages/`: Nix derivations for the addon and generated tables.
