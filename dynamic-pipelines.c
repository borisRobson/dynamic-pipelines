#include <gst/gst.h>
#include <stdio.h>

static GstElement *conv_before;
static GstElement *conv_after;
static GstElement *pipeline;
static GstElement *fsink;
static GstElement *q3;
static GstElement *depay;
static GstElement *src;
static GstElement *mcells;
static GstElement *curr_sink;
static GstElement *mux;
static GstElement *enc;

GstPad *tee_video2_pad, *tee_video_pad;
GstPad *queue_video_pad, *queue_video2_pad;

static void pad_added_handler(GstElement *el_src, GstPad *new_pad, gpointer user_data);

gulong queue_probe;
int count, filecount;

static GstPadProbeReturn queue_data_probe_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{ 
  GstPad *muxpad;
  GstState state;
  
  gst_element_get_state(curr_sink, &state, NULL, GST_CLOCK_TIME_NONE);
  
  if(state == GST_STATE_PLAYING){
    g_print("getting sinkpad\n");    
    muxpad = gst_element_get_static_pad(mux, "video_0");    
    //send eos to elements about to be removed
    gst_pad_send_event(muxpad, gst_event_new_eos());        
    //gst_element_set_state(curr_sink, GST_STATE_READY);    
    gst_element_set_state(curr_sink, GST_STATE_NULL);
    gst_element_set_state(mux, GST_STATE_NULL);
  }  
  //drop data
  return GST_PAD_PROBE_DROP;
}

static gboolean bus_cb (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GMainLoop *loop = user_data;  
  GstPad *qpad;
  char name[12];  
  GstState sinkstate;
  
  qpad = gst_element_get_static_pad(q3, "src");
  //parse bus messages
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      //quit on error
      GError *err = NULL;
      gchar *dbg;
      gst_message_parse_error (msg, &err, &dbg);
      gst_object_default_error (msg->src, err, dbg);
      g_clear_error (&err);
      g_free (dbg);
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_STATE_CHANGED:{      
      GstState old_state, pending_state, new_state;
      gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);                      
      if(GST_OBJECT_NAME(msg->src) == GST_OBJECT_NAME(curr_sink)){        
        g_print("'%s' state changed from %s to %s. \n", GST_OBJECT_NAME(msg->src), gst_element_state_get_name(old_state), gst_element_state_get_name(new_state)); 
        //block data flow as soon as it is ready, this starts it in idle state
        /*if (new_state == GST_STATE_PLAYING && count == 0){
          g_print("blocking fsink\n");            
          queue_probe = gst_pad_add_probe(qpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, queue_data_probe_cb, user_data, NULL);                                                   
        }*/
      } 
      else if(GST_OBJECT_NAME(msg->src) == GST_OBJECT_NAME(pipeline)){
        //show  pipeline messages
        g_print("'%s' state changed from %s to %s. \n", GST_OBJECT_NAME(msg->src), gst_element_state_get_name(old_state), gst_element_state_get_name(new_state)); 
        if (new_state == GST_STATE_PLAYING && count == 0){
          g_print("blocking fsink\n");            
          queue_probe = gst_pad_add_probe(qpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, queue_data_probe_cb, user_data, NULL);                                                   
        }

      }
      else if(GST_OBJECT_NAME(msg->src) == GST_OBJECT_NAME(src)){
        //show src messages
        g_print("'%s' state changed from %s to %s. \n", GST_OBJECT_NAME(msg->src), gst_element_state_get_name(old_state), gst_element_state_get_name(new_state)); 
      }          
      else if(GST_OBJECT_NAME(msg->src) == GST_OBJECT_NAME(mux)){
        //show mux messages
        g_print("'%s' state changed from %s to %s. \n", GST_OBJECT_NAME(msg->src), gst_element_state_get_name(old_state), gst_element_state_get_name(new_state)); 
      }   
      break;
    }
    default:
      if(GST_OBJECT_NAME(msg->src) == GST_OBJECT_NAME(mcells)) {
        /*
          parse motioncells messages
          messages sent on motion detected and motion stopped
        */
        //break if initial probe not in place - causes error if motion detected before probe in place
        gst_element_get_state(curr_sink, &sinkstate, NULL, GST_CLOCK_TIME_NONE);
        if(sinkstate == GST_STATE_PLAYING && count < 1)
          break;

        count++;   
        if(count % 2 == 1 && queue_probe !=0){
          //motin detected, remove probe to allow data flow
          g_print("Motion Detected\n");
          g_print("removing probe: '%lu'\n", queue_probe);    
          gst_pad_remove_probe(qpad, queue_probe);
          //create and set filename
          gst_bin_remove(GST_BIN(pipeline), curr_sink);
          gst_bin_remove(GST_BIN(pipeline), mux);
          filecount++;
          sprintf(name, "file%d.mp4", filecount) ;
          mux = gst_element_factory_make("mp4mux", NULL);
          curr_sink = gst_element_factory_make("filesink", NULL);
          g_object_set(curr_sink, "location", name, NULL);
          //add new source to pipeline
          gst_bin_add_many(GST_BIN(pipeline), mux, curr_sink,NULL);

          g_print("Attempting link\n");
          //link and set to play
          if(!gst_element_link_many(enc, mux, curr_sink,NULL)){
            g_printerr("could not relink mux\n");
            g_main_loop_quit (loop);
            break;
          }

          gst_element_set_state(mux, GST_STATE_PLAYING);  
          gst_element_set_state(curr_sink, GST_STATE_PLAYING);            
        }else{
          //motion stopped, bock data flow
          g_print("Motion Stopped\n");                  
          queue_probe = gst_pad_add_probe(qpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, queue_data_probe_cb, user_data, NULL);  
          g_print("adding new-probe: '%lu'\n", queue_probe);            
        }
      }
      break;
  }
  return TRUE;
}

