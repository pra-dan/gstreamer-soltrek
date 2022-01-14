/*
Run: gcc bt3-dynamic-pipelines.c -o bt3-dynamic-pipelines `pkg-config --cflags --libs gstreamer-1.0`
Aim:
  - How to attain finer control when linking elements
  - How to be notified of interesting events so we can react in time
  - What various states can an element be in

In this app, we open a file which is multiplexed/muxed. Audio and video are
stored together inside a container file. Some container formats are: Matroska(MKV),
Quick Time (QT, MOV), Ogg or Advanced Systems Format (ASF, WMV, WMA). 'Demuxers'
are the elements responsible for unpacking such containers.

Pads in Gstreamer: They are ports through which GStreamer elements communicate
with each other, called `GstPad`, mainly of 2 types:
  - sink pads: data enters an element through them
  - source pads: data exits an element through them
  # sink elements only have sink pads; source elements only have source pads,
  and filter elements contain both.

  --source--                ---sink---
  |     |src|               |sink|    |
  ----------                ----------

A demuxer can have two such pads:
  ----demuxer-----
  |sink|   |audio|
  |        |video|
  ----------------
  Also look at fig3 (Example pipeline with two branches) on https://gstreamer.freedesktop.org/documentation/tutorials/basic/dynamic-pipelines.html?gi-language=c

A demuxer doesn't have a source pad (so not exit port) and hence can't have elements
next to it; we must terminate the pipeline after a demuxer.

In this example, we only demux the audio stream and not the video.

Signals in GStreamer: Signal are crucial and allow us to be notified (by means
of a callback) when something interesting has happened. They are identified with
a name and each `GObject` has its own signals.
*/

#include <gst/gst.h>

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData {
  GstElement *pipeline;
  GstElement *source;
  GstElement *convert;
  GstElement *resample;
  GstElement *sink;
} CustomData;

/* Handler for the pad-added signal */
static void pad_added_handler (GstElement *src, GstPad *pad, CustomData *data);

int main(int argc, char *argv[]) {
  CustomData data;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;
  gboolean terminate = FALSE;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  // Create the elements
  data.source = gst_element_factory_make ("uridecodebin", "source");
  /*
  `uridecodebin` will internally instantiate all the necessary elements (sources,
  demuxers and decoders) to turn a URI into raw audio and/or video streams.
  It does half the work that `playbin` does; dince it contains demuxers, source
  pads are not initially available and we will need to link them on the fly.
  */
  data.convert = gst_element_factory_make ("audioconvert", "convert");
  /*
  `audioconvert` is used for converting b/w different audio formats, making sure
  this example works on any platform, since the audio decoder might not be the
  same that the audio sink expects.
  */
  data.resample = gst_element_factory_make ("audioresample", "resample");
  /*
  `audioresample` is used to convert b/w different audio sample rates, similarly
  making sure it works on any platform.
  */
  data.sink = gst_element_factory_make ("autoaudiosink", "sink");
  /*
  The `autoaudiosink` is the equivalent of `autovideosink` for video.
  */

  /* Create the empty pipeline */
  data.pipeline = gst_pipeline_new ("test-pipeline");

  if (!data.pipeline || !data.source || !data.convert || !data.resample || !data.sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  // Build the pipeline/Add elements to the bin.
  gst_bin_add_many (GST_BIN (data.pipeline), data.source, data.convert, data.resample, data.sink, NULL);
  /*
  Note that we are NOT linking the source at this point, since
  it contains no source pads at this point. We will do it later.
  */
  if (!gst_element_link_many (data.convert, data.resample, data.sink, NULL)) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  /* Set the URI to play */
  g_object_set (data.source, "uri", "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm", NULL);

  // Connect to the pad-added signal
  /*
  Each element in GStreamer has its own signals. Just like `source` has a signal
  called 'pad-added'. It notifies the app (its attached to), if its element just
  added a pad.
  We use `g_signal_connect()` to attach the signal to our app. It involves
  providing a callback function (`pad_added_handler`) using `G_CALLBACK()` and
  a data pointer (`&data`) that is passed on to the callback function and helps
  in sharing information b/w the `main` and callback function.
  */
  g_signal_connect (data.source, "pad-added", G_CALLBACK (pad_added_handler), &data);
  /*
  (TBConfirmed) What `g_signal_connect` is indirectly doing is, its creating a signal that
  triggers `pad_added_handler` and passes arguments(`data.source`, `"pad-added"`
  and `data`) in a not-so-common way 😕.

  /* Start playing */
  ret = gst_element_set_state (data.pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  /* Listen to the bus */
  bus = gst_element_get_bus (data.pipeline);
  do {
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
        GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* Parse message */
    if (msg != NULL) {
      GError *err;
      gchar *debug_info;

      switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
          gst_message_parse_error (msg, &err, &debug_info);
          g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
          g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
          g_clear_error (&err);
          g_free (debug_info);
          terminate = TRUE;
          break;
        case GST_MESSAGE_EOS:
          g_print ("End-Of-Stream reached.\n");
          terminate = TRUE;
          break;
        case GST_MESSAGE_STATE_CHANGED:
          /* We are only interested in state-changed messages from the pipeline */
          if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data.pipeline)) {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
            g_print ("Pipeline state changed from %s to %s:\n",
                gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
          }
          break;
        default:
          /* We should not reach here */
          g_printerr ("Unexpected message received.\n");
          break;
      }
      gst_message_unref (msg);
    }
  } while (!terminate);

  /* Free resources */
  gst_object_unref (bus);
  gst_element_set_state (data.pipeline, GST_STATE_NULL);
  gst_object_unref (data.pipeline);
  return 0;
}

