/*
Run: gcc bt4-seeking.c -o bt4-seeking `pkg-config --cflags --libs gstreamer-1.0`

In this tutorial, we ask the pipeline if seeking is allowed (some sources like
live streams do not allow) and if yes, once the clip has been running for 10
seconds (this is a requirement), we can skip to a different position using a seek.

In previous tutorials, once the pipeline was setup & running, our main function
just sat and waited to receive an ERROR or EOS through the bus. Here, we modify
this function to periodically wake up and query the pipeline for the stream
position, so we can print it on the screen. This is similar to a media player
updating the UI periodically.

For sake of similplicity, we use `playbin` as the only element.

We implemented the querying part alongwith message parsing. This is why:
  - Querying (whether seeking is possible for the stream, and if yes, in what
  range) can only be done when the pipeline is in PLAY or PAUSE state.
  - Finding the current state of pipeline is possible via the messages received
  from the pipelime.
  - So we parse the messages and derive the state and if we find state to be PLAY
  or PAUSE, we query 😎.
*/
#include <gst/gst.h>

/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData {
  GstElement *playbin;  /* Our one and only element */
  gboolean playing;      /* Are we in the PLAYING state? */
  gboolean terminate;    /* Should we terminate execution? */
  gboolean seek_enabled; /* Is seeking enabled for this media? */
  gboolean seek_done;    /* Have we performed the seek already? */
  gint64 duration;       /* How long does this media last, in nanoseconds */
} CustomData;

/* Forward definition of the message processing function */
static void handle_message (CustomData *data, GstMessage *msg);

int main(int argc, char *argv[]) {
  CustomData data;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;

  data.playing = FALSE;
  data.terminate = FALSE;
  data.seek_enabled = FALSE;
  data.seek_done = FALSE;
  data.duration = GST_CLOCK_TIME_NONE;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the elements */
  data.playbin = gst_element_factory_make ("playbin", "playbin");

  if (!data.playbin) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Set the URI to play */
  g_object_set (data.playbin, "uri", "file:///home/virus/Desktop/media/sintel_trailer-480p.webm", NULL);

  /* Start playing */
  ret = gst_element_set_state (data.playbin, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.playbin);
    return -1;
  }

  /* Listen to the bus */
  bus = gst_element_get_bus (data.playbin);
  do {
    msg = gst_bus_timed_pop_filtered (bus, 100 * GST_MSECOND,
        GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_DURATION);
    /*
     Previously we did not provide a timeout to `gst_bus_timed_pop_filtered` meaning
     that it didn't return until a message was received. But this time, we use a
     100ms timeout so that if no message is received during one tenth of a second
     or 100ms, the function will return NULL meaning that the stream is still
     playing. This logic will help us update the UI.
    */

    // Parse message
    if (msg != NULL) {
      handle_message (&data, msg);
    // Otherwise:
    } else {
      /*
       We got no message, this means the timeout expired and the stream is still
       playing/pipline is still in `PLAYING` state. This is important to figure
       as we can only extract stats from the stream as long it is playing.
      */
      if (data.playing) {
        gint64 current = -1;

        /* Query the current position of the stream */
        if (!gst_element_query_position (data.playbin, GST_FORMAT_TIME, &current)) {
          g_printerr ("Could not query current position.\n");
        }

        /* If we didn't know it yet, query the stream duration */
        if (!GST_CLOCK_TIME_IS_VALID (data.duration)) {
          if (!gst_element_query_duration (data.playbin, GST_FORMAT_TIME, &data.duration)) {
            g_printerr ("Could not query current duration.\n");
          }
        }

        /* Print current position and total duration */
        g_print ("Position %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT "\r",
            GST_TIME_ARGS (current), GST_TIME_ARGS (data.duration));

        /* If seeking is enabled, we have not done it yet, and the time is right, seek */
        if (data.seek_enabled && !data.seek_done && current > 10 * GST_SECOND) {
          g_print ("\nReached 10s, performing seek...\n");
          gst_element_seek_simple (data.playbin, GST_FORMAT_TIME,
              GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, 30 * GST_SECOND);
          data.seek_done = TRUE;
        /*
         A lot of intricacies are hidden behind this function. Lets look the params:
          - GST_SEEK_FLAG_FLUSH: It discards all data currently in the pipeline
            before doing the seek. It might pause a bit while the pipeline is
            refilled and the new data starts to show up but greatly increases the
            responsiveness of the app. If not provided, stale data might show up
            for a while until the new position appears at the end of pipeline.
          - GST_SEEK_FLAG_KEY_UNIT: With most encoded video streams, seeking to
            arbitrary positions is not possible but only to certain frames called:
            'Key Frames'. When this flag is used, the seek will move to the closest
            key frame and start producing data *straight away*. If not used, the
            pipeline will move (internally) to the closest key frame and data will
            be show ONLY when it reaches the requested position. The latter is
            more accurate but might take longer.
          - GST_SEEK_FLAG_ACCURATE: Some media clips do not provide enough indexing
            information, meaning that seeking to arbitrary positions is time
            consuming. In such cases, GStreamer usually estimates the position to
            seek to and usually works just fine. If more precision is needed, then
            we provide this flag, which may also be time-consuming.
          `30 * GST_SECOND` is the time we wish to seek to.
        */
        }
      }
    }
  } while (!data.terminate);

  /* Free resources */
  gst_object_unref (bus);
  gst_element_set_state (data.playbin, GST_STATE_NULL);
  gst_object_unref (data.playbin);
  return 0;
}

