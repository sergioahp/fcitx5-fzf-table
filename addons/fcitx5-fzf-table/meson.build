fcitx5_core_dep = dependency('Fcitx5Core')

addon_conf_data = configuration_data()
addon_conf_data.set('PROJECT_VERSION', meson.project_version())

fzftable_addon = shared_library(
  'fzftable',
  sources : [
    'fzftable.cpp',
  ],
  include_directories : [fzftable_inc, fzfmatch_inc],
  dependencies : [fcitx5_core_dep, fzftable_dep, fzfmatch_dep],
  install : true,
  install_dir : join_paths(get_option('libdir'), 'fcitx5'),
)

addon_conf = configure_file(
  input : 'fzftable.conf.in',
  output : 'fzftable.conf',
  configuration : addon_conf_data,
)

install_data(
  addon_conf,
  install_dir : join_paths(get_option('datadir'), 'fcitx5', 'addon'),
)

install_data(
  files(
    'inputmethod/fzf-emoji.conf',
    'inputmethod/fzf-kaomoji.conf',
    'inputmethod/fzf-latex.conf',
    'inputmethod/fzf-ipa.conf',
    'inputmethod/fzf-typst.conf',
  ),
  install_dir : join_paths(get_option('datadir'), 'fcitx5', 'inputmethod'),
)

# Table .tab files are no longer installed by meson. The Nix derivation
# (nix/packages/fcitx5-fzf-table.nix) drops them into share/fcitx5/fzf-table
# in postInstall: emoji from the CLDR-driven derivation, the rest still from
# data/<name>/seed.tab pending their own pipelines.
