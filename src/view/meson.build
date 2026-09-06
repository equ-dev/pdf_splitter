view_sources = files('mainwindow.c')

view_lib = static_library('view',
  view_sources,
  dependencies : [gtk_dep])

view_dep = declare_dependency(
  link_with : view_lib,
  include_directories : include_directories('.'),
  dependencies : [gtk_dep])
