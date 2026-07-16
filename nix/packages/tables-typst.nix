{ runCommand, python3, fetchFromGitHub, fetchurl }:

let
  upstreamRev = "6e97668c9f2ffea09f3187c34b7641038370fd21";
  emojiVersion = "17.0.0";

  typstSymbols = fetchFromGitHub {
    owner = "jgm";
    repo = "typst-symbols";
    rev = upstreamRev;
    hash = "sha256-C43CXTVEmm9d9gIWixm1LxcdSzLd4orc4OW3u0Snp7o=";
  };

  emojiTest = fetchurl {
    url = "https://www.unicode.org/Public/${emojiVersion}/emoji/emoji-test.txt";
    hash = "sha256-HYqUT4jXlS9+98UWf+88Z5lbyuJFQ5SXECMbA6IBrNo=";
  };
in
runCommand "fzftable-typst" {
  nativeBuildInputs = [ python3 ];
  passthru = { inherit upstreamRev emojiVersion; };
} ''
  mkdir -p $out
  python3 ${../../data/typst/build.py} \
    --source ${typstSymbols}/src/Typst/Symbols.hs \
    --emoji-test ${emojiTest} \
    --out $out/typst.tab
''
