/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <gst/gst.h>
#include <glib-unix.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <dlfcn.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <curl/curl.h>
#include <media/drm/DrmAPI.h>
#include <utils/Vector.h>

using namespace std;
using namespace android;

#define DASH_LINE  "--------------------------------------------------"
#define SPACE "                                                       "

// manifest will be downloaded here
#define MANIFEST_DOWNLOAD_PATH "/data/manifest.xml"

#define DRM_LIB_PATH "/usr/lib/libprdrmengine.so"

// type : PERSIST_FALSE_SECURESTOP_FALSE_SL150
#define CONTENT_TYPE "Content-Type: text/xml; charset=utf-8"
#define SOAP_ACTION "SOAPAction: ""\"http://schemas.microsoft.com/DRM/2007/03/protocols/AcquireLicense\""
#define LA_URL "https://test.playready.microsoft.com/service/rightsmanager.asmx?cfg=(securestop:false,persist:false,sl:150)"

// DRM UUIDs
#define PLAYREADY_UUID "urn:uuid:9a04f079-9840-4286-ab92-e65be0885f95"
#define WIDEVINE_UUID "urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed"

// playready uuid in hex
const uint8_t pr_uuid[16] = {
  0x9A, 0x04, 0xF0, 0x79, 0x98, 0x40, 0x42, 0x86,
  0xAB, 0x92, 0xE6, 0x5B, 0xE0, 0x88, 0x5F, 0x95
};

// menu options
#define PLAY "p"
#define STOP "s"
#define QUIT "q"

// for inter-thread communication
#define STDIN_MESSAGE "stdin"
#define TERMINATE_MESSAGE "terminate"

#define OPENING_TAG_HLS "#EXTM3U"
#define OPENING_TAG_DASH "<?xml"


typedef long PRDRM_RESULT;
typedef enum _PRDRMResults {
  PRDRM_SUCCESS                       = ((PRDRM_RESULT)0x00000000L),
  PRDRM_FAILED                        = ((PRDRM_RESULT)0x70040001L),
  PRDRM_BAD_VALUE                     = ((PRDRM_RESULT)0x70040003L),
} PRDRMResults;

typedef struct _DrmPlayer DrmPlayer;
struct _DrmPlayer
{
  // GStreamer pipeline instance
  GstElement *pipeline;

  // main application event loop
  GMainLoop *loop;

  // queue for asynchronous communication b/w threads
  GAsyncQueue *queue;

  // bus event source id
  guint bus_watch_id;

  // IO event source id
  guint stdin_watch_id;

  // interrupt source id
  guint interrupt_watch_id;

  // current state of pipeline
  GstState current_state;

  // state the pipeline is desired to switch to after buffering is done
  GstState desired_state;

  // boolean variable indicating whether the pipeline is buffering
  gboolean buffering;

  // boolean variable indicating whether the pipeline is live
  gboolean live;

  // content dash or hls
  gboolean is_dash;

  // content encrypted or clear
  gboolean encrypted;

  // content has playready-encrypted license
  gboolean is_playready;

  // content has widevine-encrypted license
  gboolean is_widevine;

  // handle for prdrmengine library
  void *lib_handle;

  // DRMFactory object instance
  DrmFactory *drm_factory;

  // DRMPlugin object instance
  DrmPlugin *drm_plugin;

  // session id returned after opening DRM session
  string drm_session_id;

  // playready object header
  gchar *pro_header;

  // license challenge used to request license
  string la_request;

  // license response returned by license server
  string la_response;
};

// to store license request and response data
struct soapbuf {
  gchar *pdata;
  size_t sdata;
};

