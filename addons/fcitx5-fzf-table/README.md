# fcitx5-fzf-table

An fcitx5 input-method addon that exposes five IMEs:

- `fzf-emoji`
- `fzf-kaomoji`
- `fzf-latex`
- `fzf-ipa`
- `fzf-typst`

Each IME loads a `.tab` file from `share/fcitx5/fzf-table/<name>.tab`,
parses it through the standalone `fzftablecore` library, and ranks matches
with `libfzfmatch`.

## What is implemented

- One shared-library addon: `lib/fcitx5/libfzftable.so`
- One addon manifest: `share/fcitx5/addon/fzftable.conf`
- Four IME definitions: `share/fcitx5/inputmethod/fzf-*.conf`
- Seed table payloads installed with the addon package
- A fcitx-free search/session core under `libs/fzftable/` with gtests

## Runtime behavior

- Activating the IME opens fuzzy-search mode for that table.
- Typed text becomes the query buffer.
- Query syntax is the same parser already implemented in `libfzfmatch`:
  fuzzy, exact (`'foo`), prefix (`^foo`), suffix (`foo$`), equal (`=foo`),
  inverse (`!foo`), AND by spaces, OR by `|`.
- Candidate window shows the top `PageSize` hits.
- `Enter` commits the highlighted candidate.
- `Alt+1..0` commits a visible candidate directly.
- Global prev/next candidate and page keys from fcitx still work.
- `BackSpace` edits the query buffer.
- `Escape` clears the current search UI.
- `fzf-typst` uses the same transparent `\` trigger as `fzf-latex`, then
  `Tab` / `Shift+Tab` cycle style preference: plain, bold, cal, bb, frak,
  upright.

`Alt+1..0` was chosen instead of plain digits so digits remain available inside
the query language.

## Config

The addon reads `conf/fzftable.conf` with these defaults:

```ini
[Behavior]
PageSize=10
EnableNormalize=True
CaseMode=Smart
```

`CaseMode` accepts `Smart`, `Ignore`, or `Respect`.

## Packaging

`nix build .#fcitx5-fzf-table` produces a package containing:

- `bin/fzftable`
- `lib/fcitx5/libfzftable.so`
- `share/fcitx5/addon/fzftable.conf`
- `share/fcitx5/inputmethod/fzf-*.conf`
- `share/fcitx5/fzf-table/*.tab`

## What is still missing

- M2 real data pipelines instead of seed tables
- M3 golden tests against upstream `fzf`
- M5 clipboard rework on top of the same matcher/session core
- M6 end-to-end VM/UI automation
