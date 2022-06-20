qvdeinterlace_sources = [
  'gstqvdeinterlace.c',
  'gstqvdeinpool.c',
  'gpudeinterlace.c',
  'qvdeindmabuf.c',
]

gbm_dep = dependency('gbm')

gstqvdeinterlace = library('gstqvdeinterlace',
  qvdeinterlace_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, gstallocators_dep, gbm_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstqvdeinterlace, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstqvdeinterlace]
