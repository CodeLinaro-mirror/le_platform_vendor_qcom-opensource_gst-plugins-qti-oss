gstqcodec2common_sources = [
  'gstqcodec2bufferpool.c'
]

gstqcodec2common = library('gstqcodec2common',
  gstqcodec2common_sources,
  c_args : gst_plugin_codec2_args,
  include_directories: [configinc, include_directories('../wrapper/include')],
  dependencies : [codec2wrapper_dep, gst_dep, gstvideo_dep, gstallocators_dep],
  install : true,
)

gstqcodec2common_inc = include_directories('.')
gstqcodec2common_dep = declare_dependency(link_with : gstqcodec2common, include_directories : gstqcodec2common_inc)