void
str_to_vec (string s, Vector<uint8_t> & v)
{
  v.appendArray(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

string
vec_to_str (Vector<uint8_t> v)
{
  string s (v.begin(), v.end());
  return s;
}

PRDRM_RESULT
init_playready (DrmPlayer *player)
{
  // For PR3.0 and above
  // Load library
  gchar *libpath = (gchar *) DRM_LIB_PATH;
  PRDRM_RESULT ret;

  void *handle = NULL;
  g_print ("Trying to load %s\n", libpath);
  handle = dlopen (libpath, RTLD_NOW);
  if (handle == NULL) {
    g_printerr ("Cannot load library, dlerror = %s\n", dlerror());
    return PRDRM_FAILED;
  }  else {
    g_print ("Library loaded successfully\n");
    player->lib_handle = handle;
  }

  // Create DRMFactory object
  typedef DrmFactory *(*createDrmFactoryFunc)();
  createDrmFactoryFunc createDrmFactory =
  (createDrmFactoryFunc) dlsym (handle, "createDrmFactory");

  DrmFactory *drm_factory = createDrmFactory();
  if (drm_factory == NULL) {
    gchar *err = NULL;
    if ((err = dlerror()) != NULL) {
      g_printerr ("Cannot find symbol, dlerror = %s\n", err);
      g_free (err);
    }
    return PRDRM_FAILED;
  }

  if (!drm_factory->isCryptoSchemeSupported (pr_uuid)) {
    g_printerr ("ERROR: Check given PR UUID\n");
    return PRDRM_FAILED;
  } else {
    g_print ("Created DRMFactory\n");
    player->drm_factory = drm_factory;
  }

  // Create DRMPlugin object
  ret = drm_factory->createDrmPlugin (pr_uuid, &player->drm_plugin);
  if (ret != PRDRM_SUCCESS) {
    g_printerr ("ERROR: Couldn't create DrmPlugin \n");
    return ret;
  } else {
    g_print ("Created DrmPlugin\n");
  }

  // Open DRM session
  Vector<uint8_t> session_id;
  if (ret = player->drm_plugin->openSession (session_id) != PRDRM_SUCCESS) {
    g_printerr ("ERROR: Couldn't create session \n");
    return ret;
  } else {
    string sid (session_id.begin(), session_id.end());
    g_print ("Opened DRM Session with session ID %s\n", sid.c_str());
    player->drm_session_id.assign (sid);
  }
  return ret;
}

gint
init_widevine ()
{
  return 1;
}

gboolean
wait_stdin_message (GAsyncQueue *queue, gchar **input)
{
  GstStructure *message = NULL;

  // clear input from previous use
  g_free (*input);
  *input = NULL;

  // block the thread until the queue is empty
  // function g_async_queue_pop performs the blocking
  // as soon as message is available in the queue, it is popped and the body of the loop is executed
  // keep executing the loop till eos/error msg or user input is provided
  while ((message = (GstStructure *) g_async_queue_pop (queue)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      // returning FALSE will cause menu thread to terminate
      return FALSE;
    }

    if (gst_structure_has_name (message, STDIN_MESSAGE)) {
      *input = g_strdup (gst_structure_get_string (message, "input"));
      break;
    }
    gst_structure_free (message);
  }
  gst_structure_free (message);
  return TRUE;
}

// WRITEFUNCTION callback for curl for fetching manifest
size_t
write_data (void *ptr, size_t size, size_t nmemb, void *stream)
{
  size_t written = fwrite (ptr, size, nmemb, (FILE *)stream);
  return written;
}

CURLcode
fetch_manifest (gchar *manifest_url)
{
  CURL *curl = NULL;
  FILE *fp;
  CURLcode res = CURLE_FAILED_INIT;
  gchar outfilename[FILENAME_MAX] = MANIFEST_DOWNLOAD_PATH;

  g_print ("Trying to fetch manifest from the url %s...\n", manifest_url);

  if (curl_global_init (CURL_GLOBAL_ALL) != CURLE_OK) {
    g_printerr ("Curl global init failed.\n");
    return res;
  }
  if ((curl = curl_easy_init ()) == NULL) {
    g_printerr ("Curl easy init failed\n");
    goto init_error;
  }

  if ((fp = fopen (outfilename, "wb")) == NULL) {
    g_printerr ("Couldn't open file for output\n");
    goto io_error;
  }

  // Uncomment this line to print curl outputs
  // curl_easy_setopt (curl, CURLOPT_VERBOSE, 1L);
  curl_easy_setopt (curl, CURLOPT_URL, manifest_url);
  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_data);
  curl_easy_setopt (curl, CURLOPT_WRITEDATA, fp);
  res = curl_easy_perform (curl);
  fclose (fp);

  if (res != CURLE_OK) {
    g_printerr ("Curl error %d\n", res);
  } else {
    g_print ("Manifest downloaded and saved to %s\n", MANIFEST_DOWNLOAD_PATH);
  }

io_error:
  curl_easy_cleanup (curl);
init_error:
  curl_global_cleanup ();
  return res;
}

PRDRM_RESULT
create_license_request (DrmPlayer *player)
{
  PRDRM_RESULT ret;
  Vector<uint8_t> init_data;
  Vector<uint8_t> request;
  Vector<uint8_t> session_id;
  String8 mime_type;
  DrmPlugin::KeyType key_type = DrmPlugin::kKeyType_Streaming;
  KeyedVector<String8, String8> const optional_parameters;
  String8 default_url;
  DrmPlugin::KeyRequestType key_request_type;

  // Decode base64 encoded PR object
  gsize out_len;
  guchar *decoded_str = g_base64_decode (player->pro_header, &out_len);
  uint8_t header[out_len];
  for (int i=0; i<out_len; i++) {
    header[i] = (uint8_t) decoded_str[i];
  }
  g_free (decoded_str);

  init_data.appendArray (header, out_len);
  str_to_vec (player->drm_session_id, session_id);

  g_print ("Creating license request...\n");
  if ((ret = player->drm_plugin->getKeyRequest (session_id, init_data, mime_type, key_type, optional_parameters, request, default_url, &key_request_type)) == PRDRM_SUCCESS) {
    g_print ("License request created successfully.\n");
    player->la_request = vec_to_str (request);
  }
  return ret;
}

// WRITEFUNCTION callback for curl for fetching license
size_t
soap_callback (gchar *buffer,
    size_t size, size_t nitems, void *outstream)
{
  struct soapbuf *userp = (soapbuf *) outstream;
  size_t s = size*nitems;

  userp->pdata = (gchar *) realloc (userp->pdata, userp->sdata + s + 1);
  if (userp->pdata == NULL) {
    g_printerr ("Memory allocation failed\n");
    return 0;
  }
  memcpy (userp->pdata + userp->sdata, buffer, s);
  userp->sdata += s;
  userp->pdata [userp->sdata] = '\0';

  return s;
}

gint
acquire_license (gchar *url, struct curl_slist *http_header, gchar *content_type, gchar **post_data, size_t *post_data_size)
{
  CURL *curl = NULL;
  glong response_code = -1;
  gint ret = -1;
  struct soapbuf soapbuf;

  if (post_data == NULL || *post_data_size == 0)
    return ret;

  // Init curl
  if (curl_global_init (CURL_GLOBAL_ALL) != CURLE_OK) {
    g_printerr ("Curl global init failed.\n");
    return ret;
  }
  if ((curl = curl_easy_init()) == NULL) {
    g_printerr ("Curl easy init failed.\n");
    goto init_error;
  }

  http_header = curl_slist_append (http_header, content_type);
  curl_easy_setopt (curl, CURLOPT_URL, url);
  curl_easy_setopt (curl, CURLOPT_HTTPHEADER, http_header);
  curl_easy_setopt (curl, CURLOPT_POST, 1L);
  curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, *post_data_size);
  curl_easy_setopt (curl, CURLOPT_POSTFIELDS, *post_data);

  soapbuf.pdata = *post_data;
  soapbuf.sdata = 0;

  curl_easy_setopt (curl, CURLOPT_WRITEDATA, &soapbuf);
  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, soap_callback);
  // Uncomment this line to print curl outputs
  // curl_easy_setopt (curl, CURLOPT_VERBOSE, 1L);

  g_print ("Acquiring license from server...\n");
  if ((ret = curl_easy_perform (curl)) != CURLE_OK) {
    g_printerr ("Curl error %d\n", ret);
    goto curl_error;
  }

  curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &response_code);
  if (response_code != 200) {
    g_printerr ("Response error: %ld", response_code);
    ret = -1;
    goto curl_error;
  }

  // response data
  *post_data = soapbuf.pdata;
  *post_data_size = soapbuf.sdata;

