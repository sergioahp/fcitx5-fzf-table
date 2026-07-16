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
- Five IME definitions: `share/fcitx5/inputmethod/fzf-*.conf`
- Table payloads installed with the addon package
- A fcitx-free search/session core under `libs/fzftable/` with gtests

## Runtime behavior

- Activating the IME opens fuzzy-search mode for that table.
- Typed text becomes the query buffer.
- Query syntax is the same parser already implemented in `libfzfmatch`:
  fuzzy, exact (`'foo`), prefix (`^foo`), suffix (`foo$`), equal (`=foo`),
  inverse (`!foo`), AND by spaces, OR by `|`.
- Candidate window shows the top `PageSize` hits.
- `Tab` commits the highlighted candidate.
- Global prev/next candidate and page keys from fcitx still work.
- `BackSpace` edits the query buffer.
- `Escape` clears the current search UI.
- `fzf-typst` uses the same transparent `\` trigger as `fzf-latex` for
  symbol lookup. While idle, `Ctrl+Tab` / `Ctrl+Shift+Tab` cycle styled typing
  modes and `Ctrl+Alt+0..8` selects normal, bold, italic, bold italic, cal,
  bold cal, bb, frak, or bold frak directly.
- In a Typst style mode, ordinary typed characters are committed immediately in
  that Unicode mathematical style. Characters unavailable in the style fall
  back to the original typed character.

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

- Kaomoji and IPA still use seed tables.
- Golden tests against upstream `fzf`.
- End-to-end VM/UI automation for candidate window behavior.
