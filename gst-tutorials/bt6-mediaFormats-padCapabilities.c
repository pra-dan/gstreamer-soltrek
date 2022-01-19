/*
Run: gcc bt6-mediaFormats-padCapabilities.c -o bt6 `pkg-config --cflags --libs gstreamer-1.0`

Pads allow information to enter and leave an element. The caps/capabilities of a
pad specify what kind of information can travel through the pad, for e.g., 30fps,
or 5.1 channels, etc. Pads can support multiple caps and caps can be specified as
ranges however the actualy information traveling from pad to pad must have only
one well-specified type meaning that through a process called 'negotiation', two
linked pads agree on a common type and then the caps of the pads become fixed (i.e
they no longer contain ranges).

Pads are created from Pad templates which are the first step in the negotiation
process. Actual pads are instantiated in process (as the process evolves) and their
caps are refined until they are fixed or negotiation fails.

This tutorial instantiates 2 elements (this time through their factories;the
longer way), shows their pad templates (via `print_pad_templates_information`),
links them and sets the pipeline to play.
On each stage change, the caps of the *sink element's pad* are shown, so we can
observe how the negotiation proceeds until the pad caps are fixed.

This app doesn't terminate as we have taken no action when `msg == NULL` i.e.,
when timeout occurs.
*/
#include <gst/gst.h>

/* Functions below print the Capabilities in a human-friendly format */
static gboolean print_field (GQuark field, const GValue * value, gpointer pfx) {
  gchar *str = gst_value_serialize (value);

  g_print ("%s  %15s: %s\n", (gchar *) pfx, g_quark_to_string (field), str);
  g_free (str);
  return TRUE;
}

static void print_caps (const GstCaps * caps, const gchar * pfx) {
  guint i;

  g_return_if_fail (caps != NULL);

  if (gst_caps_is_any (caps)) {
    g_print ("%sANY\n", pfx);
    return;
  }
  if (gst_caps_is_empty (caps)) {
    g_print ("%sEMPTY\n", pfx);
    return;
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    g_print ("%s%s\n", pfx, gst_structure_get_name (structure));
    gst_structure_foreach (structure, print_field, (gpointer) pfx);
  }
}

/* Prints information about a Pad Template, including its Capabilities */
static void print_pad_templates_information (GstElementFactory * factory) {
  const GList *pads;
  GstStaticPadTemplate *padtemplate;

  g_print ("Pad Templates for %s:\n", gst_element_factory_get_longname (factory));
  if (!gst_element_factory_get_num_pad_templates (factory)) {
    g_print ("  none\n");
    return;
  }

  pads = gst_element_factory_get_static_pad_templates (factory);
  while (pads) {
    padtemplate = pads->data;
    pads = g_list_next (pads);

    if (padtemplate->direction == GST_PAD_SRC)
      g_print ("  SRC template: '%s'\n", padtemplate->name_template);
    else if (padtemplate->direction == GST_PAD_SINK)
      g_print ("  SINK template: '%s'\n", padtemplate->name_template);
    else
      g_print ("  UNKNOWN!!! template: '%s'\n", padtemplate->name_template);

    if (padtemplate->presence == GST_PAD_ALWAYS)
      g_print ("    Availability: Always\n");
    else if (padtemplate->presence == GST_PAD_SOMETIMES)
      g_print ("    Availability: Sometimes\n");
    else if (padtemplate->presence == GST_PAD_REQUEST)
      g_print ("    Availability: On request\n");
    else
      g_print ("    Availability: UNKNOWN!!!\n");

    if (padtemplate->static_caps.string) {
      GstCaps *caps;
      g_print ("    Capabilities:\n");
      caps = gst_static_caps_get (&padtemplate->static_caps);
      print_caps (caps, "      ");
      gst_caps_unref (caps);

    }

    g_print ("\n");
  }
}

