/*
Run: gcc bt2-gstreamer-concepts.c -o bt2-gstreamer-concepts `pkg-config --cflags --libs gstreamer-1.0`
In this tut, we build a pipeline manually by instatiating each element and linking
them all together.
A general pipeline:
  source -> filter -> sink
But in this tut, we use
  source -> sink
*/

#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstElement *pipeline, *source, *sink;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the 2 elements */
  /*
  `gst_element_factory_make`: helps create new elements.
    First param: type of element to create.
    Second param: name of this element
      # Naming your elements is useful to retreieve them later if you didn't
      keep a pointer and also for debugging. If we pass NULL, Gstreamer will
      itself provide a unique name for it.
  */
  source = gst_element_factory_make ("videotestsrc", "source"); // source
  /*
  `videotestsrc` is a src element which creates a test video pattern. Its helpful
    for debugging purpose.
  */
  sink = gst_element_factory_make ("autovideosink", "sink");    // sink
  /*
  `autovideosink` is a sink element that displays on a window, the images it receives.
    It automatically selects the best video sink depending on the OS.
  */

  /* Create the empty pipeline */
  pipeline = gst_pipeline_new ("test-pipeline");

  if (!pipeline || !source || !sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  // Build the pipeline
  /*
  A pipeline is a type of a `bin` which is the element used to contain other
  elements. Thus, all methods that apply to bins, also apply to pipelines.
  `gst_bin_add_many` adds elements to the pipeline and always ends with `NULL`.
  `GST_BIN()` is just for typecasting (why ? ...TBF)
  To add elements individually, `gst_bin_add()` can be used.
  */
  gst_bin_add_many (GST_BIN (pipeline), source, sink, NULL);

  // Link all elements
  /*
  `gst_element_link` links all elements ONLY after they have been put in a bin.
  The syntax is: `gst_element_link(SOURCE, DESTINATION)` and the order is crucial.
  */
  if (gst_element_link (source, sink) != TRUE) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (pipeline);
    return -1;
  }

  // Modify the source's properties
  /*
  All gstreamer objects are a particular kind of `GObject` which is the entity
  offering 'property' facitilies. Most elements have customizable properties:
  named attributes that can be modified to change the element's behavior (writable
  properties) or inquired to find out about the element's internal state (readable
  property).
  Properties are read from with `g_object_get()` and written to with `g_object_set()`.
  `g_object_get()` accepts a NULL-terminated list of
    - property-name and
    - property-value pairs
  In the following line of code, we change the property of our source: the
  `videotestsrc` element; we change the test pattern.
  */
  g_object_set (source, "pattern", 0, NULL); // Changing 0 to 1,2,etc changes pattern

  /* Start playing */
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (pipeline);
    return -1;
  }

  /* Wait until error or EOS */
  bus = gst_element_get_bus (pipeline);
  msg =
      gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  /* Parse message */
  if (msg != NULL) {
    GError *err;
    gchar *debug_info;

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_ERROR:
        gst_message_parse_error (msg, &err, &debug_info);
        /*
        `gst_message_parse_error` returns a GLib `GError` error structure
        (`GError *err`) and a useful string for debugging (`gchar *debug_info`).
        */
        g_printerr ("Error received from element %s: %s\n",
            GST_OBJECT_NAME (msg->src), err->message);
        g_printerr ("Debugging information: %s\n",
            debug_info ? debug_info : "none");
        g_clear_error (&err);
        g_free (debug_info);
        break;
      case GST_MESSAGE_EOS:
        g_print ("End-Of-Stream reached.\n");
        break;
      default:
        /* We should not reach here because we only asked for ERRORs and EOS */
        g_printerr ("Unexpected message received.\n");
        break;
    }
    gst_message_unref (msg);
  }

  /* Free resources */
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  return 0;
}
