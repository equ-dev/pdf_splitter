check_dep = dependency('check')

test_pdfdoc = executable('test_pdfdoc',
  files('test_pdfdoc.c'),
  dependencies : [model_dep, check_dep])

test('pdfdoc model tests', test_pdfdoc)