curl_error:
  curl_slist_free_all (http_header);
  curl_easy_cleanup (curl);
init_error:
  curl_global_cleanup();
  return ret;
}

PRDRM_RESULT
create_soap_request (DrmPlayer *player)
{
  PRDRM_RESULT ret;
  gchar *content_type = (gchar *) CONTENT_TYPE;
  gchar *url = (gchar *) LA_URL;
  struct curl_slist *http_header = NULL;
  http_header = curl_slist_append (http_header, SOAP_ACTION);

  if (player->la_request.empty()) {
    g_printerr ("License request object is empty.\n");
    return PRDRM_FAILED;
  }

  gchar *req_buf = NULL;
  size_t req_buf_size = (size_t) player->la_request.length();
  req_buf = (gchar*) malloc (req_buf_size);
  memcpy (req_buf, player->la_request.c_str(), req_buf_size);

  if ((ret = acquire_license (url, http_header, content_type, &req_buf, &req_buf_size)) == PRDRM_SUCCESS) {
    g_print ("License acquired from license server successfully.\n");
    player->la_response.assign (req_buf, req_buf+req_buf_size);
  }

  free (req_buf);
  return ret;
}

PRDRM_RESULT
provide_key_response (DrmPlayer *player)
{
  PRDRM_RESULT ret;
  Vector<uint8_t> req_id;
  Vector<uint8_t> session_id;
  Vector<uint8_t> response;
  str_to_vec (player->drm_session_id, session_id);
  str_to_vec (player->la_response, response);

  if ((ret = player->drm_plugin->provideKeyResponse (session_id, response, req_id)) == PRDRM_SUCCESS) {
    // player->key_set_id = vec_to_str (req_id);
    g_print ("Provided license response to DRMPlugin successfully.\n");
  }

  return ret;
}

gchar*
decide_dash_or_hls (DrmPlayer *player)
{
  gchar *content;

  // read the manifest file
  FILE *f;
  if ((f = fopen (MANIFEST_DOWNLOAD_PATH, "r")) == NULL) {
    g_printerr ("Error opening manifest file!\n");
    return NULL;
  }
  fseek (f, 0, SEEK_END);
  glong fsize = ftell(f);
  fseek (f, 0, SEEK_SET);

  content = (gchar *) malloc (fsize + 1);
  fread (content, fsize, 1, f);
  fclose (f);
  content[fsize] = 0;
  content = g_strstrip (content);

  // if <?xml then DASH, if m3u8 then HLS
  if (g_str_has_prefix (content, OPENING_TAG_HLS)) {
    g_print("Parsing manifest.....it's HLS\n");
    player->is_dash = FALSE;
  } else if (g_str_has_prefix (content, OPENING_TAG_DASH)) {
    g_print("Parsing manifest.....it's DASH\n");
  }

  return content;
}

xmlNodePtr
find_xml_sibling_with_name (xmlNodePtr node, gchar *child_name)
{
  xmlNodePtr cur = node->next;
  while (cur != NULL) {
    if ((!xmlStrcmp(cur->name, (const xmlChar *)child_name)))
      return cur;
    cur = cur->next;
  }
  return NULL;
}

xmlNodePtr
find_xml_child_with_name (xmlNodePtr root, gchar *child_name)
{
  xmlNodePtr cur = root->xmlChildrenNode;
  while (cur != NULL) {
    if ((!xmlStrcmp(cur->name, (const xmlChar *)child_name)))
      return cur;
    cur = cur->next;
  }
  return NULL;
}

PRDRM_RESULT
playready_usecase (DrmPlayer *player)
{
  PRDRM_RESULT ret;
  if ((ret = init_playready (player)) != PRDRM_SUCCESS) {
    g_printerr ("PlayReady session init failed.\n");
    return ret;
  }

  if ((ret = create_license_request (player)) != PRDRM_SUCCESS) {
    g_printerr ("Creation of license request failed.\n");
    return ret;
  }

  if ((ret = create_soap_request (player)) != PRDRM_SUCCESS) {
    g_printerr ("Creation of soap request failed.\n");
    return ret;
  }

  if ((ret = provide_key_response (player)) != PRDRM_SUCCESS) {
    g_printerr ("Provide key response failed.\n");
    return ret;
  }

  return ret;
}

