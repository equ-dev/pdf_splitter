subdir('model')

app_sources = files('main.c')

executable('pdf-chapter-splitter',
  app_sources,
  dependencies : [gtk_dep, poppler_dep],
  install : false)