// Had to give the error-handling part a function of its own, as it has grown
static void handle_message (CustomData *data, GstMessage *msg) {
  GError *err;
  gchar *debug_info;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error (msg, &err, &debug_info);
      g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
      g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
      g_clear_error (&err);
      g_free (debug_info);
      data->terminate = TRUE;
      break;
    case GST_MESSAGE_EOS:
      g_print ("\nEnd-Of-Stream reached.\n");
      data->terminate = TRUE;
      break;
    case GST_MESSAGE_DURATION:
      /*
       The duration has changed, mark the current one as invalid. This message is
       posted whenever the duration of the stream changes. We mark it invalid here
       so it gets re-queried.
      */
      data->duration = GST_CLOCK_TIME_NONE;
      break;
    case GST_MESSAGE_STATE_CHANGED: {
      /*
       Seeks and time queries generally only get a valid reply when in the
       PAUSED or PLAYING state, since all elements have had a chance to receive
       information and configure themselves. We track this using `data->playing`.
      */
      GstState old_state, new_state, pending_state;
      gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
      if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->playbin)) {
        g_print ("Pipeline state changed from %s to %s:\n",
            gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));

        /* Remember whether we are in the PLAYING state or not */
        data->playing = (new_state == GST_STATE_PLAYING);

        if (data->playing) {
          /* We just moved to PLAYING. Check if seeking is possible */
          GstQuery *query;
          gint64 start, end;
          query = gst_query_new_seeking (GST_FORMAT_TIME);
          /*
           `gst_query_new_seeking()` creates a new object of the "seeking" type,
           with GST_FORMAT_TIME format. This indicates that we are interested in
           seeking by specifying the new time to which we want to move. We could
           also ask for GST_FORMAT_BYTES and then seek to a particular byte
           position but this is normally less useful.

           This query object is then passed to the pipeline with `gst_element_query`
           and the result is stored *in the same query* and retreieved using
           `gst_query_parse_seeking`.
          */
          if (gst_element_query (data->playbin, query)) {
            gst_query_parse_seeking (query, NULL, &data->seek_enabled, &start, &end);
            /*
             `gst_query_parse_seeking` extracts a boolean indicating whether
             seeking is allowed and the range in which its possible.
            */
            if (data->seek_enabled) {
              g_print ("Seeking is ENABLED from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT "\n",
                  GST_TIME_ARGS (start), GST_TIME_ARGS (end));
            } else {
              g_print ("Seeking is DISABLED for this stream.\n");
            }
          }
          else {
            g_printerr ("Seeking query failed.");
          }
          gst_query_unref (query);
        }
      }
    } break;
    default:
      /* We should not reach here */
      g_printerr ("Unexpected message received.\n");
      break;
  }
  gst_message_unref (msg);
}
