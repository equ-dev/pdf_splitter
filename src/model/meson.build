model_sources = files('pdfdoc.c')

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)

model_lib = static_library('model',
  model_sources,
  dependencies : [gtk_dep, poppler_dep, m_dep])

model_dep = declare_dependency(
  link_with : model_lib,
  include_directories : include_directories('.'),
  dependencies : [gtk_dep, poppler_dep, m_dep])
