## General gstreamer application workflow
```c

```
- Initialise all elements
```c
GstElement *pipeline, *source, *sink;
GstBus *bus;
GstMessage *msg;
GstStateChangeReturn ret;
// etc
```
- Initialize GStreamer
```c
gst_init (&argc, &argv);
```

- Create each element
```code
source = gst_element_factory_make();
```

- Create empty pipeline
```c
pipeline = gst_pipeline_new ("pipeline-name");
// Check for errors in element creation
if (!pipeline || !source || !sink) {
  g_printerr ("Not all elements could be created.\n");
  return -1;
}
```

- Build the pipeline/Add elements to the pipeline
```c
gst_bin_add_many (GST_BIN (pipeline), source, sink, NULL);
```

- Link all elements (and check for errors using status flag)
```c
if (gst_element_link (source, sink) != TRUE) {
  g_printerr ("Elements could not be linked.\n");
  gst_object_unref (pipeline);
  return -1;
}
```

- Modify each elements' property (if & as needed)
```c
g_object_set (source, "pattern", 0, NULL);
```

- Start/Play pipeline (and check for errors)
```c
ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
if (ret == GST_STATE_CHANGE_FAILURE) {
  g_printerr ("Unable to set the pipeline to the playing state.\n");
  gst_object_unref (pipeline);
  return -1;
}
```

- Wait until error or EOS
```c
bus = gst_element_get_bus (pipeline);
msg =
    gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
    GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
```

- Parse message
```C
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
```

- Free resources
```c
gst_object_unref (bus);
gst_element_set_state (pipeline, GST_STATE_NULL);
gst_object_unref (pipeline);
```
