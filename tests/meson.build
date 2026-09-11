check_dep = dependency('check')

test_pdfdoc = executable('test_pdfdoc',
  files('test_pdfdoc.c'),
  dependencies : [model_dep, check_dep])

test('pdfdoc model tests', test_pdfdoc)

test_app_controller = executable('test_app_controller',
  files('test_app_controller.c'),
  dependencies : [model_dep, controller_dep, check_dep])

test('app controller export tests', test_app_controller)
