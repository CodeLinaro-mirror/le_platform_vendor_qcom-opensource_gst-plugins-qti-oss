// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_VESDELIVER_H__
#define __GST_VESDELIVER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_VESDELIVER \
  (gst_vesdeliver_get_type())
#define GST_VESDELIVER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VESDELIVER,GstVesDeliver))
#define GST_VESDELIVER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VESDELIVER,GstVesDeliverClass))
#define GST_IS_VESDELIVER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VESDELIVER))
#define GST_IS_VESDELIVER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VESDELIVER))

typedef struct _GstVesDeliver GstVesDeliver;
typedef struct _GstVesDeliverClass GstVesDeliverClass;

struct _GstVesDeliver
{
  GstBaseTransform parent;

  GstBufferPool *outpool;
};

struct _GstVesDeliverClass
{
  GstBaseTransformClass parent_class;
};

GType gst_vesdeliver_get_type (void);

G_END_DECLS

#endif /* __GST_VESDELIVER_H__ */
