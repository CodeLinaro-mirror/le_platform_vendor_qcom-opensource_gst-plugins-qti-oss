/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "samplestitchingsinkpad.h"

#include <gst/video/video-utils.h>
#include <gst/utils/common-utils.h>

GST_DEBUG_CATEGORY_STATIC (gst_sample_stitching_sinkpad_debug);
#define GST_CAT_DEFAULT gst_sample_stitching_sinkpad_debug

#define DEFAULT_PROP_DESTINATION_WIDTH         0
#define DEFAULT_PROP_DESTINATION_HEIGHT        0

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

G_DEFINE_TYPE (GstSampleStitchingSinkPad, gst_sample_stitching_sinkpad,
               GST_TYPE_AGGREGATOR_PAD);

enum
{
  PROP_0,
};

static GstCaps *
gst_sample_stitching_sinkpad_transform_caps (GstAggregatorPad * pad,
    GstCaps * caps, GstCaps * filter)
{
  GstCaps *result = NULL;
  GstStructure *structure = NULL;
  GstCapsFeatures *features = NULL;
  gint idx = 0, length = 0;

  GST_DEBUG_OBJECT (pad, "Transforming caps %" GST_PTR_FORMAT, caps);
  GST_DEBUG_OBJECT (pad, "Filter caps %" GST_PTR_FORMAT, filter);

  result = gst_caps_new_empty ();

  // In case there is no featureless or memory:GBM caps structure add one.
  if (gst_is_gbm_supported () && !gst_caps_is_empty (caps) &&
      !gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    structure = gst_caps_get_structure (caps, 0);
    features = gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GBM, NULL);

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    // Set width and height to a range instead of fixed value.
    gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

    // If pixel aspect ratio, make a range of it.
    if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
      gst_structure_set (structure, "pixel-aspect-ratio",
          GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
    }

    // Remove the format/color/compression related fields.
    gst_structure_remove_fields (structure, "format", "colorimetry",
        "chroma-site", "compression", NULL);

    gst_caps_append_structure_full (result, structure, features);
  } else if (!gst_caps_is_empty (caps) && !gst_caps_has_feature (caps, NULL)) {

    structure = gst_caps_get_structure (caps, 0);

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    // Set width and height to a range instead of fixed value.
    gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

    // If pixel aspect ratio, make a range of it.
    if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
      gst_structure_set (structure, "pixel-aspect-ratio",
          GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
    }

    // Remove the format/color/compression related fields.
    gst_structure_remove_fields (structure, "format", "colorimetry",
        "chroma-site", "compression", NULL);

    gst_caps_append_structure (result, structure);

    gchar *caps_str = gst_caps_to_string(result);
    GST_DEBUG("Resulting caps: %s", caps_str);
    g_free(caps_str);
  }

  length = gst_caps_get_size (caps);

  for (idx = 0; idx < length; idx++) {
    structure = gst_caps_get_structure (caps, idx);
    features = gst_caps_get_features (caps, idx);

    // If this is already expressed by the existing caps skip this structure.
    if (idx > 0 && gst_caps_is_subset_structure_full (result, structure, features))
      continue;

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    // Set width, height and framerate to a range instead of fixed value.
    gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT16,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT16, "framerate",
        GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXUINT8, 1, NULL);

    // If pixel aspect ratio field exists, make a range of it.
    if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
      gst_structure_set (structure, "pixel-aspect-ratio",
          GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
    }

    // Remove the format/color related fields.
    gst_structure_remove_fields (structure, "format", "colorimetry",
        "chroma-site", NULL);

    gst_caps_append_structure_full (result, structure,
        gst_caps_features_copy (features));
  }

  if (filter) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

