{ stdenv, lib, meson, ninja, pkg-config, gtest, fcitx5, wl-clipboard,
  tablesEmoji, tablesKaomoji, tablesLatex, tablesIpa, tablesTypst }:

stdenv.mkDerivation {
  pname = "fcitx5-fzf-table";
  version = "0.1.0";

  src = lib.fileset.toSource {
    root = ../..;
    fileset = lib.fileset.unions [
      ../../meson.build
      ../../meson_options.txt
      ../../libs/fzfmatch
      ../../libs/fzftable
      ../../bin/fzftable
      ../../addons/fcitx5-fzf-table
      ../../addons/fcitx5-fzf-picker
      ../../addons/fcitx5-fzf-clipboard
    ];
  };

  # wl-clipboard is both a build-time dep (meson find_program at configure
  # time pins wl-paste's store path into the addon binary) and a runtime
  # dep (the addon execs it). Adding it to buildInputs covers both.
  nativeBuildInputs = [ meson ninja pkg-config ];
  buildInputs = [ gtest fcitx5 wl-clipboard ];

  mesonFlags = [
    "-Dtests=true"
    "-Dfcitx5_addon=true"
  ];
  doCheck = true;

  postInstall = ''
    install -Dm644 ${tablesEmoji}/emoji.tab     $out/share/fcitx5/fzf-table/emoji.tab
    install -Dm644 ${tablesKaomoji}/kaomoji.tab $out/share/fcitx5/fzf-table/kaomoji.tab
    install -Dm644 ${tablesLatex}/latex.tab     $out/share/fcitx5/fzf-table/latex.tab
    install -Dm644 ${tablesIpa}/ipa.tab         $out/share/fcitx5/fzf-table/ipa.tab
    install -Dm644 ${tablesTypst}/typst.tab     $out/share/fcitx5/fzf-table/typst.tab
  '';

  meta = with lib; {
    description = "fcitx5 fuzzy table input methods backed by libfzfmatch";
    license = licenses.mit;
    platforms = platforms.linux;
  };
}
