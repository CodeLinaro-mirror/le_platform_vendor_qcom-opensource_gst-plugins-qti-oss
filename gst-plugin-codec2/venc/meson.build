gstqticodec2venc_sources = [
  'gstqticodec2venc.c',
  'gstqcodec2h264enc.c',
  'gstqcodec2h265enc.c'
]

xml2_dep = dependency('libxml-2.0', version : '>= 2.8')

gstqticodec2venc = library('gstqticodec2venc',
  gstqticodec2venc_sources,
  c_args : gst_plugin_codec2_args,
  include_directories: [configinc, include_directories('../wrapper/include')],
  dependencies : [codec2wrapper_dep, gstqcodec2common_dep, gst_dep, gstvideo_dep, gstallocators_dep, xml2_dep],
  install : true,
  install_dir : plugins_install_dir,
)