int
main (int argc, char **argv)
{
  GMainLoop *loop;
  GstElement  *q1, *q2, *sink, *tee;
  GstElement  *parse, *decode;
  GstPadTemplate *tee_src_pad_template, *mux_src_template;  
  GstPad  *encpad, *muxpad;   
  GstElement *filter;

  gst_init(&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  //create elements 
  pipeline = gst_pipeline_new ("pipeline");

  q1 = gst_element_factory_make ("queue", "q1");
  q2 = gst_element_factory_make ("queue", "q2");
  q3 = gst_element_factory_make ("queue", "q3");
  g_object_set(q3, "flush-on-eos", TRUE,"max-size-buffers", "0", "max-size-bytes", "0", NULL);
  mcells = gst_element_factory_make("motioncells", "mcells");
  
  conv_before = gst_element_factory_make ("videoconvert", NULL);
  conv_after = gst_element_factory_make ("videoconvert", NULL);  

  src = gst_element_factory_make("rtspsrc", "src");
  /*
    rtspsrc cannot be directly linked. this callback will negotiate pad caps 
    and create link
  */
  g_signal_connect(src, "pad-added", G_CALLBACK(pad_added_handler), loop);
  g_object_set(src, "location", argv[1], NULL);

  depay = gst_element_factory_make("rtph264depay", "depay");
  parse = gst_element_factory_make("h264parse", "parse");
  decode = gst_element_factory_make("avdec_h264", "decode");

  filter = gst_element_factory_make("capsfilter", NULL);
  gst_util_set_object_arg (G_OBJECT (filter), "caps",
    "video/x-raw,format=I420, framerate, GST_TYPE_FRACTION,10,1");
  
  //sink = gst_element_factory_make ("xvimagesink", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);

  fsink = gst_element_factory_make ("fakesink", NULL);

  curr_sink = fsink;

  enc = gst_element_factory_make("avenc_mpeg4", "enc");

  mux = gst_element_factory_make("mp4mux", "mux");

  tee = gst_element_factory_make("tee", "t");

  filecount = 0;

  gst_bin_add_many (GST_BIN (pipeline), src, depay, parse, decode, q1, conv_before, mcells, conv_after, tee, q2, sink,q3,filter, enc, mux,fsink, NULL);

  //link all but source
  //main branch
  gst_element_link_many (depay, parse, decode, q1, tee,NULL);

  //tee0
  gst_element_link_many(q2,  conv_before, mcells, conv_after, sink, NULL);

  //tee1
  gst_element_link_many(q3,filter, enc, NULL);

  //Get enc -> mux pad and link
  mux_src_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(mux), "video_%u");
  muxpad = gst_element_request_pad(mux, mux_src_template, NULL, NULL);
  g_print("recieved mux pad '%s'\n", gst_pad_get_name(muxpad));
  encpad = gst_element_get_static_pad(enc, "src");
  if(gst_pad_link(encpad, muxpad) != GST_PAD_LINK_OK){
    g_printerr("could not link mux\n");
    gst_object_unref(pipeline);
    return -1;
  }else{    
    gst_element_link(mux, curr_sink);
  }

  //Get tee -> queue pads and link
  tee_src_pad_template = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (tee), "src_%u");
  tee_video_pad = gst_element_request_pad(tee, tee_src_pad_template, NULL, NULL);
  tee_video2_pad = gst_element_request_pad(tee, tee_src_pad_template, NULL, NULL);

  g_print("recieved tee pads: '%s' , and '%s'\n", gst_pad_get_name(tee_video_pad), gst_pad_get_name(tee_video2_pad));
  queue_video_pad = gst_element_get_static_pad(q2, "sink");
  queue_video2_pad = gst_element_get_static_pad(q3, "sink");
  g_print("recieved queue pads: '%s' , and '%s'\n", gst_pad_get_name(queue_video_pad), gst_pad_get_name(queue_video2_pad));

  if(gst_pad_link(tee_video_pad, queue_video_pad) != GST_PAD_LINK_OK ||
    gst_pad_link(tee_video2_pad, queue_video2_pad) != GST_PAD_LINK_OK){
      g_printerr("could not link tee\n");
      gst_object_unref(pipeline);
      return -1;
    }

  //start playing
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  gst_bus_add_watch (GST_ELEMENT_BUS (pipeline), bus_cb, loop);

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  return 0;
}


