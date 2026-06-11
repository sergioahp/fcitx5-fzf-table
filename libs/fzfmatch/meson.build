fzfmatch_inc = include_directories('include')

fzfmatch_lib = static_library(
  'fzfmatch',
  sources : [
    'src/chars.cpp',
    'src/normalize.cpp',
    'src/algo.cpp',
    'src/query.cpp',
  ],
  include_directories : fzfmatch_inc,
  install : true,
)

fzfmatch_dep = declare_dependency(
  link_with : fzfmatch_lib,
  include_directories : fzfmatch_inc,
)

install_subdir('include/fzfmatch', install_dir : get_option('includedir'))

gtest_dep = dependency('gtest', main : true, required : get_option('tests'))

if get_option('tests') and gtest_dep.found()
  foreach name : ['chars', 'normalize', 'algo', 'query']
    exe = executable(
      'fzfmatch_' + name + '_test',
      'tests/' + name + '_test.cpp',
      dependencies : [fzfmatch_dep, gtest_dep],
    )
    test(name, exe, protocol : 'gtest')
  endforeach
endif
