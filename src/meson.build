subdir('model')
subdir('view')

app_sources = files('main.c')

executable('pdf-chapter-splitter',
  app_sources,
  dependencies : [gtk_dep, poppler_dep, view_dep],
  install : false)