static void pad_added_handler(GstElement *el_src, GstPad *new_pad, gpointer user_data){
  GstPadLinkReturn ret;
  GstEvent *event;
  GstPad *depay_pad;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;
  GstCaps *filter = NULL;

  g_print("Received new pad '%s' from '%s'\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(el_src));
  //if pad already linked, nothing to do
  if(gst_pad_is_linked(new_pad)){
    g_print(" pad linked, ignoring...\n");
    goto exit;
  }

  //get the correct depay pad
  depay_pad =  gst_element_get_static_pad(depay, "sink");  

  filter = gst_caps_from_string("application/x-rtp");
  new_pad_caps = gst_pad_query_caps(new_pad, filter);

  //send reconfigure event
  event = gst_event_new_reconfigure();
  gst_pad_send_event(new_pad, event);

  //check new pad type
  new_pad_struct = gst_caps_get_structure(new_pad_caps,0);
  new_pad_type = gst_structure_get_name(new_pad_struct);
  if(!g_str_has_prefix(new_pad_type, "application/x-rtp")){
    g_print(" Type: '%s', looking for rtp. Ignoring\n",new_pad_type);
    goto exit;
  }

  //attempt to link
  g_print("Attempting to link source pad '%s' to sink pad '%s'\n",GST_PAD_NAME(new_pad), GST_PAD_NAME(depay_pad));
  ret = gst_pad_link(new_pad, depay_pad);
  if(GST_PAD_LINK_FAILED(ret)){
    g_print(" Type is: '%s' but link failed.\n", new_pad_type);
  }else{
    g_print(" Link Succeeded (type: '%s')\n", new_pad_type); 
    g_object_set(src, "latency", "0", NULL);   
  } 

  exit:
    //unref new pad caps if required
    if(new_pad_caps != NULL){
      gst_caps_unref(new_pad_caps);
    }
    //unref depay pad
    gst_object_unref(depay_pad);    
}