gboolean
gst_sample_stitching_sinkpad_acceptcaps (GstAggregatorPad * pad,
    GstAggregator * aggregator, GstCaps * caps)
{
  GstPad *srcpad = GST_AGGREGATOR_SRC_PAD (aggregator);
  GstCaps *tmplcaps = NULL, *sinkcaps = NULL;
  gboolean success = FALSE;

  GST_DEBUG_OBJECT (pad, "Caps %" GST_PTR_FORMAT, caps);

  tmplcaps = gst_pad_get_pad_template_caps (GST_PAD (pad));
  GST_DEBUG_OBJECT (pad, "Template: %" GST_PTR_FORMAT, tmplcaps);

  success = gst_caps_can_intersect (caps, tmplcaps);
  gst_caps_unref (tmplcaps);

  if (!success) {
    GST_WARNING_OBJECT (pad, "Caps can't intersect with pad template!");
    return FALSE;
  }

  // Use currently set caps if they are set otherwise use template caps.
  tmplcaps = gst_pad_get_pad_template_caps (srcpad);

  GST_DEBUG_OBJECT (pad, "Trying to transform with source template as filter:"
      " %" GST_PTR_FORMAT, tmplcaps);

  sinkcaps = gst_sample_stitching_sinkpad_transform_caps (pad, caps, tmplcaps);
  gst_caps_unref (tmplcaps);

  success = (sinkcaps != NULL) && !gst_caps_is_empty (sinkcaps);

  if (sinkcaps != NULL)
    gst_caps_unref (sinkcaps);

  if (!success) {
    GST_WARNING_OBJECT (pad, "Failed to transform %" GST_PTR_FORMAT
        " in anything supported by the pad!", caps);
    return FALSE;
  }

  return TRUE;
}

GstCaps *
gst_sample_stitching_sinkpad_getcaps (GstAggregatorPad * pad,
    GstAggregator * aggregator, GstCaps * filter)
{
  GstPad *srcpad = GST_AGGREGATOR_SRC_PAD (aggregator);
  GstCaps *srccaps = NULL, *sinkcaps = NULL, *peercaps = NULL;
  GstCaps *tmpcaps = NULL, *tmplcaps = NULL, *intersect = NULL;

  // Use currently set caps if they are set otherwise use template caps.
  srccaps = gst_pad_get_pad_template_caps (srcpad);

  if (filter != NULL) {
    GST_DEBUG_OBJECT (pad, "Filter caps  %" GST_PTR_FORMAT, filter);

    tmplcaps = gst_pad_get_pad_template_caps (GST_PAD (pad));
    GST_DEBUG_OBJECT (pad, "Sink template caps %" GST_PTR_FORMAT, tmplcaps);

    // Intersect filter caps with the sink pad template.
    intersect =
        gst_caps_intersect_full (filter, tmplcaps, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersect);

    gst_caps_unref (tmplcaps);

    // Check whether the intersected caps can be transformed.
    tmpcaps =
        gst_sample_stitching_sinkpad_transform_caps (pad, intersect, NULL);
    GST_DEBUG_OBJECT (pad, "Transformed caps  %" GST_PTR_FORMAT, tmpcaps);

    gst_caps_unref (intersect);

    // If transformed caps are not empty intersect them with the source caps.
    if (tmpcaps && !gst_caps_is_empty (tmpcaps)) {
      GST_DEBUG_OBJECT (pad, "Source caps  %" GST_PTR_FORMAT, srccaps);

      // Intersect with source pad caps.
      intersect = gst_caps_intersect_full (tmpcaps, srccaps,
          GST_CAPS_INTERSECT_FIRST);

      gst_caps_unref (tmpcaps);
      tmpcaps = intersect;
    }
  }

  GST_DEBUG_OBJECT (pad, "Peer filter caps %" GST_PTR_FORMAT, tmpcaps);

  if ((tmpcaps != NULL) && gst_caps_is_empty (tmpcaps)) {
    GST_WARNING_OBJECT (pad, "Peer filter caps are empty!");
    gst_caps_unref (srccaps);
    return tmpcaps;
  }

  // Query the source pad peer with the transformed filter.
  peercaps = gst_pad_peer_query_caps (srcpad, tmpcaps);

  if (tmpcaps != NULL)
    gst_caps_unref (tmpcaps);

  if (peercaps != NULL) {
    GST_DEBUG_OBJECT (pad, "Peer caps  %" GST_PTR_FORMAT, peercaps);

    // Filter the peer caps against the source pad caps.
    tmpcaps =
        gst_caps_intersect_full (peercaps, srccaps, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, tmpcaps);

    gst_caps_unref (srccaps);
    gst_caps_unref (peercaps);
  } else {
    tmpcaps = srccaps;
  }

  // Check whether the intersected sink caps can be transformed.
  sinkcaps = gst_sample_stitching_sinkpad_transform_caps (pad, tmpcaps, filter);
  gst_caps_unref (tmpcaps);

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, sinkcaps);
  return sinkcaps;
}

gboolean
gst_sample_stitching_sinkpad_setcaps (GstAggregatorPad * pad,
    GstAggregator * aggregator, GstCaps * caps)
{
  GstVideoInfo info;

  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

  GST_DEBUG_OBJECT (pad, "Caps %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_DEBUG_OBJECT (pad, "Failed get video info from caps!");
    return FALSE;
  }

  if (GST_SAMPLE_STITCHING_SINKPAD (pad)->info != NULL)
    gst_video_info_free (GST_SAMPLE_STITCHING_SINKPAD (pad)->info);

  GST_SAMPLE_STITCHING_SINKPAD (pad)->info = gst_video_info_copy (&info);

  return TRUE;
}

