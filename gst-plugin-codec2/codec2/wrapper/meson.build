codec2wrapper_sources = [
  'C2ComponentAdapter.cpp',
  'C2ComponentInterfaceAdapter.cpp',
  'C2ComponentStoreAdapter.cpp',
  'C2WrapperUtils.cpp',
  'codec2wrapper.cpp',
]

codec2wrapper_inc = include_directories('include')
codec2_vndk_dep = cxx.find_library('codec2_vndk')
thread_dep = dependency('threads')

codec2wrapper_args = ['-D_AGL_LINUX_' , '-DG_LOG_DOMAIN="codec2wrapper"' , '-std=c++17']
codec2wrapper_args += color_fmt_args
use_dmaheap = get_option('usedmaheap')
if use_dmaheap
  codec2wrapper_args += ['-DUSE_DMAHEAP']
endif

codec2wrapper = library('codec2wrapper',
  codec2wrapper_sources,
  cpp_args : codec2wrapper_args,
  include_directories: [codec2wrapper_inc],
  dependencies : [glib_dep, gstvideo_dep, gst_dep, codec2_vndk_dep, dl_dep, thread_dep],
  install : true,
)

codec2wrapper_dep = declare_dependency(link_with : codec2wrapper, include_directories : codec2wrapper_inc)
