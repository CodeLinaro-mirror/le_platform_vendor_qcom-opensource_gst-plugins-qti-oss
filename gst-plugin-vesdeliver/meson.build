project('gst-plugin-vesdeliver', 'c',
  version : '1.0',
  meson_version : '>= 0.47',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')

gstvesdeliver_sources = [
  'gstvesdeliver.c',
  'gstvesdeliverallocator.c'
]

gstvesdeliver_c_args = ['-DHAVE_CONFIG_H']
gstvesdeliver_link_args = [ ]

configinc = include_directories('.')
core_conf = configuration_data()
core_conf.set_quoted('PACKAGE', 'gst-plugin-vesdeliver')
core_conf.set_quoted('PACKAGE_NAME', 'gst-plugin-vesdeliver')
core_conf.set_quoted('PACKAGE_VERSION', '1.0')
configure_file(output : 'config.h', configuration : core_conf)

gst_dep = dependency('gstreamer-1.0',
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0',
  fallback : ['gstreamer', 'gst_base_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0',
  fallback : ['gst-plugins-base', 'video_dep'])
gstallocators_dep = dependency('gstreamer-allocators-1.0',
  fallback : ['gst-plugins-base', 'allocators_dep'])
glib_dep = dependency('glib-2.0', version: '>= 2.44', required: false)

gstvesdeliver_dep = [glib_dep, gst_dep, gstbase_dep, gstvideo_dep, gstallocators_dep]

use_dmaheap = get_option('usedmaheap')
if use_dmaheap
  gstvesdeliver_c_args += ['-DUSE_DMAHEAP']
endif

gstvesdeliver = library('gstvesdeliver',
  gstvesdeliver_sources,
  c_args : [gstvesdeliver_c_args, '-Wall'],
  link_args : [gstvesdeliver_link_args, '-ldl'],
  include_directories : [configinc],
  dependencies : gstvesdeliver_dep,
  install : true,
  install_dir : plugins_install_dir,
)
