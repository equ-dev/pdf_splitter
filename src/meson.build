subdir('model')
subdir('view')
subdir('controller')

app_sources = files('main.c')

executable('pdf-chapter-splitter',
  app_sources,
  dependencies : [gtk_dep, poppler_dep, view_dep, controller_dep],
  install : false)
