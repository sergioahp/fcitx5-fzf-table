project('fcitx5-fzf-tables', 'cpp',
  version : '0.1.0',
  license : 'MIT',
  default_options : [
    'cpp_std=c++23',
    'warning_level=2',
    'b_ndebug=if-release',
  ],
)

subdir('libs/fzfmatch')
subdir('libs/fzftable')
subdir('bin/fzftable')

if get_option('fcitx5_addon')
  subdir('addons/fcitx5-fzf-table')
  subdir('addons/fcitx5-fzf-picker')
  subdir('addons/fcitx5-fzf-clipboard')
endif
