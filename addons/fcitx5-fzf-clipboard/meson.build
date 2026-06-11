fcitx5_core_dep   = dependency('Fcitx5Core')
fcitx5_module_dep = dependency('Fcitx5Module')

clipboard_conf_data = configuration_data()
clipboard_conf_data.set('PROJECT_VERSION', meson.project_version())

# Path to wl-paste used by the listener subprocess. Pinned at configure
# time so the addon doesn't have to look it up at runtime.
wl_clipboard_dep = find_program('wl-paste', required : true)
clipboard_conf_data.set('WL_PASTE_PATH', wl_clipboard_dep.full_path())

fzfclipboard_cpp_args = [
  '-DWL_PASTE_PATH="' + wl_clipboard_dep.full_path() + '"',
]

fzfclipboard_addon = shared_library(
  'fzfclipboard',
  sources : [
    'fzfclipboard.cpp',
  ],
  include_directories : [fzfmatch_inc],
  dependencies : [fcitx5_core_dep, fcitx5_module_dep, fzfmatch_dep],
  cpp_args : fzfclipboard_cpp_args,
  install : true,
  install_dir : join_paths(get_option('libdir'), 'fcitx5'),
)

clipboard_conf = configure_file(
  input : 'fzfclipboard.conf.in',
  output : 'fzfclipboard.conf',
  configuration : clipboard_conf_data,
)

install_data(
  clipboard_conf,
  install_dir : join_paths(get_option('datadir'), 'fcitx5', 'addon'),
)