/* Shows the CURRENT capabilities of the requested pad in the given element */
static void print_pad_capabilities (GstElement *element, gchar *pad_name) {
  GstPad *pad = NULL;
  GstCaps *caps = NULL;

  /* Retrieve pad */
  pad = gst_element_get_static_pad (element, pad_name);
  if (!pad) {
    g_printerr ("Could not retrieve pad '%s'\n", pad_name);
    return;
  }

  /*
   Retrieve negotiated caps i.e., the caps that have linked/fixed (or
   acceptable caps if negotiation is not finished yet)
  */
  caps = gst_pad_get_current_caps (pad);
  /*
   If caps are non-existent(/still inactive imo), retreieve currently acceptable pad caps which are
   the Pad template's caps in the NULL state but might change in later states, as
   the actual hardware capabilities might be queried.
  */
  if (!caps)
    caps = gst_pad_query_caps (pad, NULL);

  /* Print and free */
  g_print ("Caps for the %s pad:\n", pad_name);
  print_caps (caps, "      ");
  gst_caps_unref (caps);
  gst_object_unref (pad);
}

int main(int argc, char *argv[]) {
  GstElement *pipeline, *source, *sink;
  GstElementFactory *source_factory, *sink_factory;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;
  gboolean terminate = FALSE;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  // Create the element factories
  /*
   Untill this app, we initialised elements using `gst_element_factory_make` and
   didn't need to create a factory. A `GstElementFactory` is in charge of
   instantiating a particular type of element identified by its factory name.

   In this tutorial, we follow the longer way of creating elements: we use
   `gst_element_factory_find` to first create a factory (of `videotestsrc` for e.g)
   and then use `gst_element_factory_create` to instantiate multiple elements of
   `videotestsrc`. `gst_element_factory_make()` was a shortcut to this. i.e.,
   `gst_element_factory_make()` = `gst_element_factory_find`+`gst_element_factory_create`
  */

  source_factory = gst_element_factory_find ("audiotestsrc");
  sink_factory = gst_element_factory_find ("autoaudiosink");
  if (!source_factory || !sink_factory) {
    g_printerr ("Not all element factories could be created.\n");
    return -1;
  }

  /* Print information about the pad templates of these factories */
  /*
   Pad template can already be accessed through factories even without an element.
  */
  print_pad_templates_information (source_factory);
  print_pad_templates_information (sink_factory);

  /* Ask the factories to instantiate actual elements */
  source = gst_element_factory_create (source_factory, "source");
  sink = gst_element_factory_create (sink_factory, "sink");

  /* Create the empty pipeline */
  pipeline = gst_pipeline_new ("test-pipeline");

  if (!pipeline || !source || !sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Build the pipeline */
  gst_bin_add_many (GST_BIN (pipeline), source, sink, NULL);
  if (gst_element_link (source, sink) != TRUE) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (pipeline);
    return -1;
  }

  /* Print initial negotiated caps (in NULL state) */
  g_print ("In NULL state:\n");
  print_pad_capabilities (sink, "sink");

  /* Start playing */
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state (check the bus for error messages).\n");
  }

  /* Wait until error, EOS or State Change */
  bus = gst_element_get_bus (pipeline);
  do {
    msg = gst_bus_timed_pop_filtered (bus, 100 * GST_MSECOND,
        GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_DURATION);

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
          /*
           We are only interested in state-changed messages from the pipeline.
           This simply prints the current Pad caps every time the state of the
           pipeline changes.
          */
          if (GST_MESSAGE_SRC (msg) == GST_OBJECT (pipeline)) {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
            g_print ("\nPipeline state changed from %s to %s:\n",
                gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
            /* Print the current capabilities of the sink element */
            print_pad_capabilities (sink, "sink");
          }
          break;
        default:
          /* We should not reach here because we only asked for ERRORs, EOS and STATE_CHANGED */
          g_printerr ("Unexpected message received.\n");
          break;
      }
      gst_message_unref (msg);
    }
  } while (!terminate);

  /* Free resources */
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  gst_object_unref (source_factory);
  gst_object_unref (sink_factory);
  return 0;
}
