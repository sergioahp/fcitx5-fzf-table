fcitx5_core_dep = dependency('Fcitx5Core')
fcitx5_module_dep = dependency('Fcitx5Module')

picker_conf_data = configuration_data()
picker_conf_data.set('PROJECT_VERSION', meson.project_version())

fzfpicker_addon = shared_library(
  'fzfpicker',
  sources : [
    'fzfpicker.cpp',
  ],
  include_directories : [fzftable_inc, fzfmatch_inc],
  dependencies : [fcitx5_core_dep, fcitx5_module_dep, fzftable_dep, fzfmatch_dep],
  install : true,
  install_dir : join_paths(get_option('libdir'), 'fcitx5'),
)

picker_conf = configure_file(
  input : 'fzfpicker.conf.in',
  output : 'fzfpicker.conf',
  configuration : picker_conf_data,
)

install_data(
  picker_conf,
  install_dir : join_paths(get_option('datadir'), 'fcitx5', 'addon'),
)