gboolean
parse_dash_protection_tag (DrmPlayer* player, xmlNodePtr node)
{
  gboolean found_uuid = FALSE;
  xmlNodePtr child_node = node->xmlChildrenNode;

  // Parse AdaptationSet's children to find ContentProtection tag
  while (child_node != NULL) {
    if ((!xmlStrcasecmp (child_node->name, (const xmlChar *)"ContentProtection"))) {
      player->encrypted = TRUE;
      g_print ("Found ContentProtection tag, it's encrypted content..\n");

      // ContentProtection tag has property schemeIdUri with uuid
      xmlChar *schemeIdUri = xmlGetProp (child_node, (const xmlChar *)"schemeIdUri");
      if (xmlStrstr (schemeIdUri, (const xmlChar *)"uuid") != NULL) {
        if (!xmlStrcasecmp (schemeIdUri, (const xmlChar *) PLAYREADY_UUID)) {
          // PlayReady
          found_uuid = TRUE;
          player->is_playready = TRUE;
          g_print ("Found PlayReady UUID\n");

          // Parse PR header
          xmlNodePtr cur;
          if ((cur = find_xml_child_with_name (child_node, (gchar *)"pro")) != NULL) {
            player->pro_header = (gchar *) xmlNodeGetContent (cur);
          } else {
            g_printerr ("ERROR: Didn't find PlayReady header!\n");
            return FALSE;
          }
        } else if (!xmlStrcasecmp (schemeIdUri, (const xmlChar *)WIDEVINE_UUID)) {
          // Widevine
          found_uuid = TRUE;
          player->is_widevine = TRUE;
          g_print ("Found Widevine UUID\n");
        }
      }
      xmlFree (schemeIdUri);
    }
    child_node = child_node->next;
  }
  return found_uuid;
}

gboolean
parse_dash_manifest (DrmPlayer *player)
{
  xmlNodePtr root, period, adapset;
  xmlDocPtr doc = xmlParseFile (MANIFEST_DOWNLOAD_PATH);
  gboolean found_uuid = FALSE, ret = FALSE;

  g_print ("Parsing XML document...\n");

  if (doc == NULL) {
    g_printerr ("Document not parsed successfully. \n");
    return FALSE;
  }

  root = xmlDocGetRootElement (doc);
  if (root == NULL) {
    g_printerr ("Empty document.\n");
    goto error;
  }

  if (xmlStrcmp (root->name, (const xmlChar *) "MPD")) {
    g_printerr ("Document of the wrong type, root node != MPD\n");
    goto error;
  }

  // Manifest is supposed to have Period tag with one/multiple AdaptationSets as children
  if ((period = find_xml_child_with_name (root, (gchar *)"Period")) != NULL) {
    if ((adapset = find_xml_child_with_name (period, (gchar *)"AdaptationSet")) != NULL) {
      found_uuid = parse_dash_protection_tag (player, adapset);
    } else {
      g_printerr ("Couldn't find AdaptationSet tag\n");
      goto error;
    }
  } else {
    g_printerr ("Couldn't find Period tag\n");
    goto error;
  }
  // TODO: What about next periods?
  // while (period != NULL) {
  //   adapset = find_xml_child_with_name (period, (gchar *)"AdaptationSet");
  //   found_uuid = parse_dash_protection_tag (player, adapset);
  //   period = find_xml_sibling_with_name (period, (gchar *)"Period");
  // }

  g_print ("Document parsed successfully.\n");

  if (player->encrypted) {
    if (!found_uuid) {
      g_printerr ("ERROR: Neither PlayReady nor Widevine! Can't proceed...\n");
      goto error;
    }

    if (player->is_playready && player->is_widevine) {
      gchar choice;

      while (TRUE) {
        g_print ("The selected content can be played with PlayReady as well as Widevine.\nPlease enter '1' for PlayReady or '2' for Widevine: " );
        choice = fgetc (stdin);

        if (choice == '1') {
          if (playready_usecase (player) != PRDRM_SUCCESS)
            goto error;
          break;
        } else if (choice == '2') {
          // Call Widevine APIs
          g_print ("Not doing anything for Widevine yet!\n");
          break;
        }
      }
    } else if (player->is_playready) {
      // Call PlayReady APIs
      if (playready_usecase (player) != PRDRM_SUCCESS)
       goto error;
    } else {
      // Call Widevine APIs
      g_print ("Not doing anything for Widevine yet!\n");
    }
  }
  ret = TRUE;

error:
  // free doc and all descendent nodes recursively
  xmlFreeDoc (doc);
  return ret;
}

gboolean
split_str (gchar** input_str, const gchar *delim, gint num_of_splits, gint output_index)
{
  gchar **split_str = g_strsplit (*input_str, delim, num_of_splits);
  if (g_strv_length (split_str) != num_of_splits) {
    g_free (*input_str);
    return FALSE;
  }

  *input_str = NULL;
  *input_str = split_str[output_index];
  g_strstrip (*input_str);
  return TRUE;
}

