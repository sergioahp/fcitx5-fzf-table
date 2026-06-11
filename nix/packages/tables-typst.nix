{ runCommand, python3, fetchFromGitHub }:

let
  upstreamRev = "6e97668c9f2ffea09f3187c34b7641038370fd21";

  typstSymbols = fetchFromGitHub {
    owner = "jgm";
    repo = "typst-symbols";
    rev = upstreamRev;
    hash = "sha256-C43CXTVEmm9d9gIWixm1LxcdSzLd4orc4OW3u0Snp7o=";
  };
in
runCommand "fzftable-typst" {
  nativeBuildInputs = [ python3 ];
  passthru = { inherit upstreamRev; };
} ''
  mkdir -p $out
  python3 ${../../data/typst/build.py} \
    --source ${typstSymbols}/src/Typst/Symbols.hs \
    --out $out/typst.tab
''
