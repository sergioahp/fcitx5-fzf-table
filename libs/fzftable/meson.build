fzftable_inc = include_directories('include')

fzftable_lib = static_library(
  'fzftablecore',
  sources : [
    'src/engine.cpp',
  ],
  include_directories : [fzftable_inc, fzfmatch_inc],
  dependencies : [fzfmatch_dep],
  install : true,
)

fzftable_dep = declare_dependency(
  link_with : fzftable_lib,
  include_directories : [fzftable_inc, fzfmatch_inc],
  dependencies : [fzfmatch_dep],
)

install_subdir('include/fzftable', install_dir : get_option('includedir'))

if get_option('tests') and gtest_dep.found()
  exe = executable(
    'fzftablecore_table_test',
    'tests/table_test.cpp',
    dependencies : [fzftable_dep, gtest_dep],
  )
  test('fzftable_table', exe, protocol : 'gtest')
endif
