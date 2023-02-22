gstqcodec2_sources = [
  'plugin.c',
  'common/gstqcodec2bufferpool.c',
  'vdec/gstqcodec2h264dec.c',
  'vdec/gstqcodec2h265dec.c',
  'vdec/gstqcodec2mpeg2dec.c',
  'vdec/gstqcodec2vp9dec.c',
  'vdec/gstqcodec2vdec.c',
  'venc/gstqcodec2h264enc.c',
  'venc/gstqcodec2h265enc.c',
  'venc/gstqcodec2venc.c',
]

# venc depends on libxml-2.0
xml2_dep = dependency('libxml-2.0', version : '>= 2.8')

# compile codec2 wrapper as another lib
subdir('wrapper')

gstqcodec2 = library('gstqcodec2',
  gstqcodec2_sources,
  c_args : gst_plugin_codec2_args,
  include_directories: [configinc, 'vdec', 'venc', 'common', 'wrapper/include'],
  dependencies : [codec2wrapper_dep, gst_dep, gstvideo_dep, gstallocators_dep,
                  dl_dep, gstcodecparsers_dep, xml2_dep],
  install : true,
  install_dir : plugins_install_dir,
)
