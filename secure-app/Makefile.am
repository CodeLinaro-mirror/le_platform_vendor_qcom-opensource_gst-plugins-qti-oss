bin_PROGRAMS=secureappsrc secure-gst-codec2
secureappsrc_SOURCES = secureappsrc.c crypto.c

secureappsrc_LDFLAGS = -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0 -lion -ldl -lgstapp-1.0 -pthread
secureappsrc_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
  crypto.h

secure_gst_codec2_SOURCES = secure-gst-codec2.c crypto.c

secure_gst_codec2_LDFLAGS = -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0 -lion -ldl
secure_gst_codec2_LIBTOOLFLAGS = --tag=disable-static
