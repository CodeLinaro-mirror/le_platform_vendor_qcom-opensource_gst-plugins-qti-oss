project('gst-plugin-qvdeinterlace', 'c',
  version : '1.0',
  meson_version : '>= 0.47',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')

gstqvdeinterlace_sources = [
  'gstqvdeinterlace.c',
  'gstqvdeinpool.c',
  'gpudeinterlace.c',
  'qvdeindmabuf.c',
]

gstqvdeinterlace_c_args = ['-DHAVE_CONFIG_H']
gstqvdeinterlace_link_args = [ ]

# comment out below 5 lines for testing on Ubuntu
gstqvdeinterlace_c_args += ['-DQTI_PLATFORM']
gstqvdeinterlace_c_args += ['-DUSE_GBM']
gstqvdeinterlace_link_args += ['-lgbm']
# comment out below 2 lines for testing without GPU deinterlace
#gstqvdeinterlace_c_args += ['-DUSE_GPU_DEINTERLACE']
#gstqvdeinterlace_link_args += ['-L.', '-lgpudi']

configinc = include_directories('.')
core_conf = configuration_data()
core_conf.set_quoted('PACKAGE', 'gst-plugin-qvdeinterlace')
core_conf.set_quoted('PACKAGE_NAME', 'gst-plugin-qvdeinterlace')
core_conf.set_quoted('PACKAGE_VERSION', '1.0')
configure_file(output : 'config.h', configuration : core_conf)

#gbm_dep = dependency('gbm')
#gpudi_dep = dependency('gpudi')

gst_dep = dependency('gstreamer-1.0',
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0',
  fallback : ['gstreamer', 'gst_base_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0',
  fallback : ['gst-plugins-base', 'video_dep'])
gstallocators_dep = dependency('gstreamer-allocators-1.0',
  fallback : ['gst-plugins-base', 'allocators_dep'])
glib_dep = dependency('glib-2.0', version: '>= 2.44', required: false)

gstqvdeinterlace = library('gstqvdeinterlace',
  gstqvdeinterlace_sources,
  c_args : [gstqvdeinterlace_c_args, '-Wall'],
  link_args : [gstqvdeinterlace_link_args],
  include_directories : [configinc],
  dependencies : [glib_dep, gst_dep, gstbase_dep, gstvideo_dep, gstallocators_dep],
  install : true,
  install_dir : plugins_install_dir,
)
