[1;33mdiff --cc gst-plugin-base/gst/video/video-converter-engine.h[m
[1;33mindex 7ee5e2ab,5730d9cc..00000000[m
[1;33m--- a/gst-plugin-base/gst/video/video-converter-engine.h[m
[1;33m+++ b/gst-plugin-base/gst/video/video-converter-engine.h[m
[1;33mdiff --cc gst-plugin-codec2/c2-engine/c2-engine-utils.cc[m
[1;33mindex fc1d345f,5c235a4d..00000000[m
[1;33m--- a/gst-plugin-codec2/c2-engine/c2-engine-utils.cc[m
[1;33m+++ b/gst-plugin-codec2/c2-engine/c2-engine-utils.cc[m
[1;33mdiff --cc gst-plugin-qmmfsrc/qmmf_source_context.cc[m
[1;33mindex 854559b7,22e8a9f4..00000000[m
[1;33m--- a/gst-plugin-qmmfsrc/qmmf_source_context.cc[m
[1;33m+++ b/gst-plugin-qmmfsrc/qmmf_source_context.cc[m
[1;33mdiff --cc gst-plugin-vcomposer/videocomposer.c[m
[1;33mindex 4eca566e,aff618f5..00000000[m
[1;33m--- a/gst-plugin-vcomposer/videocomposer.c[m
[1;33m+++ b/gst-plugin-vcomposer/videocomposer.c[m
[1;33mdiff --cc gst-plugin-vcomposer/videocomposersinkpad.c[m
[1;33mindex 590fa6e1,b616da7e..00000000[m
[1;33m--- a/gst-plugin-vcomposer/videocomposersinkpad.c[m
[1;33m+++ b/gst-plugin-vcomposer/videocomposersinkpad.c[m
[1;33mdiff --cc gst-plugin-vcomposer/videocomposersinkpad.h[m
[1;33mindex cd3e3bd4,b3c5eb15..00000000[m
[1;33m--- a/gst-plugin-vcomposer/videocomposersinkpad.h[m
[1;33m+++ b/gst-plugin-vcomposer/videocomposersinkpad.h[m
[1;33mdiff --cc gst-sample-apps/gst-weston-composition-example/main.cc[m
[1;33mindex f4efb100,f8bf3e14..00000000[m
[1;33m--- a/gst-sample-apps/gst-weston-composition-example/main.cc[m
[1;33m+++ b/gst-sample-apps/gst-weston-composition-example/main.cc[m
[1;33mdiff --git a/gst-plugin-base/gst/video/c2d-video-converter.c b/gst-plugin-base/gst/video/c2d-video-converter.c[m
[1;33mindex 2340149c..97dc26b3 100644[m
[1;33m--- a/gst-plugin-base/gst/video/c2d-video-converter.c[m
[1;33m+++ b/gst-plugin-base/gst/video/c2d-video-converter.c[m
[1;35m@@ -318,9 +318,10 @@[m [mgst_c2d_blits_compatible (const GstVideoComposition * l_composition,[m
     l_blit = &(l_composition->blits[idx]);[m
     r_blit = &(r_composition->blits[idx]);[m
 [m
[1;31m-    // Both entries need to have the same ubwc, flip, rotate and global alpha.[m
[1;31m-    if ((l_blit->rotate != r_blit->rotate) || (l_blit->alpha != r_blit->alpha) ||[m
[1;31m-        (l_blit->flip != r_blit->flip) || (l_blit->isubwc != r_blit->isubwc))[m
[1;32m+[m[1;32m    // Both entries need to have the same flip, rotate and global alpha.[m
[1;32m+[m[1;32m    if ((l_blit->rotate != r_blit->rotate) ||[m
[1;32m+[m[1;32m        (l_blit->alpha != r_blit->alpha) ||[m
[1;32m+[m[1;32m        (l_blit->flip != r_blit->flip))[m
       return FALSE;[m
 [m
     l_fd = gst_fd_memory_get_fd ([m
[1;35m@@ -424,8 +425,6 @@[m [mgst_c2d_optimize_composition (GstVideoBlit * blit,[m
     // Increase the score if both target blit surfaces have the same format.[m
     l_score += (GST_VIDEO_FRAME_FORMAT (l_composition->frame) ==[m
         GST_VIDEO_FRAME_FORMAT (composition->frame)) ? 1 : 0;[m
[1;31m-    // Increase the score if both target blit surfaces have the same UBWC flag.[m
[1;31m-    l_score += (l_composition->isubwc == composition->isubwc) ? 1 : 0;[m
 [m
     if (l_score <= score)[m
       continue;[m
[1;35m@@ -434,7 +433,6 @@[m [mgst_c2d_optimize_composition (GstVideoBlit * blit,[m
     score = l_score;[m
 [m
     blit->frame = l_composition->frame;[m
[1;31m-    blit->isubwc = l_composition->isubwc;[m
 [m
     optimized = TRUE;[m
   }[m
[1;35m@@ -482,7 +480,7 @@[m [mgst_c2d_unmap_gpu_address (gpointer key, gpointer data, gpointer userdata)[m
 [m
 static guint[m
 gst_c2d_create_surface (GstC2dVideoConverter * convert,[m
[1;31m-    const GstVideoFrame * frame, guint bits, gboolean isubwc)[m
[1;32m+[m[1;32m    const GstVideoFrame * frame, guint bits)[m
 {[m
   const gchar *format = NULL, *compression = NULL;[m
   guint surface_id = 0;[m
[1;35m@@ -502,13 +500,10 @@[m [mgst_c2d_create_surface (GstC2dVideoConverter * convert,[m
         gst_video_format_to_c2d_format (GST_VIDEO_FRAME_FORMAT (frame));[m
     g_return_val_if_fail (surface.format != 0, 0);[m
 [m
[1;31m-    // In case the format has UBWC enabled append additional format flags.[m
[1;31m-    if (isubwc) {[m
[1;31m-      surface.format |= C2D_FORMAT_UBWC_COMPRESSED;[m
[1;32m+[m[1;32m    if (surface.format & C2D_FORMAT_UBWC_COMPRESSED)[m
       compression = " UBWC";[m
[1;31m-    } else {[m
[1;32m+[m[1;32m    else[m
       compression = "";[m
[1;31m-    }[m
 [m
     // Set surface dimensions.[m
     surface.width = GST_VIDEO_FRAME_WIDTH (frame);[m
[1;35m@@ -542,13 +537,10 @@[m [mgst_c2d_create_surface (GstC2dVideoConverter * convert,[m
         gst_video_format_to_c2d_format (GST_VIDEO_FRAME_FORMAT (frame));[m
     g_return_val_if_fail (surface.format != 0, 0);[m
 [m
[1;31m-    // In case the format has UBWC enabled append additional format flags.[m
[1;31m-    if (isubwc) {[m
[1;31m-      surface.format |= C2D_FORMAT_UBWC_COMPRESSED;[m
[1;32m+[m[1;32m    if (surface.format & C2D_FORMAT_UBWC_COMPRESSED)[m
       compression = " UBWC";[m
[1;31m-    } else {[m
[1;32m+[m[1;32m    else[m
       compression = "";[m
[1;31m-    }[m
 [m
     // Set surface dimensions.[m
     surface.width = GST_VIDEO_FRAME_WIDTH (frame);[m
[1;35m@@ -625,7 +617,7 @@[m [mgst_c2d_create_surface (GstC2dVideoConverter * convert,[m
 [m
 static gboolean[m
 gst_c2d_update_surface (GstC2dVideoConverter * convert,[m
[1;31m-    const GstVideoFrame * frame, guint surface_id, guint bits, gboolean isubwc)[m
[1;32m+[m[1;32m    const GstVideoFrame * frame, guint surface_id, guint bits)[m
 {[m
   const gchar *format = NULL, *compression = NULL;[m
   C2D_STATUS status = C2D_STATUS_NOT_SUPPORTED;[m
[1;35m@@ -655,13 +647,10 @@[m [mgst_c2d_update_surface (GstC2dVideoConverter * convert,[m
         gst_video_format_to_c2d_format (GST_VIDEO_FRAME_FORMAT (frame));[m
     g_return_val_if_fail (surface.format != 0, FALSE);[m
 [m
[1;31m-    // In case the format has UBWC enabled append additional format flags.[m
[1;31m-    if (isubwc) {[m
[1;31m-      surface.format |= C2D_FORMAT_UBWC_COMPRESSED;[m
[1;32m+[m[1;32m    if (surface.format & C2D_FORMAT_UBWC_COMPRESSED)[m
       compression = " UBWC";[m
[1;31m-    } else {[m
[1;32m+[m[1;32m    else[m
       compression = "";[m
[1;31m-    }[m
 [m
     // Set surface dimensions.[m
     surface.width = GST_VIDEO_FRAME_WIDTH (frame);[m
[1;35m@@ -695,13 +684,10 @@[m [mgst_c2d_update_surface (GstC2dVideoConverter * convert,[m
         gst_video_format_to_c2d_format (GST_VIDEO_FRAME_FORMAT (frame));[m
     g_return_val_if_fail (surface.format != 0, FALSE);[m
 [m
[1;31m-    // In case the format has UBWC enabled append additional format flags.[m
[1;31m-    if (isubwc) {[m
[1;31m-      surface.format |= C2D_FORMAT_UBWC_COMPRESSED;[m
[1;32m+[m[1;32m    if (surface.format & C2D_FORMAT_UBWC_COMPRESSED)[m
       compression = " UBWC";[m
[1;31m-    } else {[m
[1;32m+[m[1;32m    else[m
       compression = "";[m
[1;31m-    }[m
 [m
     // Set surface dimensions.[m
     surface.width = GST_VIDEO_FRAME_WIDTH (frame);[m
[1;35m@@ -987,8 +973,7 @@[m [mgst_c2d_update_object (C2D_OBJECT * object, const guint surface_id,[m
 [m
 static guint[m
 gst_c2d_retrieve_surface_id (GstC2dVideoConverter * convert,[m
[1;31m-    GHashTable * surfaces, guint bits, const GstVideoFrame * vframe,[m
[1;31m-    const gboolean isubwc)[m
[1;32m+[m[1;32m    GHashTable * surfaces, guint bits, const GstVideoFrame * vframe)[m
 {[m
   GstMemory *memory = NULL;[m
   guint fd = 0, surface_id = 0;[m
[1;35m@@ -1006,7 +991,7 @@[m [mgst_c2d_retrieve_surface_id (GstC2dVideoConverter * convert,[m
 [m
   if (!g_hash_table_contains (surfaces, GUINT_TO_POINTER (fd))) {[m
     // Create an output surface and add its ID to the output hash table.[m
[1;31m-    surface_id = gst_c2d_create_surface (convert, vframe, bits, isubwc);[m
[1;32m+[m[1;32m    surface_id = gst_c2d_create_surface (convert, vframe, bits);[m
 [m
     if (surface_id ==