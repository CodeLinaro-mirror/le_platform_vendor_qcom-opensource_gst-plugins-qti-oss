gstqticodec2vdec_sources = [
  'gstqticodec2vdec.c',
  'gstqcodec2h264dec.c',
  'gstqcodec2h265dec.c',
  'gstqcodec2vp9dec.c',
  'gstqcodec2mpeg2dec.c'
]

gstqticodec2vdec = library('gstqticodec2vdec',
  gstqticodec2vdec_sources,
  c_args : gst_plugin_codec2_args,
  include_directories: [configinc, include_directories('../wrapper/include')],
  dependencies : [codec2wrapper_dep, gstqcodec2common_dep, gst_dep, gstvideo_dep, gstallocators_dep, dl_dep, gstcodecparsers_dep],
  install : true,
  install_dir : plugins_install_dir,
)