gboolean
parse_hls_protection_tag (DrmPlayer* player, gchar **split_content, gint index)
{
  gboolean found_uuid = FALSE;
  gchar *method = NULL, *keyformat = NULL, *uri = NULL;
  // EXT-X-KEY tag contains the decryption info for all the media segments
  // that follow it
  for (int i = index; i >= 0; i--) {
    if (g_str_has_prefix (split_content[i], "#EXT-X-KEY") || g_str_has_prefix (split_content[i], "#EXT-X-SESSION-KEY")) {
      if ((method = g_strrstr (split_content[i], "METHOD=")) != NULL) {
        if (!split_str (&method, "=", 2, 1))
          continue;

        if (g_str_has_prefix (method, "NONE")) {
          g_free (method);
          continue;
        }
        g_free (method);

        player->encrypted = TRUE;
        g_print ("Found key tag, it's encrypted content..\n");

        if ((keyformat = g_strrstr (split_content[i], "KEYFORMAT=")) != NULL) {
          if (!split_str (&keyformat, "=", 2, 1))
            continue;

          if (!split_str (&keyformat, "\"", 3, 1))
            continue;

          if (g_str_equal (keyformat, "com.microsoft.playready") || g_str_equal (keyformat, PLAYREADY_UUID)) {
            g_free (keyformat);
            if (player->is_playready)
              continue;
            g_print ("Found PlayReady UUID\n");
            found_uuid = TRUE;
            player->is_playready = TRUE;

            // Parse PR header
            if ((uri = g_strrstr (split_content[i], "URI=")) != NULL) {
              if (!split_str (&uri, "=", 2, 1))
                continue;

              if (!split_str (&uri, "\"", 3, 1))
                continue;

              if (!split_str (&uri, ",", 2, 1))
                continue;

              player->pro_header = g_strdup (uri);
              g_free (uri);
            }
          } else if (g_str_equal (keyformat, "com.widevine") || g_str_equal (keyformat, WIDEVINE_UUID)) {
            g_free (keyformat);
            if (player->is_widevine)
              continue;
            g_print ("Found Widevine UUID\n");
            found_uuid = TRUE;
            player->is_widevine = TRUE;
          }
        }
      }
    } else if (g_str_has_prefix (split_content[i], "#EXT-X-PLAYREADYHEADER")) {
      if (player->is_playready)
        continue;

      g_print ("Found key tag, it's encrypted content..\n");
      player->encrypted = TRUE;
      g_print ("Found PlayReady UUID\n");
      found_uuid = TRUE;
      player->is_playready = TRUE;

      // Parse PR header
      uri = split_content[i];
      if (!split_str (&uri, ":", 2, 1))
        continue;

      player->pro_header = g_strdup (uri);
      g_free (uri);
    }
  }

  return found_uuid;
}

gboolean
parse_hls_manifest (DrmPlayer *player, gchar *manifest_content)
{
  gboolean found_uuid = FALSE;
  if (manifest_content == NULL)
    return FALSE;

  // split_content array stores each line of the manifest as its elements
  gchar **split_content = g_strsplit (manifest_content, "\n", -1);

  int i;
  for (i=0; i<g_strv_length (split_content); i++) {
    // EXT-X-STREAM-INF tag specifies a stream, which is a set
    // of renditions which can be combined to play
    if (g_str_has_prefix (split_content[i], "#EXT-X-STREAM-INF")) {
      gchar *codec = NULL;
      if ((codec = g_strrstr (split_content[i], "CODECS")) != NULL) {
        if (!split_str (&codec, "=", 2, 1))
          continue;

        if (!split_str (&codec, "\"", 3, 1))
          continue;

        // Select the first stream which has codec avc or hevc
        if (g_str_has_prefix (codec, "avc") || g_str_has_prefix (codec, "hevc")) {
          g_print ("Selecting codec %s stream to play\n", codec);
          g_free (codec);
          break;
        }
        g_free (codec);
      }
    }
  }
  if (i >= g_strv_length (split_content)) {
    g_printerr ("Didn't find any playable stream in the content\n");
    g_strfreev (split_content);
    return FALSE;
  }

  found_uuid = parse_hls_protection_tag (player, split_content, i);
  g_strfreev (split_content);
  g_print ("Document parsed successfully.\n");

  if (player->encrypted) {
    if (!found_uuid) {
      g_printerr ("ERROR: Neither PlayReady nor Widevine! Can't proceed...\n");
      return FALSE;
    }

    if (player->is_playready && player->is_widevine) {
      while (TRUE) {
        gchar choice;
        g_print ("The content can be played with PlayReady as well as Widevine.\nPlease enter 'p' for PlayReady or 'w' for Widevine: " );
        choice = fgetc (stdin);

        if (choice == '1') {
          // Call PlayReady APIs
          if (playready_usecase (player) != PRDRM_SUCCESS)
            return FALSE;
          break;
        } else if (choice == '2') {
          // Call Widevine APIs
          g_print ("Not doing anything for Widevine yet!\n");
          break;
        }
      }
    } else if (player->is_playready) {
      // Call PlayReady APIs
      if (playready_usecase (player) != PRDRM_SUCCESS)
       return FALSE;
    } else {
      // Call Widevine APIs
      g_print ("Not doing anything for Widevine yet!\n");
    }
  }

  return TRUE;
}

// Play/pause playback as per user's command
void
toggle_play (DrmPlayer *player)
{
  player->desired_state = (player->current_state == GST_STATE_PLAYING) ? GST_STATE_PAUSED : GST_STATE_PLAYING;

  // if buffering, state change will happen after buffering has finished
  if (!player->buffering) {
    if (gst_element_set_state (player->pipeline, player->desired_state) == GST_STATE_CHANGE_FAILURE) {
      g_print ("Couldn't toggle state! %.30s \n", SPACE);
    } else {
      (player->desired_state == GST_STATE_PLAYING) ? g_print ("Playing... %.30s\n", SPACE) : g_print ("Paused %.30s\n", SPACE);
    }
    player->desired_state = player->current_state;
  } else {
    g_print ("Pipeline is buffering, will toggle state when done\n");
  }
}

