controller_sources = files('app_controller.c')

controller_lib = static_library('controller',
  controller_sources,
  dependencies : [gtk_dep, model_dep, view_dep])

controller_dep = declare_dependency(
  link_with : controller_lib,
  include_directories : include_directories('.'),
  dependencies : [gtk_dep, model_dep, view_dep])