/* This function will be called by the 'pad-added' signal */
static void pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data) {
  /*
   The first parameter of a signal handler is always the object that has triggered it.
   When our source element finally has enough information to start producing data,
   it will create source pads, and trigger the “pad-added” signal. At this point
   our callback (this function) will be called.

   The part of the architecture where this is implemented is:
   +-----source-----+     link      +-----converter-----+       +----resample-~~
   |   |new_pad|    |-- -- -- -- -- |sink|              |-------|
   +-----------------               +-------------------+       +-------------~~
   The link b/w source & converter doesn't exist until this (callback) function
   is called. And its only called when sources get input data and automatically
   generates a pad. This generates the signal and triggers this function which
   grabs the sink pad of 'converter' and only connects with (newly generated pad)
   of source if its of the type "audio/x-raw".
  */
  GstPad *sink_pad = gst_element_get_static_pad (data->convert, "sink");
  /*
  Above, we extract the converter element and then retreieve *its* sink pad using
  `gst_element_get_static_pad` (rememeber, all sink filter-type elements have
  source and sink pads). We want to link this pad with `new_pad` (later).
  */
  GstPadLinkReturn ret;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;

  g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

  // If our converter is already linked, we have nothing to do here
  /*
   `uridecodebin` can create as many pads as it seems fit and for each one, this
   callback will be called. The following lines prevent us from trying to link to
   a new pad once we are already linked.
  */
  if (gst_pad_is_linked (sink_pad)) {
    g_print ("We are already linked. Ignoring.\n");
    goto exit;
  }

  // Check the new_pad's type
  /*
  Now we check the type of data this new pad is going to output, since we are
  only interested in audio output.
  */
  new_pad_caps = gst_pad_get_current_caps (new_pad);
  /*
  `gst_pad_get_current_caps` retreieves the current capabilities of the pad i.e.,
  the kind of data it outputs, wrapped in `GstCaps` structure.
  */
  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  /*
  A pad can offer many caps/capabilities(i.e., type of outputs) and
  correspondingly, many structures. In our case, we know that our pad only had
  one cap/output: audio, hence we retreieve the first and only structure using `0`.
  # If there are no caps on a pad yet, we get NULL.
  */
  new_pad_type = gst_structure_get_name (new_pad_struct);
  /*
  Retrieve the name of the structure using `gst_structure_get_name` and check
  (below), if the name is `audio/x-raw`.
  */
  if (!g_str_has_prefix (new_pad_type, "audio/x-raw")) {
    g_print ("It has type '%s' which is not raw audio. Ignoring.\n", new_pad_type);
    goto exit;
  }

  // (Otherwise) Attempt the link
  ret = gst_pad_link (new_pad, sink_pad);
  /*
  Similar to `gst_element_link`, `gst_pad_link` also accepts source before sink
  and both pads must reside in the same bin (note that earlier, we added source
  to the bin but not into the pipeline).
  */
  if (GST_PAD_LINK_FAILED (ret)) {
    g_print ("Type is '%s' but link failed.\n", new_pad_type);
  } else {
    g_print ("Link succeeded (type '%s').\n", new_pad_type);
  }

exit:
  /* Unreference the new pad's caps, if we got them */
  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);

  /* Unreference the sink pad */
  gst_object_unref (sink_pad);
}