// Stop playback as per user's command
void
stop (DrmPlayer *player)
{
  if (player->current_state == GST_STATE_NULL || player->current_state == GST_STATE_READY) {
    g_print ("Already in stopped state! %.30s\n", SPACE);
    return;
  }
  if (gst_element_set_state (player->pipeline, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE)
    g_print ("Couldn't stop! %.30s \n", SPACE);
  else {
    g_print ("Stopped %.30s\n", SPACE);
    player->desired_state = GST_STATE_PAUSED;
  }
}

GstElement*
create_pipeline (DrmPlayer *player, gchar *pipeline_des)
{
  GstElement *pipeline;
  GError *error = NULL;

  g_print ("\nCreating pipeline %s %.30s\n", pipeline_des, SPACE);
  pipeline = gst_parse_launch ((const gchar *) pipeline_des, &error);

  if (error != NULL) {
    g_printerr ("ERROR: %s\n", GST_STR_NULL (error->message));
    g_clear_error (&error);
    return NULL;
  }

  // if it is a single element, pipeline creation will be successful but it
  // won't have a bus and thus won't receive error message
  // so insert the pipeline inside a parent pipeline to get access to the bus
  if (!GST_IS_PIPELINE (pipeline)) {
    GstElement *parent_pipeline = gst_element_factory_make ("pipeline", NULL);
    gst_bin_add (GST_BIN (parent_pipeline), pipeline);
    pipeline = parent_pipeline;
  }

  return pipeline;
}

gboolean
interrupt_signal_handler (gpointer userdata)
{
  DrmPlayer *player = (DrmPlayer *) userdata;

  g_print ("\n\nReceived an interrupt signal, send EOS ...\n");
  if (gst_element_set_state (player->pipeline,
    GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
    g_print ("Couldn't set the pipeline to NULL! %.30s \n", SPACE);
  }
  g_async_queue_push (player->queue,
            gst_structure_new_empty (TERMINATE_MESSAGE));

  g_main_loop_quit (player->loop);

  return TRUE;
}

gboolean
stdin_msg (GIOChannel *source, GIOCondition condition, gpointer data)
{
  DrmPlayer *player = (DrmPlayer *) data;
  GIOStatus status = G_IO_STATUS_NORMAL;
  gchar *input;

  // keep trying to read the data until resource not available
  do {
    GError *error = NULL;
    status = g_io_channel_read_line (source, &input, NULL, NULL, &error);

    if ((status == G_IO_STATUS_ERROR) && (error != NULL)) {
      g_printerr ("ERROR: Failed to parse input: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);

      g_print ("I/O channel failed, none of the input commands will work!\n");
      return FALSE;
    } else if ((status == G_IO_STATUS_ERROR) && (error == NULL)) {
      g_printerr ("UNKNOWN ERROR: Failed to parse input! %.30s\n", SPACE);

      g_print ("I/O channel failed, none of the input commands will work!\n");
      return FALSE;
    }
  } while (status == G_IO_STATUS_AGAIN);

  // remove trailing spaces and newlines only if is's not <space>
  if (strlen (input) > 1)
    input = g_strchomp (input);

  g_async_queue_push (player->queue, gst_structure_new (STDIN_MESSAGE,
            "input", G_TYPE_STRING, input, NULL));

  g_free (input);
  return TRUE;
}

gboolean
bus_msg_handler (GstBus *bus, GstMessage *msg, gpointer data)
{
  DrmPlayer *player = (DrmPlayer *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_print ("\nEnd of stream reached! %.30s\n", SPACE);
      g_async_queue_push (player->queue,
            gst_structure_new_empty (TERMINATE_MESSAGE));

      g_main_loop_quit (player->loop);
      break;

    case GST_MESSAGE_ERROR:;
      GError *err;
      gchar *dbg;

      gst_message_parse_error (msg, &err, &dbg);
      g_printerr ("ERROR: %s\n", err->message);

      if (dbg != NULL)
        g_printerr ("Debug information: %s\n", dbg);
      g_clear_error (&err);
      g_free (dbg);

      gst_element_set_state (player->pipeline, GST_STATE_NULL);

      g_async_queue_push (player->queue,
            gst_structure_new_empty (TERMINATE_MESSAGE));

      g_main_loop_quit (player->loop);
      break;

    case GST_MESSAGE_WARNING:;
      gst_message_parse_warning (msg, &err, &dbg);
      g_printerr ("WARNING %s\n", err->message);

      if (dbg != NULL)
        g_printerr ("WARNING debug information: %s\n", dbg);
      g_clear_error (&err);
      g_free (dbg);
      break;

    case GST_MESSAGE_STATE_CHANGED:
      // not interested if state change msg is not from the pipeline but    individual elements
      if (GST_MESSAGE_SRC (msg) != GST_OBJECT_CAST (player->pipeline))
        break;

      GstState old_state, new_state, pending_state;
      gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
      g_print ("Pipeline state changed from %s to %s, pending: %s\n",
                gst_element_state_get_name (old_state), gst_element_state_get_name (new_state),
                gst_element_state_get_name (pending_state));
      player->current_state = new_state;
      break;

    case GST_MESSAGE_BUFFERING:;
      gint percent;
      gst_message_parse_buffering (msg, &percent);

      if (percent == 100) {
        // buffering is done, set the pipeline to previous state or state requested by user
        player->buffering = FALSE;
        if (!player->live)
          gst_element_set_state (player->pipeline, player->desired_state);
      } else if (!player->buffering) {
        // buffering started, set the pipeline to PAUSED
        if (!player->live)
          gst_element_set_state (player->pipeline, GST_STATE_PAUSED);
        player->buffering = TRUE;
      }
      break;

    // clock is lost, set the pipeline to PAUSED and then to PLAYING again to select a new one
    case GST_MESSAGE_CLOCK_LOST:
      gst_element_set_state (player->pipeline, GST_STATE_PAUSED);
      gst_element_set_state (player->pipeline, GST_STATE_PLAYING);
      break;

    default:
      break;
  }
  // keep listening to the bus
  return TRUE;
}

void
free_drm_player (DrmPlayer *player)
{
  // DRM sessions close
  Vector<uint8_t> session_id;
  str_to_vec (player->drm_session_id, session_id);

  if (player->drm_plugin->closeSession (session_id) == PRDRM_FAILED)
    g_printerr ("Close session failed\n");
  else
    g_printerr ("Session closed successfully\n");

  // Destroy DRMPlugin and DRMFactory and close lib
  delete player->drm_plugin;
  player->drm_plugin = NULL;
  delete player->drm_factory;
  player->drm_factory = NULL;

  dlclose (player->lib_handle);
  player->lib_handle = NULL;

  g_free (player->pro_header);
}

DrmPlayer*
create_player (gchar **args, GIOChannel *gio)
{
  GstElement *pipeline = NULL;
  DrmPlayer *player;
  gboolean parse_error = FALSE;
  gchar pipeline_description [5000];
  gchar *manifest_content = NULL, *demux, *tail_bin;

  player = g_new0 (DrmPlayer, 1);
  player->encrypted = FALSE;
  player->is_playready = FALSE;
  player->is_widevine = FALSE;
  player->is_dash = TRUE;
  player->lib_handle = NULL;
  player->drm_factory = NULL;
  player->drm_plugin = NULL;
  player->pro_header = NULL;

  // download manifest from the given url using libcurl
  if (fetch_manifest (args[0]) != CURLE_OK)
    goto fetch_error;

  // decide DASH or HLS
  manifest_content = decide_dash_or_hls (player);

  if (player->is_dash) {
    // parse XML manifest to extract required params
    if (!parse_dash_manifest (player)) {
      parse_error = TRUE;
      goto parse_error;
    }
  } else {
    // parse HLS manifest to extract required params
    if (!parse_hls_manifest (player, manifest_content)) {
      parse_error = TRUE;
      goto parse_error;
    }
  }

  demux = (gchar *)(player->is_dash ? "dashdemux ! qtdemux" : "hlsdemux ! tsdemux");
  tail_bin = "queue ! h264parse ! fakesink";
  if (g_strv_length (args) >= 2)
    tail_bin = args[1];

  g_snprintf (pipeline_description, sizeof (pipeline_description), "souphttpsrc location=%s ! %s ! %s", args[0], demux, tail_bin);
  // if (!player->encrypted)
  //   g_snprintf (pipeline_description, sizeof (pipeline_description), "souphttpsrc location=%s ! %s ! queue ! h264parse ! fakesink", args[0], demux);
  // else {
  //   const gchar *sid = player->drm_session_id.c_str();
  //   g_snprintf (pipeline_description, sizeof (pipeline_description), "souphttpsrc location=%s ! %s ! queue ! h264parse ! qtidecryptor sid=%s ! fakesink", args[0], demux, sid);
  // }

  pipeline = create_pipeline (player, pipeline_description);

parse_error:
  g_free (manifest_content);
  if (player->encrypted && parse_error)
    free_drm_player (player);
fetch_error:
  if (pipeline == NULL) {
    g_free (player);
    return NULL;
  }

  player->pipeline = pipeline;

  player->bus_watch_id =
      gst_bus_add_watch (GST_ELEMENT_BUS (player->pipeline), bus_msg_handler,
      player);

  // call stdin_msg function whenever there is data to read
  player->stdin_watch_id =
      g_io_add_watch (gio, GIOCondition (G_IO_PRI | G_IO_IN), stdin_msg, player);

  player->queue = g_async_queue_new_full ((GDestroyNotify) gst_structure_free);

  player->loop = g_main_loop_new (NULL, FALSE);

  player->interrupt_watch_id = g_unix_signal_add (SIGINT, interrupt_signal_handler, player);

  player->current_state = GST_STATE_NULL;

  player->desired_state = GST_STATE_PAUSED;

  player->buffering = FALSE;

  player->live = FALSE;

  return player;
}

void
play (DrmPlayer *player)
{
  gst_element_set_state (player->pipeline, GST_STATE_NULL);

  g_print ("Setting pipeline to PAUSED state...\n");
  switch (gst_element_set_state (player->pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Couldn't play! %.30s\n", SPACE);
      // rest will be handled by GST_MESSAGE_ERROR
      break;
    case GST_STATE_CHANGE_NO_PREROLL:
      // pipeline is live
      player->live = TRUE;
      break;
    case GST_STATE_CHANGE_ASYNC:
      // block and wait for state change to complete
      if (gst_element_get_state (player->pipeline, NULL, NULL,
        GST_CLOCK_TIME_NONE) != GST_STATE_CHANGE_SUCCESS) {
        g_printerr ("ERROR: Pipeline failed to preroll!\n");
      }
      break;
    default:
      break;
  }
  // gst_element_set_state (player->pipeline, GST_STATE_PLAYING);
  // g_print ("Setting to PLAYING... \n");
}

void
print_menu (DrmPlayer *player)
{
  g_print ("\n%.15s MENU %.15s\n", DASH_LINE, DASH_LINE);

  g_print ("%.2s %s %.2s : %.2s %s\n", SPACE, PLAY, SPACE, SPACE, "Play/Pause");
  g_print ("%.2s %s %.2s : %.2s %s\n", SPACE, STOP, SPACE, SPACE, "Stop");
  g_print ("%.2s %s %.2s : %.2s %s\n", SPACE, QUIT, SPACE, SPACE, "Quit");
}

gpointer
handle_menu (gpointer data)
{
  DrmPlayer *player =  (DrmPlayer *)data;
  gboolean active = TRUE;

  // keep printing the menu and listening to input until there's no error or eos message
  while (active) {
    gchar *input = NULL, *str = NULL;

    print_menu (player);
    g_print ("\nChoose an option: ");

    // if FALSE is returned, termination signal has been issued due to some error or eos
    if (!wait_stdin_message (player->queue, &str))
      active = FALSE;
    else if (g_str_equal (input = g_ascii_strdown (str, -1), PLAY))
      toggle_play (player);
    else if (g_str_equal (input, STOP))
      stop (player);
    // if the user wants to quit while in main menu, terminate and return
    else if (g_str_equal (input, QUIT)) {
      // gst_element_send_event (player->pipeline, gst_event_new_eos ());
      if (gst_element_set_state (player->pipeline,
            GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
          g_print ("Couldn't set the pipeline to NULL! %.30s \n", SPACE);
      }
      active = FALSE;
    }

  free:
    if (input)
      g_free (input);
    g_free (str);
  }

  // if main loop is still running, terminate it and return
  if (g_main_loop_is_running (player->loop))
    g_main_loop_quit (player->loop);

  return NULL;
}

void
free_player (DrmPlayer *player)
{
  if (player->encrypted)
    free_drm_player (player);

  if (player->pipeline) {
    gst_element_set_state (player->pipeline, GST_STATE_NULL);
    gst_object_unref (player->pipeline);
  }
  g_main_loop_unref (player->loop);

  if (player->queue)
    g_async_queue_unref (player->queue);

  // remove the event sources
  g_source_remove (player->bus_watch_id);
  g_source_remove (player->stdin_watch_id);
  g_source_remove (player->interrupt_watch_id);

  g_free (player);
}

int
main (gint argc, gchar *argv[])
{
  DrmPlayer *player = NULL;
  GError *err = NULL;
  gint ret = -1;
  GIOChannel *gio = NULL;

  // the command line options parser
  GOptionContext *ctx;

  // string to accept url from cmd-line
  gchar **args = NULL;

  // menu thread
  GThread *mthread = NULL;

  // parse GStreamer-specific options from command line
  gst_init (&argc, &argv);

  GOptionEntry options[] = {
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &args, NULL},
    {NULL}
  };

  g_set_prgname ("gst-drm-player-app");

  ctx = g_option_context_new ("<manifest-url> <tail-bin>");
  g_option_context_set_summary (ctx,
    "You must provide a URL (enclosed within quotes) to play.\nYou must provide the tail bin following qtdemux (enclosed within quotes) to play.\nThe pipeline used:\n\tFor DASH: \"souphttpsrc location=<manifest-url> ! dashdemux ! qtdemux ! <tail-bin>\"\n\tFor HLS: \"souphttpsrc location=<manifest-url> ! hlsdemux ! tsdemux ! <tail-bin>\"\n");

  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_printerr ("ERROR: Couldn't initialize: %s\n",
        GST_STR_NULL (err->message));

    g_option_context_free (ctx);
    g_clear_error (&err);

    goto init_error;
  }
  g_option_context_free (ctx);

  if (args == NULL) {
    g_printerr
        ("Usage: gst-drm-player-app <manifest-url> <tail-bin following qtdemux>\n");
    g_printerr ("\nYou must provide a URL (enclosed within quotes) to play.\nYou must provide the tail bin following qtdemux (enclosed within quotes) to play.\nThe pipeline used:\n\tFor DASH: \"souphttpsrc location=<manifest-url> ! dashdemux ! qtdemux ! <tail-bin>\"\n\tFor HLS: \"souphttpsrc location=<manifest-url> ! hlsdemux ! tsdemux ! <tail-bin>\"\n");
    g_printerr ("\nFor help: gst-drm-player-app [-h | --help]\n\n");

    goto init_error;
  }

  // create a GIOChannel to listen to the standard input stream
  if ((gio = g_io_channel_unix_new (fileno (stdin))) == NULL) {
    g_printerr ("ERROR: Failed to initialize I/O support! %.30s\n", SPACE);

    goto stdin_error;
  }

  if ((player = create_player (args, gio)) == NULL) {
    g_printerr ("ERROR: Failed to create pipeline. %.30s\n", SPACE);

    goto pipeline_error;
  }

  play (player);

  if ((mthread = g_thread_new ("menu", handle_menu, player)) == NULL) {
    g_printerr ("ERROR: Failed to create menu thread. %.30s\n", SPACE);

    goto thread_error;
  }

  // start the main event loop
  g_main_loop_run (player->loop);

  if (mthread != NULL)
    // block till the menu thread exits
    g_thread_join (mthread);

  g_print ("\nClosing app!!\n");
  ret = 0;

  // clean up resources
thread_error:
  free_player (player);
pipeline_error:
  g_io_channel_unref (gio);
stdin_error:
  g_free (args);
init_error:
  gst_deinit ();
  return ret;
}