static gboolean
gst_sample_stitching_sinkpad_skip_buffer (GstAggregatorPad * pad,
    GstAggregator * aggregator, GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (pad, "skip buffer!");
  GstSegment *segment = &GST_AGGREGATOR_PAD (aggregator->srcpad)->segment;

  if (segment->position != GST_CLOCK_TIME_NONE
      && GST_BUFFER_DURATION (buffer) != GST_CLOCK_TIME_NONE) {
    GstClockTime timestamp, position;

    timestamp = gst_segment_to_running_time (&pad->segment, GST_FORMAT_TIME,
        GST_BUFFER_PTS (buffer)) + GST_BUFFER_DURATION (buffer);
    position = gst_segment_to_running_time (segment, GST_FORMAT_TIME,
        segment->position);

    return (timestamp < position);
  }

  return FALSE;
}

static void
gst_sample_stitching_sinkpad_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec *pspec)
{
  GstSampleStitchingSinkPad *sinkpad = GST_SAMPLE_STITCHING_SINKPAD (object);
  GstElement *parent = gst_pad_get_parent_element (GST_PAD (sinkpad));
  const gchar *propname = g_param_spec_get_name (pspec);

  // Extract the state from the pad parent or in case there is no parent
  // use default value as parameters are being set upon object construction.
  GstState state = parent ? GST_STATE (parent) : GST_STATE_VOID_PENDING;

  // Decrease the pad parent reference count as it is not needed any more.
  if (parent != NULL)
    gst_object_unref (parent);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING_OBJECT (sinkpad, "Property '%s' change not supported in %s "
        "state!", propname, gst_element_state_get_name (state));
    return;
  }

  GST_SAMPLE_STITCHING_SINKPAD_LOCK (sinkpad);

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (sinkpad, property_id, pspec);
      break;
  }

  GST_SAMPLE_STITCHING_SINKPAD_UNLOCK (sinkpad);

  // Emit a 'notify' signal for the changed property.
  g_object_notify_by_pspec (G_OBJECT (sinkpad), pspec);
}

static void
gst_sample_stitching_sinkpad_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstSampleStitchingSinkPad *sinkpad = GST_SAMPLE_STITCHING_SINKPAD (object);

  GST_SAMPLE_STITCHING_SINKPAD_LOCK (sinkpad);

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (sinkpad, property_id, pspec);
      break;
  }

  GST_SAMPLE_STITCHING_SINKPAD_UNLOCK (sinkpad);
}

static void
gst_sample_stitching_sinkpad_finalize (GObject * object)
{
  GstSampleStitchingSinkPad *sinkpad = GST_SAMPLE_STITCHING_SINKPAD (object);

  GST_DEBUG ("For Debug: gst_sample_stitching_sinkpad_finalize");

  g_mutex_clear (&sinkpad->lock);

  if (sinkpad->info != NULL)
    gst_video_info_free (sinkpad->info);

  G_OBJECT_CLASS (gst_sample_stitching_sinkpad_parent_class)->finalize(object);
}

static void
gst_sample_stitching_sinkpad_class_init (GstSampleStitchingSinkPadClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstAggregatorPadClass *aggpad = (GstAggregatorPadClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_sample_stitching_sinkpad_finalize);
  gobject->get_property =
      GST_DEBUG_FUNCPTR (gst_sample_stitching_sinkpad_get_property);
  gobject->set_property =
      GST_DEBUG_FUNCPTR (gst_sample_stitching_sinkpad_set_property);
  aggpad->skip_buffer =
      GST_DEBUG_FUNCPTR (gst_sample_stitching_sinkpad_skip_buffer);

  GST_DEBUG_CATEGORY_INIT (gst_sample_stitching_sinkpad_debug,
      "qtisamplestitching", 0, "QTI Video Sample Stitching sink pad");
}

static void
gst_sample_stitching_sinkpad_init (GstSampleStitchingSinkPad * sinkpad)
{
  g_mutex_init (&sinkpad->lock);

  sinkpad->index  = 0;
  sinkpad->info   = NULL;

  sinkpad->destination.w = DEFAULT_PROP_DESTINATION_WIDTH;
  sinkpad->destination.h = DEFAULT_PROP_DESTINATION_HEIGHT;
}
