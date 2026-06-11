{ runCommand, python3, fetchurl }:

let
  cldrRev = "release-48";
  emojiVersion = "17.0.0";

  cldrAnnotations = fetchurl {
    url = "https://raw.githubusercontent.com/unicode-org/cldr/${cldrRev}/common/annotations/en.xml";
    hash = "sha256-hRGq3QRv26Lw/+WQJmzti79IF1rRObJnXYXXFBBXsjU=";
  };

  cldrAnnotationsDerived = fetchurl {
    url = "https://raw.githubusercontent.com/unicode-org/cldr/${cldrRev}/common/annotationsDerived/en.xml";
    hash = "sha256-12vQQcjJ57ALcWr/i32dv1CYdwEOGR1e/QaL4lU+Bm4=";
  };

  emojiTest = fetchurl {
    url = "https://www.unicode.org/Public/${emojiVersion}/emoji/emoji-test.txt";
    hash = "sha256-HYqUT4jXlS9+98UWf+88Z5lbyuJFQ5SXECMbA6IBrNo=";
  };
in
runCommand "fzftable-emoji" {
  nativeBuildInputs = [ python3 ];
  passthru = { inherit cldrRev emojiVersion; };
} ''
  mkdir -p $out
  python3 ${../../data/emoji/build.py} \
    --annotations ${cldrAnnotations} \
    --derived ${cldrAnnotationsDerived} \
    --emoji-test ${emojiTest} \
    --out $out/emoji.tab
''
