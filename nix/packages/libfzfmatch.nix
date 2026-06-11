{ stdenv, lib, meson, ninja, pkg-config, gtest }:

stdenv.mkDerivation {
  pname = "libfzfmatch";
  version = "0.1.0";

  src = lib.fileset.toSource {
    root = ../..;
    fileset = lib.fileset.unions [
      ../../meson.build
      ../../meson_options.txt
      ../../libs/fzfmatch
      ../../libs/fzftable
      ../../bin/fzftable
    ];
  };

  nativeBuildInputs = [ meson ninja pkg-config ];
  buildInputs = [ gtest ];

  mesonFlags = [ "-Dtests=true" ];
  doCheck = true;

  meta = with lib; {
    description = "fzf-style fuzzy matcher port, embeddable C++23 library";
    license     = licenses.mit;
    platforms   = platforms.linux;
  };
}
