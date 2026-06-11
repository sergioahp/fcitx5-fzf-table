{ runCommand, python3, fetchurl }:

let
  # Pinned snapshot of fcitx/fcitx5-table-other's latex.txt. Update by
  # rerunning `nix store prefetch-file --json --hash-type sha256 <url>`.
  upstreamRev = "master";

  latexTxt = fetchurl {
    url = "https://raw.githubusercontent.com/fcitx/fcitx5-table-other/${upstreamRev}/tables/other/latex.txt";
    hash = "sha256-rFx1nlukxrCLJCmIoyGzWh/Xr+iYYNuYTsEfQW1SdOQ=";
  };
in
runCommand "fzftable-latex" {
  nativeBuildInputs = [ python3 ];
  passthru = { inherit upstreamRev; };
} ''
  mkdir -p $out
  python3 ${../../data/latex/build.py} \
    --source ${latexTxt} \
    --out $out/latex.tab
''
