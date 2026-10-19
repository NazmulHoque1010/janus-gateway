/*! \file   janus_audiocall.c
 * \author Lorenzo Miniero <lorenzo@meetecho.com>
 * \copyright GNU General Public License v3
 * \brief  Janus AudioCall plugin
 * \details Check the \ref audiocall for more details.
 *
 * \ingroup plugins
 * \ref plugins
 *
 * \page audiocall AudioCall plugin documentation
 * This is a simple audio call plugin for Janus, allowing two
 * WebRTC peers to call each other through the Janus core. The idea is to
 * provide a similar service as the at the time well known (and now
 * discontinued) AppRTC demo (https://github.com/webrtc/apprtc),
 * but with the media flowing through a server rather than being peer-to-peer.
 *
 * The plugin provides a simple fake registration mechanism. A peer attaching
 * to the plugin needs to specify a username, which acts as a "phone number":
 * if the username is free, it is associated with the peer, which means
 * he/she can be "called" using that username by another peer. Peers can
 * either "call" another peer, by specifying their username, or wait for a call.
 * The approach used by this plugin is similar to the one employed by the
 * echo test one: all frames (RTP/RTCP) coming from one peer are relayed
 * to the other.
 *
 * Just as in the janus_audiocall.c plugin, there are knobs to control
 * whether audio and/or video should be muted or not, and if the bitrate
 * of the peer needs to be capped by means of REMB messages.
 *
 * \section vcallapi Audio Call API
 *
 * All requests you can send in the Audio Call API are asynchronous,
 * which means all responses (successes and errors) will be delivered
 * as events with the same transaction.
 *
 * The supported requests are \c list , \c register , \c call ,
 * \c accept , \c set and \c hangup . \c list allows you to get a list
 * of all the registered peers; \c register can be used to register
 * a username to call and be called; \c call is used to start a video
 * call with somebody through the plugin, while \c accept is used to
 * accept the call in case one is invited instead of inviting; \c set
 * can be used to configure some call-related settings (e.g., a cap on
 * the send bandwidth); finally, \c hangup can be used to terminate the
 * communication at any time, either to hangup an ongoing call or to
 * cancel/decline a call that hasn't started yet.
 *
 * The \c list request has to be formatted as follows:
 *
\verbatim
{
	"request" : "list"
}
\endverbatim
 *
 * A successful request will result in an array of peers to be returned:
 *
\verbatim
{
	"audiocall" : "event",
	"result" : {
		"list": [	// Array of peers
			"alice78",
			"bob51",
			// others
		]
	}
}
\endverbatim
 *
 * An error instead (and the same applies to all other requests, so this
 * won't be repeated) would provide both an error code and a more verbose
 * description of the cause of the issue:
 *
\verbatim
{
	"audiocall" : "event",
	"error_code" : <numeric ID, check Macros below>,
	"error" : "<error description as a string>"
}
\endverbatim
 *
 * To register a username to call and be called, the \c register request
 * can be used. This works on a "first come, first served" basis: there's
 * no authentication involved, you just specify the username you'd like
 * to use and, if free, it's assigned to you. Notice that there's no
 * way to unregister: you have to close the handle to free the username.
 * The \c register request has to be formatted as follows:
 *
\verbatim
{
	"request" : "register",
	"username" : "<desired unique username>"
}
\endverbatim
 *
 * If successful, this will result in a \c registered event:
 *
\verbatim
{
	"audiocall" : "event",
	"result" : {
		"event" : "registered",
		"username" : "<same username, registered>"
	}
}
\endverbatim
 *
 * Once you're registered, you can either start a new call or wait to
 * be called by someone else who knows your username. To start a new
 * call, the \c call request can be used: this request must be attached
 * to a JSEP offer containing the WebRTC-related info to setup a new
 * media session. A \c call request has to be formatted as follows:
 *
\verbatim
{
	"request" : "call",
	"username" : "<username to call>"
}
\endverbatim
 *
 * If successful, this will result in a \c calling event:
 *
\verbatim
{
	"audiocall" : "event",
	"result" : {
		"event" : "calling",
		"username" : "<same username, registered>"
	}
}
\endverbatim
 *
 * At the same time, the user being called will receive an
 * \c incomingcall event
 *
\verbatim
{
	"audiocall" : "event",
	"result" : {
		"event" : "incomingcall",
		"username" : "<your username>"
	}
}
\endverbatim
 *
 * To accept the call, the \c accept request can be used. This request
 * must be attached to a JSEP answer containing the WebRTC-related
 * information to complete the actual PeerConnection setup. A \c accept
 * request has to be formatted as follows:
 *
\verbatim
{
	"request" : "accept"
}
\endverbatim
 *
 * If successful, both the caller and the callee will receive an
 * \c accepted event to notify them about the success of the signalling:
 *
\verbatim
{
	"audiocall" : "event",
	"result" : {
		"event" : "accepted",
		"username" : "<caller username>"
	}
}
\endverbatim
 *
 * At this point, the media-related settings of the call can be modified
 * on either side by means of a \c set request, which acts pretty much
 * as the one in the \ref echoapi . The \c set request has to be
 * formatted as follows. All the attributes (except \c request) are
 * optional, so any request can contain a subset of them:
 *
\verbatim
{
	"request" : "set",
	"audio" : true|false,
	"video" : true|false,
	"bitrate" : <numeric bitrate value>,
	"record" : true|false,
	"filename" : <base path/filename to use for the recording>,
	"substream" : <substream to receive (0-2), in case simulcasting is enabled>,
	"temporal" : <temporal layers to receive (0-2), in case simulcasting is enabled>,
	"fallback" : <How much time (in us, default 250000) without receiving packets will make us drop to the substream below>
}
\endverbatim
 *
 * \c audio instructs the plugin to do or do not relay audio frames;
 * \c video does the same for video; \c bitrate caps the bandwidth to
 * force on the browser encoding side (e.g., 128000 for 128kbps);
 * \c record enables or disables the recording of this peer; in case
 * recording is enabled, \c filename allows to specify a base
 * path/filename to use for the files (-audio.mjr, -video.mjr and -data.mjr
 * are automatically appended). Beware that enabling the recording only
 * records this user's contribution, and not the whole call: to record
 * both sides, you need to enable recording for both the peers in the
 * call. Finally, in case the call uses simulcasting, \c substream and
 * \c temporal can be used to manually pick which substream and/or temporal
 * layer should be received from the peer.
 *
 * A successful request will result in a \c set event:
 *
\verbatim
{
	"audiocall" : "event",
	"result" : {
		"event" : "set"
	}
}
\endverbatim
 *
 * Notice that the \c set request is also what you use when you want
 * to renegotiate a session, e.g., for the purpose of adding/removing
 * media streams or forcing an ICE restart. In that case, even an empty
 * \c set request is fine, as long as it accompanies a new JSEP offer
 * or answer (depending on who originated the session update). The user
 * receiving the updated JSEP offer/answer will get an \c update event:
 *
\verbatim
{
	"audiocall" : "event",
	"result" : {
		"event" : "update",
	}
}
\endverbatim
 *
 * To decline an incoming call, cancel an attempt to call or simply
 * hangup an ongoing conversation, the \c hangup request can be used,
 * which has to be formatted as follows:
 *
\verbatim
{
	"request" : "hangup"
}
\endverbatim
 *
 * Whatever the reason of a call being closed (e.g., a \c hangup request,
 * a PeerConnection being closed, or something else), both parties in
 * the communication will receive a \c hangup event:
 *
\verbatim
{
	"audiocall" : "event",
	"result" : {
		"event" : "hangup",
		"username" : "<username of who closed the communication>",
		"reason" : "<description of what happened>"
	}
}
\endverbatim
 */

#include "plugin.h"

#include <jansson.h>

#include "../debug.h"
#include "../apierror.h"
#include "../config.h"
#include "../mutex.h"
#include "../record.h"
#include "../rtp.h"
#include "../rtcp.h"
#include "../sdp-utils.h"
#include "../utils.h"

/* IC: sockets for the peer link between the two Janus servers */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>


/* Plugin information */
#define JANUS_AUDIOCALL_VERSION			6
#define JANUS_AUDIOCALL_VERSION_STRING	"0.3.0-ic"
/* IC: this file builds two plugins. Normally it is the audio-only AudioCall
 * plugin; the build also generates janus_customvideocall.c from it (all names
 * renamed) and compiles that with -DIC_VIDEO to get the CustomVideoCall plugin,
 * which allows video. Both share the peer link code (each on its own port). */
#ifdef IC_VIDEO
#define JANUS_AUDIOCALL_DESCRIPTION		"Audio and video calls between two WebRTC peers, on this or a peer Janus server (IC project)."
#define JANUS_AUDIOCALL_DEFAULT_LINK_PORT	7089
#else
#define JANUS_AUDIOCALL_DESCRIPTION		"Audio-only calls between two WebRTC peers (IC project, based on the Janus VideoCall plugin)."
#define JANUS_AUDIOCALL_DEFAULT_LINK_PORT	7088
#endif
#define JANUS_AUDIOCALL_NAME			"JANUS AudioCall plugin"
#define JANUS_AUDIOCALL_AUTHOR			"Meetecho s.r.l."
#define JANUS_AUDIOCALL_PACKAGE			"janus.plugin.audiocall"

/* Plugin methods */
janus_plugin *create(void);
int janus_audiocall_init(janus_callbacks *callback, const char *config_path);
void janus_audiocall_destroy(void);
int janus_audiocall_get_api_compatibility(void);
int janus_audiocall_get_version(void);
const char *janus_audiocall_get_version_string(void);
const char *janus_audiocall_get_description(void);
const char *janus_audiocall_get_name(void);
const char *janus_audiocall_get_author(void);
const char *janus_audiocall_get_package(void);
void janus_audiocall_create_session(janus_plugin_session *handle, int *error);
struct janus_plugin_result *janus_audiocall_handle_message(janus_plugin_session *handle, char *transaction, json_t *message, json_t *jsep);
void janus_audiocall_setup_media(janus_plugin_session *handle);
void janus_audiocall_incoming_rtp(janus_plugin_session *handle, janus_plugin_rtp *packet);
void janus_audiocall_incoming_rtcp(janus_plugin_session *handle, janus_plugin_rtcp *packet);
void janus_audiocall_incoming_data(janus_plugin_session *handle, janus_plugin_data *packet);
void janus_audiocall_data_ready(janus_plugin_session *handle);
void janus_audiocall_slow_link(janus_plugin_session *handle, int mindex, gboolean video, gboolean uplink);
void janus_audiocall_hangup_media(janus_plugin_session *handle);
void janus_audiocall_destroy_session(janus_plugin_session *handle, int *error);
json_t *janus_audiocall_query_session(janus_plugin_session *handle);

/* Plugin setup */
static janus_plugin janus_audiocall_plugin =
	JANUS_PLUGIN_INIT (
		.init = janus_audiocall_init,
		.destroy = janus_audiocall_destroy,

		.get_api_compatibility = janus_audiocall_get_api_compatibility,
		.get_version = janus_audiocall_get_version,
		.get_version_string = janus_audiocall_get_version_string,
		.get_description = janus_audiocall_get_description,
		.get_name = janus_audiocall_get_name,
		.get_author = janus_audiocall_get_author,
		.get_package = janus_audiocall_get_package,

		.create_session = janus_audiocall_create_session,
		.handle_message = janus_audiocall_handle_message,
		.setup_media = janus_audiocall_setup_media,
		.incoming_rtp = janus_audiocall_incoming_rtp,
		.incoming_rtcp = janus_audiocall_incoming_rtcp,
		.incoming_data = janus_audiocall_incoming_data,
		.data_ready = janus_audiocall_data_ready,
		.slow_link = janus_audiocall_slow_link,
		.hangup_media = janus_audiocall_hangup_media,
		.destroy_session = janus_audiocall_destroy_session,
		.query_session = janus_audiocall_query_session,
	);

/* Plugin creator */
janus_plugin *create(void) {
	JANUS_LOG(LOG_VERB, "%s created!\n", JANUS_AUDIOCALL_NAME);
	return &janus_audiocall_plugin;
}

/* Parameter validation */
static struct janus_json_parameter request_parameters[] = {
	{"request", JSON_STRING, JANUS_JSON_PARAM_REQUIRED}
};
static struct janus_json_parameter username_parameters[] = {
	{"username", JSON_STRING, JANUS_JSON_PARAM_REQUIRED}
};
static struct janus_json_parameter set_parameters[] = {
	{"audio", JANUS_JSON_BOOL, 0},
	{"video", JANUS_JSON_BOOL, 0},
	{"bitrate", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"record", JANUS_JSON_BOOL, 0},
	{"filename", JSON_STRING, 0},
	{"restart", JANUS_JSON_BOOL, 0}
};

/* Useful stuff */
static volatile gint initialized = 0, stopping = 0;
static gboolean notify_events = TRUE;
static janus_callbacks *gateway = NULL;
static GThread *handler_thread;
static void *janus_audiocall_handler(void *data);
static void janus_audiocall_hangup_media_internal(janus_plugin_session *handle);

typedef struct janus_audiocall_message {
	janus_plugin_session *handle;
	char *transaction;
	json_t *message;
	json_t *jsep;
} janus_audiocall_message;
static GAsyncQueue *messages = NULL;
static janus_audiocall_message exit_message;

typedef struct janus_audiocall_session {
	janus_plugin_session *handle;
	gchar *username;
	gboolean has_audio;
	gboolean has_video;
	gboolean has_data;
	gboolean audio_active;
	gboolean video_active;
	janus_audiocodec acodec;/* Codec used for audio, if available */
	janus_videocodec vcodec;/* Codec used for video, if available */
	int opusred_pt;
	uint32_t bitrate, peer_bitrate;
	guint16 slowlink_count;
	struct janus_audiocall_session *peer;
	janus_rtp_switching_context context;
	uint32_t ssrc[3];		/* Only needed in case VP8 (or H.264) simulcasting is involved */
	char *rid[3];			/* Only needed if simulcasting is rid-based */
	janus_mutex rid_mutex;	/* Mutex to protect access to the rid array */
	janus_rtp_simulcasting_context sim_context;
	janus_vp8_simulcast_context vp8_context;
	janus_recorder *arc;	/* The Janus recorder instance for this user's audio, if enabled */
	janus_recorder *vrc;	/* The Janus recorder instance for this user's video, if enabled */
	janus_recorder *drc;	/* The Janus recorder instance for this user's data, if enabled */
	gboolean e2ee;			/* Whether media is encrypted, e.g., using Insertable Streams */
	janus_mutex rec_mutex;	/* Mutex to protect the recorders from race conditions */
	volatile gint incall;
	volatile gint dataready;
	volatile gint hangingup;
	volatile gint destroyed;
	/* IC: cross-server call state (protected by mutex) */
	gboolean remote_call;		/* TRUE if our call partner is on the peer server */
	guint32 call_id;			/* Identifies the call on the peer link */
	char *remote_username;		/* Username of our partner on the peer server */
	gboolean remote_video_seen;	/* Got the first video packet from the peer (keyframe requested) */
	struct sockaddr_in remote_addr;	/* Link address of the server our partner is on */
	char *remote_server;		/* Name of that server */
	janus_mutex mutex;
	janus_refcount ref;
} janus_audiocall_session;
static GHashTable *sessions = NULL, *usernames = NULL;
static janus_mutex sessions_mutex = JANUS_MUTEX_INITIALIZER;

static void janus_audiocall_session_destroy(janus_audiocall_session *session) {
	if(session && g_atomic_int_compare_and_exchange(&session->destroyed, 0, 1))
		janus_refcount_decrease(&session->ref);
}
static void janus_audiocall_session_unref(janus_audiocall_session *session) {
	if(session)
		janus_refcount_decrease(&session->ref);
}

static void janus_audiocall_session_free(const janus_refcount *session_ref) {
	janus_audiocall_session *session = janus_refcount_containerof(session_ref, janus_audiocall_session, ref);
	/* Remove the reference to the core plugin session */
	janus_refcount_decrease(&session->handle->ref);
	/* This session can be destroyed, free all the resources */
	g_free(session->username);
	g_free(session->remote_username);
	g_free(session->remote_server);
	janus_mutex_destroy(&session->mutex);
	janus_mutex_destroy(&session->rid_mutex);
	janus_mutex_destroy(&session->rec_mutex);
	janus_rtp_simulcasting_cleanup(NULL, NULL, session->rid, NULL);
	g_free(session);
}

static void janus_audiocall_message_free(janus_audiocall_message *msg) {
	if(!msg || msg == &exit_message)
		return;

	if(msg->handle && msg->handle->plugin_handle) {
		janus_audiocall_session *session = (janus_audiocall_session *)msg->handle->plugin_handle;
		janus_refcount_decrease(&session->ref);
	}
	msg->handle = NULL;

	g_free(msg->transaction);
	msg->transaction = NULL;
	if(msg->message)
		json_decref(msg->message);
	msg->message = NULL;
	if(msg->jsep)
		json_decref(msg->jsep);
	msg->jsep = NULL;

	g_free(msg);
}


/* Error codes */
#define JANUS_AUDIOCALL_ERROR_UNKNOWN_ERROR			499
#define JANUS_AUDIOCALL_ERROR_NO_MESSAGE			470
#define JANUS_AUDIOCALL_ERROR_INVALID_JSON			471
#define JANUS_AUDIOCALL_ERROR_INVALID_REQUEST		472
#define JANUS_AUDIOCALL_ERROR_REGISTER_FIRST		473
#define JANUS_AUDIOCALL_ERROR_INVALID_ELEMENT		474
#define JANUS_AUDIOCALL_ERROR_MISSING_ELEMENT		475
#define JANUS_AUDIOCALL_ERROR_USERNAME_TAKEN		476
#define JANUS_AUDIOCALL_ERROR_ALREADY_REGISTERED	477
#define JANUS_AUDIOCALL_ERROR_NO_SUCH_USERNAME		478
#define JANUS_AUDIOCALL_ERROR_USE_ECHO_TEST			479
#define JANUS_AUDIOCALL_ERROR_ALREADY_IN_CALL		480
#define JANUS_AUDIOCALL_ERROR_NO_CALL				481
#define JANUS_AUDIOCALL_ERROR_MISSING_SDP			482
#define JANUS_AUDIOCALL_ERROR_INVALID_SDP			483
#define JANUS_AUDIOCALL_ERROR_AUDIO_ONLY			484
#define JANUS_AUDIOCALL_ERROR_PEER_USER			485


/* ===========================================================================
 * IC: link between the plugins of all the Janus servers on the LAN.
 *
 * Nothing about other servers is configured. Every plugin listens on a UDP
 * port (link_port, the same on every server) and, every 2 seconds, broadcasts
 *   {"type":"hello","server":"pc1","users":["Nazmul"],"ws_port":8188}
 * on the LAN, and also sends it directly (unicast) to every server it already
 * knows, because Wi-Fi delivers broadcasts unreliably. Every server builds its
 * table of servers from these announcements: name, address and who is
 * registered there. A server that stays silent for 8 seconds is dropped.
 *
 * Apps find the servers the same way: they broadcast {"type":"discover"}
 * and each server answers {"type":"here","server":...,"users":N,"ws_port":...}.
 *
 * Apps on the internet can't broadcast to the servers. For them, every server
 * can be given a public_host (the address apps use to reach it, e.g. its public
 * IP). It is added to the hello (so every server learns everybody's public
 * address), to "here", and to the "list"/"online" results: an app connects to
 * any one server over the WebSocket, sends "list", and gets every server with
 * its public_host, ws_port and users, so it can pick the least busy one.
 *
 * Calls between servers use unicast messages to the other server
 * (call / accepted / hangup) and media packets 'A'/'V'/'K' + call_id.
 * Media is only accepted from the address the call was set up with.
 * ======================================================================== */
static gboolean link_enabled = FALSE;
static char *link_server_name = NULL;
static uint16_t link_local_port = JANUS_AUDIOCALL_DEFAULT_LINK_PORT, link_ws_port = 8188;
static char *link_public_host = NULL;	/* address apps use to reach this server (public_host), or NULL */
static int link_fd = -1;
static GThread *link_thread = NULL;
static volatile gint link_dirty = 0;			/* our users changed: announce now */
static volatile gint link_online_dirty = 1;		/* someone's users changed: refresh our users' online lists */
static char *link_last_online = NULL;			/* last online list pushed (link thread only) */

/* Another Janus server, learned from its announcements */
typedef struct janus_audiocall_peer {
	char *name;
	struct sockaddr_in addr;	/* where its plugin's link socket is */
	json_t *users;				/* usernames registered there (sorted array) */
	char *public_host;			/* address apps use to reach it (its public_host), or NULL */
	uint16_t ws_port;			/* its Janus WebSocket port */
	gint64 last_seen;
} janus_audiocall_peer;
static GHashTable *link_peers = NULL;	/* name -> janus_audiocall_peer, protected by link_mutex */
static janus_mutex link_mutex = JANUS_MUTEX_INITIALIZER;

#define JANUS_AUDIOCALL_LINK_HELLO_EVERY	(2 * G_TIME_SPAN_SECOND)
#define JANUS_AUDIOCALL_LINK_DEAD_AFTER		(8 * G_TIME_SPAN_SECOND)

/* IC: cross-server calls in progress on THIS server: call_id -> local session (the table holds a reference) */
static GHashTable *remote_calls = NULL;
static janus_mutex remote_calls_mutex = JANUS_MUTEX_INITIALIZER;
static gboolean janus_audiocall_sdp_allowed(const char *sdp_text, char *reason, size_t reason_len);

static void janus_audiocall_peer_free(janus_audiocall_peer *peer) {
	if(peer == NULL)
		return;
	g_free(peer->name);
	json_decref(peer->users);
	g_free(peer->public_host);
	g_free(peer);
}

static gboolean janus_audiocall_same_addr(const struct sockaddr_in *a, const struct sockaddr_in *b) {
	return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static const char *janus_audiocall_addr_str(const struct sockaddr_in *a, char *buf, size_t len) {
	char ip[INET_ADDRSTRLEN];
	if(inet_ntop(AF_INET, &a->sin_addr, ip, sizeof(ip)) == NULL)
		g_strlcpy(ip, "?", sizeof(ip));
	g_snprintf(buf, len, "%s:%d", ip, ntohs(a->sin_port));
	return buf;
}

/* Is this username registered on THIS server? (caller must not hold sessions_mutex) */
static gboolean janus_audiocall_is_local_user(const char *username) {
	if(username == NULL)
		return FALSE;
	janus_mutex_lock(&sessions_mutex);
	janus_audiocall_session *user = usernames ? g_hash_table_lookup(usernames, username) : NULL;
	gboolean found = (user != NULL && !g_atomic_int_get(&user->destroyed));
	janus_mutex_unlock(&sessions_mutex);
	return found;
}

/* Usernames registered on THIS server, as a sorted JSON array (caller must not hold sessions_mutex) */
static json_t *janus_audiocall_local_users(void) {
	GList *names = NULL;
	janus_mutex_lock(&sessions_mutex);
	if(usernames != NULL) {
		GHashTableIter iter;
		gpointer key, value;
		g_hash_table_iter_init(&iter, usernames);
		while(g_hash_table_iter_next(&iter, &key, &value)) {
			janus_audiocall_session *user = value;
			if(user != NULL && !g_atomic_int_get(&user->destroyed))
				names = g_list_prepend(names, g_strdup((const char *)key));
		}
	}
	janus_mutex_unlock(&sessions_mutex);
	names = g_list_sort(names, (GCompareFunc)g_strcmp0);
	json_t *list = json_array();
	GList *temp = names;
	while(temp) {
		json_array_append_new(list, json_string((const char *)temp->data));
		temp = temp->next;
	}
	g_list_free_full(names, g_free);
	return list;
}

/* Send one JSON message to another server (or app) */
static void janus_audiocall_link_send_to(const struct sockaddr_in *to, json_t *msg) {
	if(link_fd < 0 || to == NULL || msg == NULL)
		return;
	char *text = json_dumps(msg, JSON_COMPACT);
	if(text == NULL)
		return;
	if(sendto(link_fd, text, strlen(text), 0, (const struct sockaddr *)to, sizeof(*to)) < 0) {
		char addr[64];
		JANUS_LOG(LOG_WARN, "[AudioCall] Link send error to %s: %s\n",
			janus_audiocall_addr_str(to, addr, sizeof(addr)), g_strerror(errno));
	}
	free(text);
}

/* Is this username registered on another server? If so, returns TRUE and,
 * optionally, that server's link address and name (caller frees the name) */
static gboolean janus_audiocall_peer_find_user(const char *username, struct sockaddr_in *addr, char **server) {
	if(username == NULL)
		return FALSE;
	gboolean found = FALSE;
	janus_mutex_lock(&link_mutex);
	if(link_peers != NULL) {
		GHashTableIter iter;
		gpointer value;
		g_hash_table_iter_init(&iter, link_peers);
		while(!found && g_hash_table_iter_next(&iter, NULL, &value)) {
			janus_audiocall_peer *peer = value;
			size_t i = 0;
			for(i = 0; i < json_array_size(peer->users); i++) {
				const char *u = json_string_value(json_array_get(peer->users, i));
				if(u != NULL && !strcmp(u, username)) {
					found = TRUE;
					if(addr)
						*addr = peer->addr;
					if(server)
						*server = g_strdup(peer->name);
					break;
				}
			}
		}
	}
	janus_mutex_unlock(&link_mutex);
	return found;
}

/* What we know about the other servers, as
 *   {"peer_online":bool, "peer_server":"pc2, pc3", "peer_list":[all their users],
 *    "servers":[{"server":"pc2","ip":"192.168.99.112","public_host":"...","ws_port":8188,"users":[...]}, ...]}
 * (public_host only if that server has one configured)
 * (sorted by server name, so the result only changes when something changed) */
static json_t *janus_audiocall_peer_info(void) {
	json_t *info = json_object();
	json_t *peer_list = json_array();
	json_t *servers = json_array();
	GString *names = g_string_new(NULL);
	janus_mutex_lock(&link_mutex);
	GList *peers = link_peers ? g_hash_table_get_keys(link_peers) : NULL;
	peers = g_list_sort(peers, (GCompareFunc)g_strcmp0);
	GList *temp = peers;
	while(temp) {
		janus_audiocall_peer *peer = g_hash_table_lookup(link_peers, temp->data);
		if(peer != NULL) {
			char ip[INET_ADDRSTRLEN];
			if(inet_ntop(AF_INET, &peer->addr.sin_addr, ip, sizeof(ip)) == NULL)
				g_strlcpy(ip, "?", sizeof(ip));
			json_t *s = json_object();
			json_object_set_new(s, "server", json_string(peer->name));
			json_object_set_new(s, "ip", json_string(ip));
			if(peer->public_host != NULL)
				json_object_set_new(s, "public_host", json_string(peer->public_host));
			json_object_set_new(s, "ws_port", json_integer(peer->ws_port));
			json_object_set_new(s, "users", json_deep_copy(peer->users));
			json_array_append_new(servers, s);
			json_array_extend(peer_list, peer->users);
			if(names->len > 0)
				g_string_append(names, ", ");
			g_string_append(names, peer->name);
		}
		temp = temp->next;
	}
	janus_mutex_unlock(&link_mutex);
	g_list_free(peers);
	json_object_set_new(info, "peer_online", json_array_size(servers) > 0 ? json_true() : json_false());
	if(names->len > 0)
		json_object_set_new(info, "peer_server", json_string(names->str));
	json_object_set_new(info, "peer_list", peer_list);
	json_object_set_new(info, "servers", servers);
	g_string_free(names, TRUE);
	return info;
}

/* ---- IC: cross-server call helpers ---- */

/* Mark a local session as being in a cross-server call with a user on another server */
static void janus_audiocall_remote_add(janus_audiocall_session *session, guint32 call_id, const char *remote_username,
		const struct sockaddr_in *remote_addr, const char *remote_server) {
	janus_mutex_lock(&session->mutex);
	session->remote_call = TRUE;
	session->call_id = call_id;
	session->remote_video_seen = FALSE;
	session->remote_addr = *remote_addr;
	g_free(session->remote_username);
	session->remote_username = g_strdup(remote_username);
	g_free(session->remote_server);
	session->remote_server = g_strdup(remote_server ? remote_server : "?");
	janus_mutex_unlock(&session->mutex);
	janus_refcount_increase(&session->ref);
	janus_mutex_lock(&remote_calls_mutex);
	g_hash_table_insert(remote_calls, GUINT_TO_POINTER(call_id), session);
	janus_mutex_unlock(&remote_calls_mutex);
}

/* Take a local session out of its cross-server call. Returns FALSE if it wasn't in one.
 * The caller must hold its own reference to the session. */
static gboolean janus_audiocall_remote_detach(janus_audiocall_session *session, guint32 *call_id,
		char **remote_username, struct sockaddr_in *remote_addr) {
	janus_mutex_lock(&session->mutex);
	if(!session->remote_call) {
		janus_mutex_unlock(&session->mutex);
		return FALSE;
	}
	guint32 id = session->call_id;
	char *user = session->remote_username;
	if(remote_addr)
		*remote_addr = session->remote_addr;
	session->remote_call = FALSE;
	session->call_id = 0;
	session->remote_username = NULL;
	g_free(session->remote_server);
	session->remote_server = NULL;
	janus_mutex_unlock(&session->mutex);
	g_atomic_int_set(&session->incall, 0);
	janus_mutex_lock(&remote_calls_mutex);
	if(remote_calls != NULL)
		g_hash_table_remove(remote_calls, GUINT_TO_POINTER(id));	/* Also drops the table's reference */
	janus_mutex_unlock(&remote_calls_mutex);
	if(call_id)
		*call_id = id;
	if(remote_username)
		*remote_username = user;
	else
		g_free(user);
	return TRUE;
}

/* Find the local session for a cross-server call (returns it with a reference, or NULL) */
static janus_audiocall_session *janus_audiocall_remote_get(guint32 call_id) {
	janus_mutex_lock(&remote_calls_mutex);
	janus_audiocall_session *session = remote_calls ? g_hash_table_lookup(remote_calls, GUINT_TO_POINTER(call_id)) : NULL;
	if(session != NULL)
		janus_refcount_increase(&session->ref);
	janus_mutex_unlock(&remote_calls_mutex);
	return session;
}

/* Push {"audiocall":"event","result":result} (+ optional jsep) to a local user; takes ownership of both */
static void janus_audiocall_push_result(janus_audiocall_session *session, json_t *result, json_t *jsep) {
	json_t *event = json_object();
	json_object_set_new(event, "audiocall", json_string("event"));
	json_object_set_new(event, "result", result);
	int ret = gateway->push_event(session->handle, &janus_audiocall_plugin, NULL, event, jsep);
	JANUS_LOG(LOG_VERB, "  >> Pushing event: %d (%s)\n", ret, janus_get_api_error(ret));
	json_decref(event);
	if(jsep != NULL)
		json_decref(jsep);
}

/* Tell the other server that a cross-server call has ended */
static void janus_audiocall_link_send_hangup(const struct sockaddr_in *to, guint32 call_id, const char *reason) {
	json_t *msg = json_object();
	json_object_set_new(msg, "type", json_string("hangup"));
	json_object_set_new(msg, "call_id", json_integer(call_id));
	json_object_set_new(msg, "reason", json_string(reason ? reason : "Hangup"));
	json_object_set_new(msg, "server", json_string(link_server_name));
	janus_audiocall_link_send_to(to, msg);
	json_decref(msg);
}

/* Forward one RTP packet of a cross-server call to the other server:
 * 'A' (audio) or 'V' (video) + call_id + RTP */
static void janus_audiocall_link_send_rtp(const struct sockaddr_in *to, guint32 call_id, gboolean video, const char *buf, uint16_t len) {
	if(link_fd < 0 || buf == NULL || len < 12 || len > 1500)
		return;
	char out[1505];
	out[0] = video ? 'V' : 'A';
	guint32 nid = htonl(call_id);
	memcpy(out + 1, &nid, sizeof(nid));
	memcpy(out + 5, buf, len);
	sendto(link_fd, out, len + 5, 0, (const struct sockaddr *)to, sizeof(*to));
}

/* Ask the other server for a video keyframe for a cross-server call: 'K' + call_id */
static void janus_audiocall_link_send_keyframe_request(const struct sockaddr_in *to, guint32 call_id) {
	if(link_fd < 0)
		return;
	char out[5];
	out[0] = 'K';
	guint32 nid = htonl(call_id);
	memcpy(out + 1, &nid, sizeof(nid));
	sendto(link_fd, out, sizeof(out), 0, (const struct sockaddr *)to, sizeof(*to));
}

/* End a local user's cross-server call because of the other side (or the link): notify the user and close the PeerConnection */
static void janus_audiocall_remote_hangup_local(janus_audiocall_session *session, const char *reason) {
	char *remote_user = NULL;
	if(!janus_audiocall_remote_detach(session, NULL, &remote_user, NULL))
		return;
	json_t *result = json_object();
	json_object_set_new(result, "event", json_string("hangup"));
	json_object_set_new(result, "username", json_string(remote_user ? remote_user : ""));
	json_object_set_new(result, "reason", json_string(reason ? reason : "Remote hangup"));
	janus_audiocall_push_result(session, result, NULL);
	gateway->close_pc(session->handle);
	g_free(remote_user);
}

/* End the cross-server calls with one server (NULL = all of them), e.g. because it went away */
static void janus_audiocall_remote_end_peer(const char *server, const char *reason) {
	GList *list = NULL;
	janus_mutex_lock(&remote_calls_mutex);
	if(remote_calls != NULL) {
		GHashTableIter iter;
		gpointer value;
		g_hash_table_iter_init(&iter, remote_calls);
		while(g_hash_table_iter_next(&iter, NULL, &value)) {
			janus_audiocall_session *session = value;
			janus_mutex_lock(&session->mutex);
			gboolean match = (server == NULL || !g_strcmp0(session->remote_server, server));
			janus_mutex_unlock(&session->mutex);
			if(match) {
				janus_refcount_increase(&session->ref);
				list = g_list_prepend(list, session);
			}
		}
	}
	janus_mutex_unlock(&remote_calls_mutex);
	GList *temp = list;
	while(temp) {
		janus_audiocall_session *session = temp->data;
		JANUS_LOG(LOG_WARN, "[AudioCall] Ending cross-server call of %s: %s\n", session->username ? session->username : "?", reason);
		janus_audiocall_remote_hangup_local(session, reason);
		janus_refcount_decrease(&session->ref);
		temp = temp->next;
	}
	g_list_free(list);
}

/* Send every local user the list of online users (here and on all the other
 * servers), if it changed since the last time (or always, if force is TRUE).
 * Runs in the link thread. */
static void janus_audiocall_push_online(gboolean force) {
	json_t *info = janus_audiocall_peer_info();
	json_object_set_new(info, "list", janus_audiocall_local_users());
	char *signature = json_dumps(info, JSON_COMPACT | JSON_SORT_KEYS);
	if(!force && signature != NULL && link_last_online != NULL && !strcmp(signature, link_last_online)) {
		free(signature);
		json_decref(info);
		return;
	}
	free(link_last_online);
	link_last_online = signature;
	json_object_set_new(info, "event", json_string("online"));
	json_object_set_new(info, "server", json_string(link_server_name));
	if(link_public_host != NULL)
		json_object_set_new(info, "public_host", json_string(link_public_host));
	json_object_set_new(info, "ws_port", json_integer(link_ws_port));
	GList *list = NULL;
	janus_mutex_lock(&sessions_mutex);
	if(usernames != NULL) {
		GHashTableIter iter;
		gpointer value;
		g_hash_table_iter_init(&iter, usernames);
		while(g_hash_table_iter_next(&iter, NULL, &value)) {
			janus_audiocall_session *user = value;
			if(user != NULL && !g_atomic_int_get(&user->destroyed)) {
				janus_refcount_increase(&user->ref);
				list = g_list_prepend(list, user);
			}
		}
	}
	janus_mutex_unlock(&sessions_mutex);
	GList *temp = list;
	while(temp) {
		janus_audiocall_session *user = temp->data;
		janus_audiocall_push_result(user, json_deep_copy(info), NULL);
		janus_refcount_decrease(&user->ref);
		temp = temp->next;
	}
	g_list_free(list);
	json_decref(info);
}

/* Broadcast our announcement on every LAN interface, and send it to every known server directly */
static void janus_audiocall_link_announce(void) {
	if(link_fd < 0)
		return;
	json_t *hello = json_object();
	json_object_set_new(hello, "type", json_string("hello"));
	json_object_set_new(hello, "server", json_string(link_server_name));
	json_object_set_new(hello, "users", janus_audiocall_local_users());
	json_object_set_new(hello, "ws_port", json_integer(link_ws_port));
	if(link_public_host != NULL)
		json_object_set_new(hello, "public_host", json_string(link_public_host));
	char *text = json_dumps(hello, JSON_COMPACT);
	json_decref(hello);
	if(text == NULL)
		return;
	int sent = 0;
	struct ifaddrs *ifas = NULL, *ifa = NULL;
	if(getifaddrs(&ifas) == 0) {
		for(ifa = ifas; ifa != NULL; ifa = ifa->ifa_next) {
			if(ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
				continue;
			if(!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK) ||
					!(ifa->ifa_flags & IFF_BROADCAST) || ifa->ifa_broadaddr == NULL)
				continue;
			struct sockaddr_in dst;
			memcpy(&dst, ifa->ifa_broadaddr, sizeof(dst));
			dst.sin_family = AF_INET;
			dst.sin_port = htons(link_local_port);
			if(sendto(link_fd, text, strlen(text), 0, (struct sockaddr *)&dst, sizeof(dst)) >= 0)
				sent++;
		}
		freeifaddrs(ifas);
	}
	if(sent == 0) {
		/* No usable interface found: try the generic broadcast address */
		struct sockaddr_in dst;
		memset(&dst, 0, sizeof(dst));
		dst.sin_family = AF_INET;
		dst.sin_port = htons(link_local_port);
		dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
		sendto(link_fd, text, strlen(text), 0, (struct sockaddr *)&dst, sizeof(dst));
	}
	/* Unicast to the servers we already know: Wi-Fi may drop broadcasts, but it retries unicast */
	GList *addrs = NULL;
	janus_mutex_lock(&link_mutex);
	if(link_peers != NULL) {
		GHashTableIter iter;
		gpointer value;
		g_hash_table_iter_init(&iter, link_peers);
		while(g_hash_table_iter_next(&iter, NULL, &value)) {
			janus_audiocall_peer *peer = value;
			struct sockaddr_in *copy = g_malloc(sizeof(struct sockaddr_in));
			*copy = peer->addr;
			addrs = g_list_prepend(addrs, copy);
		}
	}
	janus_mutex_unlock(&link_mutex);
	GList *temp = addrs;
	while(temp) {
		sendto(link_fd, text, strlen(text), 0, (struct sockaddr *)temp->data, sizeof(struct sockaddr_in));
		temp = temp->next;
	}
	g_list_free_full(addrs, g_free);
	free(text);
}

/* Handle one JSON message received on the link (runs in the link thread) */
static void janus_audiocall_link_handle(json_t *msg, const struct sockaddr_in *from) {
	const char *type = json_string_value(json_object_get(msg, "type"));
	if(type == NULL)
		return;
	const char *name = json_string_value(json_object_get(msg, "server"));
	char addr[64];
	if(!strcmp(type, "hello")) {
		/* Another server's announcement (or our own, coming back from the broadcast) */
		if(name == NULL || !strcmp(name, link_server_name))
			return;
		json_t *users = json_object_get(msg, "users");
		const char *public_host = json_string_value(json_object_get(msg, "public_host"));
		json_int_t ws_port = json_integer_value(json_object_get(msg, "ws_port"));
		gboolean is_new = FALSE, moved = FALSE, changed = FALSE;
		janus_mutex_lock(&link_mutex);
		janus_audiocall_peer *peer = g_hash_table_lookup(link_peers, name);
		if(peer == NULL) {
			peer = g_malloc0(sizeof(janus_audiocall_peer));
			peer->name = g_strdup(name);
			peer->users = json_array();
			g_hash_table_insert(link_peers, peer->name, peer);
			is_new = TRUE;
		} else if(!janus_audiocall_same_addr(&peer->addr, from)) {
			moved = TRUE;
		}
		peer->addr = *from;
		peer->last_seen = g_get_monotonic_time();
		json_t *new_users = json_is_array(users) ? json_incref(users) : json_array();
		changed = is_new || !json_equal(peer->users, new_users);
		json_decref(peer->users);
		peer->users = new_users;
		if(g_strcmp0(peer->public_host, public_host) != 0) {
			g_free(peer->public_host);
			peer->public_host = g_strdup(public_host);
			changed = TRUE;
		}
		uint16_t new_ws_port = (ws_port > 0 && ws_port <= 65535) ? (uint16_t)ws_port : 8188;
		if(peer->ws_port != new_ws_port) {
			peer->ws_port = new_ws_port;
			changed = TRUE;
		}
		janus_mutex_unlock(&link_mutex);
		if(is_new) {
			JANUS_LOG(LOG_INFO, "[AudioCall] Discovered server '%s' at %s\n", name,
				janus_audiocall_addr_str(from, addr, sizeof(addr)));
			g_atomic_int_set(&link_dirty, 1);	/* Introduce ourselves right away */
		} else if(moved) {
			JANUS_LOG(LOG_WARN, "[AudioCall] Server '%s' is now at %s (two servers with the same server_name?)\n", name,
				janus_audiocall_addr_str(from, addr, sizeof(addr)));
		}
		if(changed)
			g_atomic_int_set(&link_online_dirty, 1);
	} else if(!strcmp(type, "discover")) {
		/* An app looking for a server: tell it how busy we are */
		json_t *users = janus_audiocall_local_users();
		json_t *reply = json_object();
		json_object_set_new(reply, "type", json_string("here"));
		json_object_set_new(reply, "server", json_string(link_server_name));
		json_object_set_new(reply, "users", json_integer(json_array_size(users)));
		json_object_set_new(reply, "ws_port", json_integer(link_ws_port));
		if(link_public_host != NULL)
			json_object_set_new(reply, "public_host", json_string(link_public_host));
		json_decref(users);
		janus_audiocall_link_send_to(from, reply);
		json_decref(reply);
		JANUS_LOG(LOG_VERB, "[AudioCall] Answered a discover from %s\n", janus_audiocall_addr_str(from, addr, sizeof(addr)));
	} else if(!strcmp(type, "call")) {
		/* Another server has a user calling one of OUR users */
		guint32 call_id = (guint32)json_integer_value(json_object_get(msg, "call_id"));
		const char *caller_name = json_string_value(json_object_get(msg, "from"));
		const char *to = json_string_value(json_object_get(msg, "to"));
		const char *sdp_type = json_string_value(json_object_get(msg, "sdp_type"));
		const char *sdp = json_string_value(json_object_get(msg, "sdp"));
		if(call_id == 0 || caller_name == NULL || to == NULL || sdp_type == NULL || sdp == NULL) {
			janus_audiocall_link_send_hangup(from, call_id, "Invalid call request");
			return;
		}
		char reason[256];
		if(!janus_audiocall_sdp_allowed(sdp, reason, sizeof(reason))) {
			janus_audiocall_link_send_hangup(from, call_id, reason);
			return;
		}
		janus_mutex_lock(&sessions_mutex);
		janus_audiocall_session *callee = usernames ? g_hash_table_lookup(usernames, to) : NULL;
		if(callee == NULL || g_atomic_int_get(&callee->destroyed)) {
			janus_mutex_unlock(&sessions_mutex);
			JANUS_LOG(LOG_WARN, "[AudioCall] Cross-server call %u from %s: '%s' is not registered here\n", call_id, caller_name, to);
			janus_audiocall_link_send_hangup(from, call_id, "User not found");
			return;
		}
		janus_refcount_increase(&callee->ref);
		gboolean busy = (callee->peer != NULL) || !g_atomic_int_compare_and_exchange(&callee->incall, 0, 1);
		janus_mutex_unlock(&sessions_mutex);
		if(busy) {
			JANUS_LOG(LOG_INFO, "[AudioCall] Cross-server call %u from %s: '%s' is busy\n", call_id, caller_name, to);
			janus_audiocall_link_send_hangup(from, call_id, "User busy");
			janus_refcount_decrease(&callee->ref);
			return;
		}
		janus_audiocall_session *existing = janus_audiocall_remote_get(call_id);
		if(existing != NULL) {
			/* Extremely unlikely: the same call_id is already in use here */
			janus_refcount_decrease(&existing->ref);
			g_atomic_int_set(&callee->incall, 0);
			janus_refcount_decrease(&callee->ref);
			janus_audiocall_link_send_hangup(from, call_id, "Call id already in use, please retry");
			return;
		}
		janus_audiocall_remote_add(callee, call_id, caller_name, from, name);
		callee->has_audio = TRUE;
		callee->has_video = FALSE;
		callee->has_data = FALSE;
		g_atomic_int_set(&callee->hangingup, 0);
		JANUS_LOG(LOG_INFO, "[AudioCall] Incoming cross-server call %u: %s (server '%s') -> %s\n",
			call_id, caller_name, name ? name : "?", to);
		json_t *result = json_object();
		json_object_set_new(result, "event", json_string("incomingcall"));
		json_object_set_new(result, "username", json_string(caller_name));
		json_object_set_new(result, "remote", json_true());
		janus_audiocall_push_result(callee, result, json_pack("{ssss}", "type", sdp_type, "sdp", sdp));
		janus_refcount_decrease(&callee->ref);
	} else if(!strcmp(type, "accepted") || !strcmp(type, "hangup")) {
		guint32 call_id = (guint32)json_integer_value(json_object_get(msg, "call_id"));
		janus_audiocall_session *session = janus_audiocall_remote_get(call_id);
		if(session == NULL) {
			if(!strcmp(type, "accepted"))
				janus_audiocall_link_send_hangup(from, call_id, "Unknown call");
			return;
		}
		/* Only the server the call was set up with can answer or end it */
		janus_mutex_lock(&session->mutex);
		gboolean ok = (session->remote_addr.sin_addr.s_addr == from->sin_addr.s_addr);
		char *remote_user = g_strdup(session->remote_username ? session->remote_username : "");
		janus_mutex_unlock(&session->mutex);
		if(!ok) {
			JANUS_LOG(LOG_WARN, "[AudioCall] Ignoring '%s' for call %u from unexpected address %s\n", type, call_id,
				janus_audiocall_addr_str(from, addr, sizeof(addr)));
		} else if(!strcmp(type, "accepted")) {
			/* The other server's user answered a call made by one of OUR users */
			const char *sdp_type = json_string_value(json_object_get(msg, "sdp_type"));
			const char *sdp = json_string_value(json_object_get(msg, "sdp"));
			if(sdp_type == NULL || sdp == NULL) {
				janus_audiocall_remote_hangup_local(session, "Peer answered without SDP");
				janus_audiocall_link_send_hangup(from, call_id, "Missing SDP");
			} else {
				JANUS_LOG(LOG_INFO, "[AudioCall] Cross-server call %u: %s accepted the call from %s\n", call_id, remote_user, session->username);
				json_t *result = json_object();
				json_object_set_new(result, "event", json_string("accepted"));
				json_object_set_new(result, "username", json_string(remote_user));
				janus_audiocall_push_result(session, result, json_pack("{ssss}", "type", sdp_type, "sdp", sdp));
			}
		} else {
			/* The other server ended (or refused) a cross-server call */
			const char *reason = json_string_value(json_object_get(msg, "reason"));
			JANUS_LOG(LOG_INFO, "[AudioCall] Cross-server call %u ended by server '%s': %s\n", call_id, name ? name : "?", reason ? reason : "?");
			janus_audiocall_remote_hangup_local(session, reason ? reason : "Remote hangup");
		}
		g_free(remote_user);
		janus_refcount_decrease(&session->ref);
	} else {
		JANUS_LOG(LOG_WARN, "[AudioCall] Unknown link message type '%s' from %s\n", type,
			janus_audiocall_addr_str(from, addr, sizeof(addr)));
	}
}

/* Drop the servers we haven't heard from for a while, and end our calls with them */
static void janus_audiocall_link_expire_peers(void) {
	gint64 now = g_get_monotonic_time();
	GList *gone = NULL;
	janus_mutex_lock(&link_mutex);
	GHashTableIter iter;
	gpointer value;
	g_hash_table_iter_init(&iter, link_peers);
	while(g_hash_table_iter_next(&iter, NULL, &value)) {
		janus_audiocall_peer *peer = value;
		if(now - peer->last_seen > JANUS_AUDIOCALL_LINK_DEAD_AFTER) {
			gone = g_list_prepend(gone, g_strdup(peer->name));
			g_hash_table_iter_remove(&iter);	/* Frees the peer */
		}
	}
	janus_mutex_unlock(&link_mutex);
	GList *temp = gone;
	while(temp) {
		JANUS_LOG(LOG_WARN, "[AudioCall] Server '%s' is gone: no announcements for %d seconds\n",
			(char *)temp->data, (int)(JANUS_AUDIOCALL_LINK_DEAD_AFTER / G_TIME_SPAN_SECOND));
		janus_audiocall_remote_end_peer((char *)temp->data, "Peer server unreachable");
		g_atomic_int_set(&link_online_dirty, 1);
		temp = temp->next;
	}
	g_list_free_full(gone, g_free);
}

/* Thread that announces this server, receives link messages and relays cross-server media */
static void *janus_audiocall_link_thread(void *data) {
	JANUS_LOG(LOG_INFO, "[AudioCall] Link thread started: UDP %d, finding other servers by broadcast\n", link_local_port);
	static char buffer[65536];	/* SDPs can be bigger than one Ethernet frame */
	gint64 last_hello = 0;
	while(g_atomic_int_get(&initialized) && !g_atomic_int_get(&stopping)) {
		gint64 now = g_get_monotonic_time();
		gboolean dirty = g_atomic_int_compare_and_exchange(&link_dirty, 1, 0);
		if(dirty || now - last_hello >= JANUS_AUDIOCALL_LINK_HELLO_EVERY) {
			last_hello = now;
			janus_audiocall_link_announce();
			janus_audiocall_link_expire_peers();
			if(dirty)
				g_atomic_int_set(&link_online_dirty, 1);
		}
		if(g_atomic_int_compare_and_exchange(&link_online_dirty, 1, 0))
			janus_audiocall_push_online(FALSE);
		struct pollfd pfd = { .fd = link_fd, .events = POLLIN, .revents = 0 };
		int res = poll(&pfd, 1, 200);
		if(res < 0) {
			if(errno == EINTR)
				continue;
			JANUS_LOG(LOG_ERR, "[AudioCall] Link poll error: %s\n", g_strerror(errno));
			break;
		}
		if(res == 0 || !(pfd.revents & POLLIN))
			continue;
		struct sockaddr_in from;
		socklen_t fromlen = sizeof(from);
		ssize_t len = recvfrom(link_fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&from, &fromlen);
		if(len <= 0)
			continue;
		if(buffer[0] == 'A' || buffer[0] == 'V' || buffer[0] == 'K') {
			/* Media of a cross-server call: 'A'/'V' + call_id (4 bytes) + RTP packet,
			 * or a keyframe request: 'K' + call_id */
			gboolean keyframe = (buffer[0] == 'K');
			if(len < (keyframe ? 5 : 5 + 12))
				continue;
			gboolean video = (buffer[0] == 'V');
#ifndef IC_VIDEO
			if(video || keyframe)
				continue;	/* the audio plugin never relays video */
#endif
			guint32 nid = 0;
			memcpy(&nid, buffer + 1, sizeof(nid));
			guint32 call_id = ntohl(nid);
			janus_audiocall_session *session = janus_audiocall_remote_get(call_id);
			if(session == NULL)
				continue;
			if(!janus_audiocall_same_addr(&session->remote_addr, &from) ||
					g_atomic_int_get(&session->destroyed) || g_atomic_int_get(&session->hangingup)) {
				/* Not from the server this call is with (or the call is ending): drop it */
				janus_refcount_decrease(&session->ref);
				continue;
			}
			if(keyframe) {
				/* The other server's user needs a keyframe: ask OUR user (the video sender) for one */
				gateway->send_pli(session->handle);
			} else {
				/* First video packet of this call: ask the sender for a keyframe right away */
				if(video && !session->remote_video_seen) {
					session->remote_video_seen = TRUE;
					janus_audiocall_link_send_keyframe_request(&from, call_id);
				}
				janus_plugin_rtp packet;
				janus_plugin_rtp_reset(&packet);
				packet.mindex = -1;
				packet.video = video;
				packet.buffer = buffer + 5;
				packet.length = (uint16_t)(len - 5);
				gateway->relay_rtp(session->handle, &packet);
			}
			janus_refcount_decrease(&session->ref);
			continue;
		}
		buffer[len] = '\0';
		if(buffer[0] != '{')
			continue;
		json_error_t jerror;
		json_t *msg = json_loads(buffer, 0, &jerror);
		if(msg == NULL) {
			JANUS_LOG(LOG_WARN, "[AudioCall] Invalid JSON on the link: %s\n", jerror.text);
			continue;
		}
		janus_audiocall_link_handle(msg, &from);
		json_decref(msg);
	}
	JANUS_LOG(LOG_INFO, "[AudioCall] Link thread stopped\n");
	return NULL;
}

/* Create and bind the link UDP socket (broadcast enabled) */
static int janus_audiocall_link_setup(void) {
	link_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(link_fd < 0) {
		JANUS_LOG(LOG_ERR, "[AudioCall] Couldn't create the link socket: %s\n", g_strerror(errno));
		return -1;
	}
	int yes = 1;
	setsockopt(link_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	if(setsockopt(link_fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) < 0)
		JANUS_LOG(LOG_WARN, "[AudioCall] Couldn't enable broadcast on the link socket: %s\n", g_strerror(errno));
	struct sockaddr_in local;
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_port = htons(link_local_port);
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	if(bind(link_fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
		JANUS_LOG(LOG_ERR, "[AudioCall] Couldn't bind the link to UDP port %d: %s\n", link_local_port, g_strerror(errno));
		close(link_fd);
		link_fd = -1;
		return -1;
	}
	return 0;
}

/* Plugin implementation */
int janus_audiocall_init(janus_callbacks *callback, const char *config_path) {
	if(g_atomic_int_get(&stopping)) {
		/* Still stopping from before */
		return -1;
	}
	if(callback == NULL || config_path == NULL) {
		/* Invalid arguments */
		return -1;
	}

	/* Read configuration */
	char filename[255];
	g_snprintf(filename, 255, "%s/%s.jcfg", config_path, JANUS_AUDIOCALL_PACKAGE);
	JANUS_LOG(LOG_VERB, "Configuration file: %s\n", filename);
	janus_config *config = janus_config_parse(filename);
	if(config == NULL) {
		JANUS_LOG(LOG_WARN, "Couldn't find .jcfg configuration file (%s), trying .cfg\n", JANUS_AUDIOCALL_PACKAGE);
		g_snprintf(filename, 255, "%s/%s.cfg", config_path, JANUS_AUDIOCALL_PACKAGE);
		JANUS_LOG(LOG_VERB, "Configuration file: %s\n", filename);
		config = janus_config_parse(filename);
	}
	if(config != NULL) {
		janus_config_print(config);
		janus_config_category *config_general = janus_config_get_create(config, NULL, janus_config_type_category, "general");
		janus_config_item *events = janus_config_get(config, config_general, janus_config_type_item, "events");
		if(events != NULL && events->value != NULL)
			notify_events = janus_is_true(events->value);
		if(!notify_events && callback->events_is_enabled()) {
			JANUS_LOG(LOG_WARN, "Notification of events to handlers disabled for %s\n", JANUS_AUDIOCALL_NAME);
		}
		/* IC: peer link settings */
		janus_config_item *item = janus_config_get(config, config_general, janus_config_type_item, "server_name");
		if(item != NULL && item->value != NULL)
			link_server_name = g_strdup(item->value);
		item = janus_config_get(config, config_general, janus_config_type_item, "link_port");
		if(item != NULL && item->value != NULL && janus_string_to_uint16(item->value, &link_local_port) < 0)
			JANUS_LOG(LOG_WARN, "[AudioCall] Invalid link_port '%s', using %d\n", item->value, link_local_port);
		item = janus_config_get(config, config_general, janus_config_type_item, "ws_port");
		if(item != NULL && item->value != NULL && janus_string_to_uint16(item->value, &link_ws_port) < 0)
			JANUS_LOG(LOG_WARN, "[AudioCall] Invalid ws_port '%s', using %d\n", item->value, link_ws_port);
		item = janus_config_get(config, config_general, janus_config_type_item, "public_host");
		if(item != NULL && item->value != NULL && strlen(item->value) > 0)
			link_public_host = g_strdup(item->value);
		if(janus_config_get(config, config_general, janus_config_type_item, "peer_host") != NULL ||
				janus_config_get(config, config_general, janus_config_type_item, "peer_port") != NULL) {
			JANUS_LOG(LOG_WARN, "[AudioCall] peer_host/peer_port are no longer used (other servers are found automatically), ignoring them\n");
		}
	}
	/* The link is on unless link_port is 0 */
	link_enabled = (link_local_port != 0);
	if(link_server_name == NULL)
		link_server_name = g_strdup(g_get_host_name());
	janus_config_destroy(config);
	config = NULL;

	sessions = g_hash_table_new_full(NULL, NULL, NULL, (GDestroyNotify)janus_audiocall_session_destroy);
	remote_calls = g_hash_table_new_full(NULL, NULL, NULL, (GDestroyNotify)janus_audiocall_session_unref);
	usernames = g_hash_table_new_full(g_str_hash, g_str_equal,
		(GDestroyNotify)g_free, (GDestroyNotify)janus_audiocall_session_unref);
	messages = g_async_queue_new_full((GDestroyNotify) janus_audiocall_message_free);
	/* This is the callback we'll need to invoke to contact the Janus core */
	gateway = callback;

	g_atomic_int_set(&initialized, 1);

	/* Launch the thread that will handle incoming messages */
	GError *error = NULL;
	handler_thread = g_thread_try_new("audiocall handler", janus_audiocall_handler, NULL, &error);
	if(error != NULL) {
		g_atomic_int_set(&initialized, 0);
		JANUS_LOG(LOG_ERR, "Got error %d (%s) trying to launch the AudioCall handler thread...\n",
			error->code, error->message ? error->message : "??");
		g_error_free(error);
		return -1;
	}
	/* IC: start the peer link, if configured */
	if(!link_enabled) {
		JANUS_LOG(LOG_INFO, "[AudioCall] link_port is 0: link disabled, local calls only\n");
	} else {
		link_peers = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify)janus_audiocall_peer_free);
		if(janus_audiocall_link_setup() < 0) {
			JANUS_LOG(LOG_ERR, "[AudioCall] Peer link disabled, local calls only\n");
			link_enabled = FALSE;
		} else {
			link_thread = g_thread_try_new("audiocall link", janus_audiocall_link_thread, NULL, &error);
			if(error != NULL) {
				JANUS_LOG(LOG_ERR, "[AudioCall] Couldn't start the peer link thread: %s\n",
					error->message ? error->message : "??");
				g_error_free(error);
				error = NULL;
				close(link_fd);
				link_fd = -1;
				link_enabled = FALSE;
			}
		}
	}
	JANUS_LOG(LOG_INFO, "%s initialized! (server '%s')\n", JANUS_AUDIOCALL_NAME, link_server_name);
	if(link_public_host != NULL)
		JANUS_LOG(LOG_INFO, "[AudioCall] Apps on the internet reach this server at %s:%d (public_host)\n",
			link_public_host, link_ws_port);
	return 0;
}

void janus_audiocall_destroy(void) {
	if(!g_atomic_int_get(&initialized))
		return;
	g_atomic_int_set(&stopping, 1);

	g_async_queue_push(messages, &exit_message);
	if(handler_thread != NULL) {
		g_thread_join(handler_thread);
		handler_thread = NULL;
	}
	/* IC: stop the peer link */
	if(link_thread != NULL) {
		g_thread_join(link_thread);
		link_thread = NULL;
	}
	if(link_fd >= 0) {
		close(link_fd);
		link_fd = -1;
	}
	janus_mutex_lock(&link_mutex);
	if(link_peers != NULL) {
		g_hash_table_destroy(link_peers);
		link_peers = NULL;
	}
	janus_mutex_unlock(&link_mutex);
	free(link_last_online);
	link_last_online = NULL;
	g_free(link_server_name);
	link_server_name = NULL;
	g_free(link_public_host);
	link_public_host = NULL;
	link_enabled = FALSE;
	janus_mutex_lock(&remote_calls_mutex);
	if(remote_calls != NULL) {
		g_hash_table_destroy(remote_calls);
		remote_calls = NULL;
	}
	janus_mutex_unlock(&remote_calls_mutex);
	/* FIXME We should destroy the sessions cleanly */
	janus_mutex_lock(&sessions_mutex);
	g_hash_table_destroy(sessions);
	sessions = NULL;
	g_hash_table_destroy(usernames);
	usernames = NULL;
	janus_mutex_unlock(&sessions_mutex);
	g_async_queue_unref(messages);
	messages = NULL;
	g_atomic_int_set(&initialized, 0);
	g_atomic_int_set(&stopping, 0);
	JANUS_LOG(LOG_INFO, "%s destroyed!\n", JANUS_AUDIOCALL_NAME);
}

int janus_audiocall_get_api_compatibility(void) {
	/* Important! This is what your plugin MUST always return: don't lie here or bad things will happen */
	return JANUS_PLUGIN_API_VERSION;
}

int janus_audiocall_get_version(void) {
	return JANUS_AUDIOCALL_VERSION;
}

const char *janus_audiocall_get_version_string(void) {
	return JANUS_AUDIOCALL_VERSION_STRING;
}

const char *janus_audiocall_get_description(void) {
	return JANUS_AUDIOCALL_DESCRIPTION;
}

const char *janus_audiocall_get_name(void) {
	return JANUS_AUDIOCALL_NAME;
}

const char *janus_audiocall_get_author(void) {
	return JANUS_AUDIOCALL_AUTHOR;
}

const char *janus_audiocall_get_package(void) {
	return JANUS_AUDIOCALL_PACKAGE;
}

static janus_audiocall_session *janus_audiocall_lookup_session(janus_plugin_session *handle) {
	janus_audiocall_session *session = NULL;
	if(g_hash_table_contains(sessions, handle)) {
		session = (janus_audiocall_session *)handle->plugin_handle;
	}
	return session;
}

void janus_audiocall_create_session(janus_plugin_session *handle, int *error) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized)) {
		*error = -1;
		return;
	}
	janus_audiocall_session *session = g_malloc0(sizeof(janus_audiocall_session));
	session->handle = handle;
	session->has_audio = FALSE;
	session->has_video = FALSE;
	session->has_data = FALSE;
	session->audio_active = TRUE;
	session->video_active = TRUE;
	session->bitrate = 0;	/* No limit */
	session->peer_bitrate = 0;
	session->peer = NULL;
	session->username = NULL;
	janus_rtp_switching_context_reset(&session->context);
	janus_rtp_simulcasting_context_reset(&session->sim_context);
	janus_vp8_simulcast_context_reset(&session->vp8_context);
	janus_mutex_init(&session->mutex);
	janus_mutex_init(&session->rec_mutex);
	janus_mutex_init(&session->rid_mutex);
	g_atomic_int_set(&session->incall, 0);
	g_atomic_int_set(&session->hangingup, 0);
	g_atomic_int_set(&session->destroyed, 0);
	handle->plugin_handle = session;
	janus_refcount_init(&session->ref, janus_audiocall_session_free);

	janus_mutex_lock(&sessions_mutex);
	g_hash_table_insert(sessions, handle, session);
	janus_mutex_unlock(&sessions_mutex);

	return;
}

void janus_audiocall_destroy_session(janus_plugin_session *handle, int *error) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized)) {
		*error = -1;
		return;
	}
	janus_mutex_lock(&sessions_mutex);
	janus_audiocall_session *session = janus_audiocall_lookup_session(handle);
	if(!session) {
		janus_mutex_unlock(&sessions_mutex);
		JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
		*error = -2;
		return;
	}
	JANUS_LOG(LOG_VERB, "Removing AudioCall user %s session...\n", session->username ? session->username : "'unknown'");
	janus_audiocall_hangup_media_internal(handle);
	if(session->username != NULL) {
		int res = g_hash_table_remove(usernames, (gpointer)session->username);
		JANUS_LOG(LOG_VERB, "  -- Removed: %d\n", res);
		g_atomic_int_set(&link_dirty, 1);	/* IC: tell the other servers */
	}
	g_hash_table_remove(sessions, handle);
	janus_mutex_unlock(&sessions_mutex);
	return;
}

json_t *janus_audiocall_query_session(janus_plugin_session *handle) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized)) {
		return NULL;
	}
	janus_mutex_lock(&sessions_mutex);
	janus_audiocall_session *session = janus_audiocall_lookup_session(handle);
	if(!session) {
		janus_mutex_unlock(&sessions_mutex);
		JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
		return NULL;
	}
	janus_refcount_increase(&session->ref);
	janus_mutex_unlock(&sessions_mutex);
	/* Provide some generic info, e.g., if we're in a call and with whom */
	janus_audiocall_session *peer = session->peer;
	json_t *info = json_object();
	json_object_set_new(info, "state", json_string(session->peer ? "incall" : "idle"));
	json_object_set_new(info, "username", session->username ? json_string(session->username) : NULL);
	if(peer) {
		json_object_set_new(info, "peer", peer->username ? json_string(peer->username) : NULL);
		json_object_set_new(info, "audio_active", session->audio_active ? json_true() : json_false());
		json_object_set_new(info, "video_active", session->video_active ? json_true() : json_false());
		if(session->acodec != JANUS_AUDIOCODEC_NONE) {
			json_object_set_new(info, "audio_codec", json_string(janus_audiocodec_name(session->acodec)));
			if(session->opusred_pt)
				json_object_set_new(info, "audio_red", json_true());
		}
		if(session->vcodec != JANUS_VIDEOCODEC_NONE)
			json_object_set_new(info, "video_codec", json_string(janus_videocodec_name(session->vcodec)));
		json_object_set_new(info, "video_active", session->video_active ? json_true() : json_false());
		json_object_set_new(info, "bitrate", json_integer(session->bitrate));
		json_object_set_new(info, "peer-bitrate", json_integer(session->peer_bitrate));
		json_object_set_new(info, "slowlink_count", json_integer(session->slowlink_count));
	}
	if(session->ssrc[0] != 0 || session->rid[0] != NULL) {
		json_object_set_new(info, "simulcast", json_true());
	}
	if(peer && (peer->ssrc[0] != 0 || peer->rid[0] != NULL)) {
		json_object_set_new(info, "simulcast-peer", json_true());
		json_object_set_new(info, "substream", json_integer(session->sim_context.substream));
		json_object_set_new(info, "substream-target", json_integer(session->sim_context.substream_target));
		json_object_set_new(info, "temporal-layer", json_integer(session->sim_context.templayer));
		json_object_set_new(info, "temporal-layer-target", json_integer(session->sim_context.templayer_target));
		if(session->sim_context.drop_trigger > 0)
			json_object_set_new(info, "fallback", json_integer(session->sim_context.drop_trigger));
	}
	if(session->arc || session->vrc || session->drc) {
		json_t *recording = json_object();
		if(session->arc && session->arc->filename)
			json_object_set_new(recording, "audio", json_string(session->arc->filename));
		if(session->vrc && session->vrc->filename)
			json_object_set_new(recording, "video", json_string(session->vrc->filename));
		if(session->drc && session->drc->filename)
			json_object_set_new(recording, "data", json_string(session->drc->filename));
		json_object_set_new(info, "recording", recording);
	}
	json_object_set_new(info, "incall", json_integer(g_atomic_int_get(&session->incall)));
	if(session->e2ee)
		json_object_set_new(info, "e2ee", json_true());
	json_object_set_new(info, "hangingup", json_integer(g_atomic_int_get(&session->hangingup)));
	json_object_set_new(info, "destroyed", json_integer(g_atomic_int_get(&session->destroyed)));
	janus_refcount_decrease(&session->ref);
	return info;
}

struct janus_plugin_result *janus_audiocall_handle_message(janus_plugin_session *handle, char *transaction, json_t *message, json_t *jsep) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return janus_plugin_result_new(JANUS_PLUGIN_ERROR, g_atomic_int_get(&stopping) ? "Shutting down" : "Plugin not initialized", NULL);

	janus_mutex_lock(&sessions_mutex);
	janus_audiocall_session *session = janus_audiocall_lookup_session(handle);
	if(!session) {
		janus_mutex_unlock(&sessions_mutex);
		return janus_plugin_result_new(JANUS_PLUGIN_ERROR, "No session associated with this handle", NULL);
	}
	/* Increase the reference counter for this session: we'll decrease it after we handle the message */
	janus_refcount_increase(&session->ref);
	janus_mutex_unlock(&sessions_mutex);

	janus_audiocall_message *msg = g_malloc(sizeof(janus_audiocall_message));
	msg->handle = handle;
	msg->transaction = transaction;
	msg->message = message;
	msg->jsep = jsep;
	g_async_queue_push(messages, msg);

	/* All the requests to this plugin are handled asynchronously */
	return janus_plugin_result_new(JANUS_PLUGIN_OK_WAIT, NULL, NULL);
}

void janus_audiocall_setup_media(janus_plugin_session *handle) {
	JANUS_LOG(LOG_INFO, "[%s-%p] WebRTC media is now available\n", JANUS_AUDIOCALL_PACKAGE, handle);
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return;
	janus_mutex_lock(&sessions_mutex);
	janus_audiocall_session *session = janus_audiocall_lookup_session(handle);
	if(!session) {
		janus_mutex_unlock(&sessions_mutex);
		JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
		return;
	}
	if(g_atomic_int_get(&session->destroyed)) {
		janus_mutex_unlock(&sessions_mutex);
		return;
	}
	g_atomic_int_set(&session->hangingup, 0);
	janus_mutex_unlock(&sessions_mutex);
	/* We really don't care, as we only relay RTP/RTCP we get in the first place anyway */
}

void janus_audiocall_incoming_rtp(janus_plugin_session *handle, janus_plugin_rtp *packet) {
	if(handle == NULL || g_atomic_int_get(&handle->stopped) || g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return;
	if(gateway) {
		/* Honour the audio/video active flags */
		janus_audiocall_session *session = (janus_audiocall_session *)handle->plugin_handle;
		if(!session) {
			JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
			return;
		}
		/* IC: cross-server call, forward our user's media to the peer server */
		if(session->remote_call) {
			guint32 call_id = session->call_id;
			if(call_id == 0 || g_atomic_int_get(&session->destroyed))
				return;
#ifdef IC_VIDEO
			if(packet->video ? session->video_active : session->audio_active)
				janus_audiocall_link_send_rtp(&session->remote_addr, call_id, packet->video, packet->buffer, packet->length);
#else
			if(!packet->video && session->audio_active)
				janus_audiocall_link_send_rtp(&session->remote_addr, call_id, FALSE, packet->buffer, packet->length);
#endif
			return;
		}
		janus_audiocall_session *peer = session->peer;
		if(!peer) {
			JANUS_LOG(LOG_ERR, "Session has no peer...\n");
			return;
		}
		if(g_atomic_int_get(&session->destroyed) || g_atomic_int_get(&peer->destroyed))
			return;
#ifndef IC_VIDEO
		/* IC: audio-only plugin, never relay video packets */
		if(packet->video)
			return;
#endif
		gboolean video = packet->video;
		char *buf = packet->buffer;
		uint16_t len = packet->length;
		if(video && session->video_active && (session->ssrc[0] != 0 || session->rid[0] != NULL)) {
			/* Handle simulcast: backup the header information first */
			janus_rtp_header *header = (janus_rtp_header *)buf;
			uint32_t seq_number = ntohs(header->seq_number);
			uint32_t timestamp = ntohl(header->timestamp);
			uint32_t ssrc = ntohl(header->ssrc);
			/* Process this packet: don't relay if it's not the SSRC/layer we wanted to handle
			 * The caveat is that the targets in OUR simulcast context are the PEER's targets */
			gboolean relay = janus_rtp_simulcasting_context_process_rtp(&peer->sim_context,
				buf, len, packet->extensions.dd_content, packet->extensions.dd_len,
				session->ssrc, session->rid, session->vcodec, &peer->context, &session->rid_mutex);
			/* Do we need to drop this? */
			if(!relay)
				return;
			if(peer->sim_context.need_pli) {
				/* Send a PLI */
				JANUS_LOG(LOG_VERB, "We need a PLI for the simulcast context\n");
				gateway->send_pli(session->handle);
			}
			/* Any event we should notify? */
			if(peer->sim_context.changed_substream) {
				/* Notify the user about the substream change */
				json_t *event = json_object();
				json_object_set_new(event, "audiocall", json_string("event"));
				json_t *result = json_object();
				json_object_set_new(result, "event", json_string("simulcast"));
				json_object_set_new(result, "videocodec", json_string(janus_videocodec_name(session->vcodec)));
				json_object_set_new(result, "substream", json_integer(peer->sim_context.substream));
				json_object_set_new(event, "result", result);
				gateway->push_event(peer->handle, &janus_audiocall_plugin, NULL, event, NULL);
				json_decref(event);
			}
			if(peer->sim_context.changed_temporal) {
				/* Notify the user about the temporal layer change */
				json_t *event = json_object();
				json_object_set_new(event, "audiocall", json_string("event"));
				json_t *result = json_object();
				json_object_set_new(result, "event", json_string("simulcast"));
				json_object_set_new(result, "videocodec", json_string(janus_videocodec_name(session->vcodec)));
				json_object_set_new(result, "temporal", json_integer(peer->sim_context.templayer));
				json_object_set_new(event, "result", result);
				gateway->push_event(peer->handle, &janus_audiocall_plugin, NULL, event, NULL);
				json_decref(event);
			}
			/* If we got here, update the RTP header and send the packet */
			janus_rtp_header_update(header, &peer->context, TRUE, 0);
			if(session->vcodec == JANUS_VIDEOCODEC_VP8) {
				int plen = 0;
				char *payload = janus_rtp_payload(buf, len, &plen);
				janus_vp8_simulcast_descriptor_update(payload, plen, &peer->vp8_context, peer->sim_context.changed_substream);
			}
			/* Save the frame if we're recording (and make sure the SSRC never changes even if the substream does) */
			header->ssrc = htonl(1);
			janus_recorder_save_frame(session->vrc, buf, len);
			/* Send the frame back */
			gateway->relay_rtp(peer->handle, packet);
			/* Restore header or core statistics will be messed up */
			header->ssrc = htonl(ssrc);
			header->timestamp = htonl(timestamp);
			header->seq_number = htons(seq_number);
		} else {
			if((!video && session->audio_active) || (video && session->video_active)) {
				/* Save the frame if we're recording */
				janus_recorder_save_frame(video ? session->vrc : session->arc, buf, len);
				/* Forward the packet to the peer */
				gateway->relay_rtp(peer->handle, packet);
			}
		}
	}
}

void janus_audiocall_incoming_rtcp(janus_plugin_session *handle, janus_plugin_rtcp *packet) {
	if(handle == NULL || g_atomic_int_get(&handle->stopped) || g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return;
	if(gateway) {
		janus_audiocall_session *session = (janus_audiocall_session *)handle->plugin_handle;
		if(!session) {
			JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
			return;
		}
		/* IC: cross-server call. RTCP stays on each server (Janus handles it per
		 * PeerConnection), except keyframe requests: if our user can't decode the
		 * peer's video, ask the peer server to get a keyframe from its user. */
		if(session->remote_call) {
#ifdef IC_VIDEO
			if(packet->video && (janus_rtcp_has_pli(packet->buffer, packet->length) ||
					janus_rtcp_has_fir(packet->buffer, packet->length))) {
				guint32 call_id = session->call_id;
				if(call_id != 0)
					janus_audiocall_link_send_keyframe_request(&session->remote_addr, call_id);
			}
#endif
			return;
		}
		janus_audiocall_session *peer = session->peer;
		if(!peer) {
			JANUS_LOG(LOG_ERR, "Session has no peer...\n");
			return;
		}
		if(g_atomic_int_get(&session->destroyed) || g_atomic_int_get(&peer->destroyed))
			return;
		guint32 bitrate = janus_rtcp_get_remb(packet->buffer, packet->length);
		if(bitrate > 0) {
			/* If a REMB arrived, make sure we cap it to our configuration, and send it as a video RTCP */
			session->peer_bitrate = bitrate;
			/* No limit ~= 10000000 */
			gateway->send_remb(handle, session->bitrate ? session->bitrate : 10000000);
			return;
		}
		gateway->relay_rtcp(peer->handle, packet);
	}
}

void janus_audiocall_incoming_data(janus_plugin_session *handle, janus_plugin_data *packet) {
	if(handle == NULL || g_atomic_int_get(&handle->stopped) || g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return;
	/* IC: audio-only plugin, data channels are not relayed */
	return;
	if(gateway) {
		janus_audiocall_session *session = (janus_audiocall_session *)handle->plugin_handle;
		if(!session) {
			JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
			return;
		}
		janus_audiocall_session *peer = session->peer;
		if(!peer) {
			JANUS_LOG(LOG_ERR, "Session has no peer...\n");
			return;
		}
		if(g_atomic_int_get(&session->destroyed) || g_atomic_int_get(&peer->destroyed) ||
				!g_atomic_int_get(&peer->dataready))
			return;
		if(packet->buffer == NULL || packet->length == 0)
			return;
		char *label = packet->label;
		char *buf = packet->buffer;
		uint16_t len = packet->length;
		JANUS_LOG(LOG_VERB, "Got a %s DataChannel message (%d bytes) to forward\n",
			!packet->binary ? "text" : "binary", len);
		/* Save the frame if we're recording */
		janus_recorder_save_frame(session->drc, buf, len);
		/* Forward the packet to the peer */
		janus_plugin_data r = {
			.label = label,
			.protocol = NULL,
			.binary = packet->binary,
			.buffer = buf,
			.length = len
		};
		gateway->relay_data(peer->handle, &r);
	}
}

void janus_audiocall_data_ready(janus_plugin_session *handle) {
	if(handle == NULL || g_atomic_int_get(&handle->stopped) ||
			g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized) || !gateway)
		return;
	/* Data channels are writable */
	janus_audiocall_session *session = (janus_audiocall_session *)handle->plugin_handle;
	if(!session || g_atomic_int_get(&session->destroyed) || g_atomic_int_get(&session->hangingup))
		return;
	if(g_atomic_int_compare_and_exchange(&session->dataready, 0, 1)) {
		JANUS_LOG(LOG_INFO, "[%s-%p] Data channel available\n", JANUS_AUDIOCALL_PACKAGE, handle);
	}
}

void janus_audiocall_slow_link(janus_plugin_session *handle, int mindex, gboolean video, gboolean uplink) {
	/* The core is informing us that our peer got or sent too many NACKs, are we pushing media too hard? */
	if(handle == NULL || g_atomic_int_get(&handle->stopped) || g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return;
	janus_audiocall_session *session = (janus_audiocall_session *)handle->plugin_handle;
	if(!session) {
		JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
		return;
	}
	if(g_atomic_int_get(&session->destroyed))
		return;
	session->slowlink_count++;
	if(uplink && !video && !session->audio_active) {
		/* We're not relaying audio and the peer is expecting it, so NACKs are normal */
		JANUS_LOG(LOG_VERB, "Getting a lot of lost packets (slow uplink) for audio, but that's expected, a configure disabled the audio forwarding\n");
	} else if(uplink && video && !session->video_active) {
		/* We're not relaying video and the peer is expecting it, so NACKs are normal */
		JANUS_LOG(LOG_VERB, "Getting a lot of lost packets (slow uplink) for video, but that's expected, a configure disabled the video forwarding\n");
	} else {
		JANUS_LOG(LOG_WARN, "Getting a lot of lost packets (slow %s) for %s\n",
			uplink ? "uplink" : "downlink", video ? "video" : "audio");
		if(!uplink) {
			/* Send an event on the handle to notify the application: it's
			 * up to the application to then choose a policy and enforce it */
			json_t *event = json_object();
			json_object_set_new(event, "audiocall", json_string("event"));
			/* Also add info on what the current bitrate cap is */
			json_t *result = json_object();
			json_object_set_new(result, "event", json_string("slow_link"));
			json_object_set_new(result, "media", json_string(video ? "video" : "audio"));
			if(video)
				json_object_set_new(result, "current-bitrate", json_integer(session->bitrate));
			json_object_set_new(event, "result", result);
			gateway->push_event(session->handle, &janus_audiocall_plugin, NULL, event, NULL);
			json_decref(event);
		}
	}
}

static void janus_audiocall_recorder_close(janus_audiocall_session *session) {
	if(session->arc) {
		janus_recorder *rc = session->arc;
		session->arc = NULL;
		janus_recorder_close(rc);
		JANUS_LOG(LOG_INFO, "Closed audio recording %s\n", rc->filename ? rc->filename : "??");
		janus_recorder_destroy(rc);
	}
	if(session->vrc) {
		janus_recorder *rc = session->vrc;
		session->vrc = NULL;
		janus_recorder_close(rc);
		JANUS_LOG(LOG_INFO, "Closed video recording %s\n", rc->filename ? rc->filename : "??");
		janus_recorder_destroy(rc);
	}
	if(session->drc) {
		janus_recorder *rc = session->drc;
		session->drc = NULL;
		janus_recorder_close(rc);
		JANUS_LOG(LOG_INFO, "Closed data recording %s\n", rc->filename ? rc->filename : "??");
		janus_recorder_destroy(rc);
	}
}

void janus_audiocall_hangup_media(janus_plugin_session *handle) {
	JANUS_LOG(LOG_INFO, "[%s-%p] No WebRTC media anymore\n", JANUS_AUDIOCALL_PACKAGE, handle);
	janus_mutex_lock(&sessions_mutex);
	janus_audiocall_hangup_media_internal(handle);
	janus_mutex_unlock(&sessions_mutex);
}

static void janus_audiocall_hangup_media_internal(janus_plugin_session *handle) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return;
	janus_audiocall_session *session = janus_audiocall_lookup_session(handle);
	if(!session) {
		JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
		return;
	}
	if(g_atomic_int_get(&session->destroyed))
		return;
	if(!g_atomic_int_compare_and_exchange(&session->hangingup, 0, 1))
		return;
	g_atomic_int_set(&session->dataready, 0);
	/* IC: if in a cross-server call, tell the peer server */
	guint32 remote_call_id = 0;
	struct sockaddr_in remote_addr;
	if(janus_audiocall_remote_detach(session, &remote_call_id, NULL, &remote_addr)) {
		JANUS_LOG(LOG_INFO, "[AudioCall] Cross-server call %u ended: %s's PeerConnection went away\n",
			remote_call_id, session->username ? session->username : "?");
		janus_audiocall_link_send_hangup(&remote_addr, remote_call_id, "Remote WebRTC hangup");
	}
	/* Get rid of the recorders, if available */
	janus_mutex_lock(&session->rec_mutex);
	janus_audiocall_recorder_close(session);
	janus_mutex_unlock(&session->rec_mutex);
	janus_mutex_lock(&session->mutex);
	janus_audiocall_session *peer = session->peer;
	session->peer = NULL;
	if(peer) {
		/* Send event to our peer too */
		json_t *call = json_object();
		json_object_set_new(call, "audiocall", json_string("event"));
		json_t *calling = json_object();
		json_object_set_new(calling, "event", json_string("hangup"));
		json_object_set_new(calling, "username", json_string(session->username));
		json_object_set_new(calling, "reason", json_string("Remote WebRTC hangup"));
		json_object_set_new(call, "result", calling);
		gateway->close_pc(peer->handle);
		int ret = gateway->push_event(peer->handle, &janus_audiocall_plugin, NULL, call, NULL);
		JANUS_LOG(LOG_VERB, "  >> Pushing event to peer: %d (%s)\n", ret, janus_get_api_error(ret));
		json_decref(call);
		/* Also notify event handlers */
		if(notify_events && gateway->events_is_enabled()) {
			json_t *info = json_object();
			json_object_set_new(info, "event", json_string("hangup"));
			json_object_set_new(info, "reason", json_string("Remote WebRTC hangup"));
			gateway->notify_event(&janus_audiocall_plugin, peer->handle, info);
		}
		janus_refcount_decrease(&peer->ref);
	}
	janus_mutex_unlock(&session->mutex);
	/* Reset controls */
	session->has_audio = FALSE;
	session->has_video = FALSE;
	session->has_data = FALSE;
	session->audio_active = TRUE;
	session->video_active = TRUE;
	session->acodec = JANUS_AUDIOCODEC_NONE;
	session->vcodec = JANUS_VIDEOCODEC_NONE;
	session->opusred_pt = 0;
	session->bitrate = 0;
	session->peer_bitrate = 0;
	session->e2ee = FALSE;
	janus_rtp_simulcasting_cleanup(NULL, session->ssrc, session->rid, &session->rid_mutex);
	janus_rtp_switching_context_reset(&session->context);
	janus_rtp_simulcasting_context_reset(&session->sim_context);
	janus_vp8_simulcast_context_reset(&session->vp8_context);
	if(g_atomic_int_compare_and_exchange(&session->incall, 1, 0) && peer) {
		janus_refcount_decrease(&peer->ref);
	}
	janus_rtp_switching_context_reset(&session->context);
	g_atomic_int_set(&session->hangingup, 0);
}

/* IC: media policy. Returns TRUE if the SDP has an audio m-line and no active
 * m-line that is not allowed (audio build: no video/data; video build: no data);
 * otherwise writes why in reason. */
static gboolean janus_audiocall_sdp_allowed(const char *sdp_text, char *reason, size_t reason_len) {
	char error_str[512];
	janus_sdp *sdp = janus_sdp_parse(sdp_text, error_str, sizeof(error_str));
	if(sdp == NULL) {
		g_snprintf(reason, reason_len, "Error parsing SDP: %s", error_str);
		return FALSE;
	}
	gboolean has_audio = FALSE, has_other = FALSE;
	GList *temp = sdp->m_lines;
	while(temp) {
		janus_sdp_mline *m = (janus_sdp_mline *)temp->data;
		if(m->port > 0) {
			if(m->type == JANUS_SDP_AUDIO)
				has_audio = TRUE;
#ifdef IC_VIDEO
			else if(m->type == JANUS_SDP_VIDEO)
				;	/* video is allowed in the CustomVideoCall plugin */
#endif
			else if(m->direction != JANUS_SDP_INACTIVE)
				has_other = TRUE;
		}
		temp = temp->next;
	}
	janus_sdp_destroy(sdp);
	if(has_other) {
#ifdef IC_VIDEO
		g_snprintf(reason, reason_len, "AudioCall: data channels are not allowed");
#else
		g_snprintf(reason, reason_len, "AudioCall is audio-only: video and data are not allowed");
#endif
		return FALSE;
	}
	if(!has_audio) {
		g_snprintf(reason, reason_len, "AudioCall needs an audio stream in the SDP");
		return FALSE;
	}
	return TRUE;
}

/* Thread to handle incoming messages */
static void *janus_audiocall_handler(void *data) {
	JANUS_LOG(LOG_VERB, "Joining AudioCall handler thread\n");
	janus_audiocall_message *msg = NULL;
	int error_code = 0;
	char error_cause[512];
	json_t *root = NULL;
	while(g_atomic_int_get(&initialized) && !g_atomic_int_get(&stopping)) {
		msg = g_async_queue_pop(messages);
		if(msg == &exit_message)
			break;
		if(msg->handle == NULL) {
			janus_audiocall_message_free(msg);
			continue;
		}
		janus_mutex_lock(&sessions_mutex);
		janus_audiocall_session *session = janus_audiocall_lookup_session(msg->handle);
		if(!session) {
			janus_mutex_unlock(&sessions_mutex);
			JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
			janus_audiocall_message_free(msg);
			continue;
		}
		if(g_atomic_int_get(&session->destroyed)) {
			janus_mutex_unlock(&sessions_mutex);
			janus_audiocall_message_free(msg);
			continue;
		}
		janus_mutex_unlock(&sessions_mutex);
		/* Handle request */
		error_code = 0;
		root = msg->message;
		if(msg->message == NULL) {
			JANUS_LOG(LOG_ERR, "No message??\n");
			error_code = JANUS_AUDIOCALL_ERROR_NO_MESSAGE;
			g_snprintf(error_cause, 512, "%s", "No message??");
			goto error;
		}
		if(!json_is_object(root)) {
			JANUS_LOG(LOG_ERR, "JSON error: not an object\n");
			error_code = JANUS_AUDIOCALL_ERROR_INVALID_JSON;
			g_snprintf(error_cause, 512, "JSON error: not an object");
			goto error;
		}
		JANUS_VALIDATE_JSON_OBJECT(root, request_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOCALL_ERROR_MISSING_ELEMENT, JANUS_AUDIOCALL_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto error;
		const char *msg_sdp_type = json_string_value(json_object_get(msg->jsep, "type"));
		const char *msg_sdp = json_string_value(json_object_get(msg->jsep, "sdp"));
		/* IC: reject any offer/answer that is not audio-only */
		if(msg_sdp != NULL) {
			char sdp_reason[256];
			if(!janus_audiocall_sdp_allowed(msg_sdp, sdp_reason, sizeof(sdp_reason))) {
				JANUS_LOG(LOG_WARN, "[AudioCall] Rejecting SDP from %s: %s\n",
					session->username ? session->username : "(not registered)", sdp_reason);
				error_code = JANUS_AUDIOCALL_ERROR_AUDIO_ONLY;
				g_snprintf(error_cause, 512, "%s", sdp_reason);
				/* Not in a call yet: drop the PeerConnection Janus set up for this attempt */
				if(session->peer == NULL)
					gateway->close_pc(session->handle);
				goto error;
			}
		}
		json_t *msg_e2ee = json_object_get(msg->jsep, "e2ee");
		if(json_is_true(msg_e2ee))
			session->e2ee = TRUE;
		json_t *request = json_object_get(root, "request");
		const char *request_text = json_string_value(request);
		json_t *result = NULL;
		gboolean sdp_update = FALSE;
		if(json_object_get(msg->jsep, "update") != NULL)
			sdp_update = json_is_true(json_object_get(msg->jsep, "update"));
		if(!strcasecmp(request_text, "list")) {
			result = json_object();
			json_t *list = json_array();
			JANUS_LOG(LOG_VERB, "Request for the list of peers\n");
			/* Return a list of all available mountpoints */
			janus_mutex_lock(&sessions_mutex);
			GHashTableIter iter;
			gpointer value;
			g_hash_table_iter_init(&iter, sessions);
			while (g_hash_table_iter_next(&iter, NULL, &value)) {
				janus_audiocall_session *user = value;
				if(user != NULL) {
					janus_refcount_increase(&user->ref);
					if(user->username != NULL)
						json_array_append_new(list, json_string(user->username));
					janus_refcount_decrease(&user->ref);
				}
			}
			json_object_set_new(result, "list", list);
			janus_mutex_unlock(&sessions_mutex);
			json_object_set_new(result, "server", json_string(link_server_name));
			if(link_public_host != NULL)
				json_object_set_new(result, "public_host", json_string(link_public_host));
			json_object_set_new(result, "ws_port", json_integer(link_ws_port));
			/* IC: also the users of all the other servers we know of */
			if(link_enabled) {
				json_t *info = janus_audiocall_peer_info();
				json_object_update(result, info);
				json_decref(info);
			}
		} else if(!strcasecmp(request_text, "locate")) {
			/* IC: tell the user where a username is registered */
			JANUS_VALIDATE_JSON_OBJECT(root, username_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOCALL_ERROR_MISSING_ELEMENT, JANUS_AUDIOCALL_ERROR_INVALID_ELEMENT);
			if(error_code != 0)
				goto error;
			const char *username_text = json_string_value(json_object_get(root, "username"));
			const char *where = "none";
			char *where_server = NULL;
			if(janus_audiocall_is_local_user(username_text)) {
				where = "local";
				where_server = g_strdup(link_server_name);
			} else if(link_enabled && janus_audiocall_peer_find_user(username_text, NULL, &where_server)) {
				where = "peer";
			}
			result = json_object();
			json_object_set_new(result, "event", json_string("located"));
			json_object_set_new(result, "username", json_string(username_text));
			json_object_set_new(result, "where", json_string(where));
			if(where_server != NULL)
				json_object_set_new(result, "server", json_string(where_server));
			g_free(where_server);
		} else if(!strcasecmp(request_text, "register")) {
			/* Map this handle to a username */
			JANUS_VALIDATE_JSON_OBJECT(root, username_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOCALL_ERROR_MISSING_ELEMENT, JANUS_AUDIOCALL_ERROR_INVALID_ELEMENT);
			if(error_code != 0)
				goto error;
			json_t *username = json_object_get(root, "username");
			const char *username_text = json_string_value(username);
			/* IC: a username can only be registered on one server */
			char *taken_on = NULL;
			if(session->username == NULL && link_enabled && janus_audiocall_peer_find_user(username_text, NULL, &taken_on)) {
				JANUS_LOG(LOG_ERR, "Username '%s' already registered on server '%s'\n", username_text, taken_on);
				error_code = JANUS_AUDIOCALL_ERROR_USERNAME_TAKEN;
				g_snprintf(error_cause, 512, "Username '%s' is already registered on server '%s'", username_text, taken_on);
				g_free(taken_on);
				goto error;
			}
			janus_mutex_lock(&sessions_mutex);
			if(session->username != NULL) {
				janus_mutex_unlock(&sessions_mutex);
				JANUS_LOG(LOG_ERR, "Already registered (%s)\n", session->username);
				error_code = JANUS_AUDIOCALL_ERROR_ALREADY_REGISTERED;
				g_snprintf(error_cause, 512, "Already registered (%s)", session->username);
				goto error;
			}
			if(g_hash_table_lookup(usernames, username_text) != NULL) {
				janus_mutex_unlock(&sessions_mutex);
				JANUS_LOG(LOG_ERR, "Username '%s' already taken\n", username_text);
				error_code = JANUS_AUDIOCALL_ERROR_USERNAME_TAKEN;
				g_snprintf(error_cause, 512, "Username '%s' already taken", username_text);
				goto error;
			}
			session->username = g_strdup(username_text);
			janus_refcount_increase(&session->ref);
			g_hash_table_insert(usernames, (gpointer)g_strdup(session->username), session);
			janus_mutex_unlock(&sessions_mutex);
			g_atomic_int_set(&link_dirty, 1);	/* IC: tell the other servers */
			result = json_object();
			json_object_set_new(result, "event", json_string("registered"));
			json_object_set_new(result, "username", json_string(username_text));
			/* Also notify event handlers */
			if(notify_events && gateway->events_is_enabled()) {
				json_t *info = json_object();
				json_object_set_new(info, "event", json_string("registered"));
				json_object_set_new(info, "username", json_string(username_text));
				gateway->notify_event(&janus_audiocall_plugin, session->handle, info);
			}
		} else if(!strcasecmp(request_text, "call")) {
			/* Call another peer */
			if(session->username == NULL) {
				JANUS_LOG(LOG_ERR, "Register a username first\n");
				error_code = JANUS_AUDIOCALL_ERROR_REGISTER_FIRST;
				g_snprintf(error_cause, 512, "Register a username first");
				/* Hangup the call attempt of the user */
				gateway->close_pc(session->handle);
				goto error;
			}
			if(session->peer != NULL) {
				JANUS_LOG(LOG_ERR, "Already in a call\n");
				error_code = JANUS_AUDIOCALL_ERROR_ALREADY_IN_CALL;
				g_snprintf(error_cause, 512, "Already in a call");
				/* Hangup the call attempt of the user */
				gateway->close_pc(session->handle);
				goto error;
			}
			if(!g_atomic_int_compare_and_exchange(&session->incall, 0, 1)) {
				JANUS_LOG(LOG_ERR, "Already in a call (but no peer?)\n");
				error_code = JANUS_AUDIOCALL_ERROR_ALREADY_IN_CALL;
				g_snprintf(error_cause, 512, "Already in a call (but no peer)");
				/* Hangup the call attempt of the user */
				gateway->close_pc(session->handle);
				goto error;
			}
			JANUS_VALIDATE_JSON_OBJECT(root, username_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOCALL_ERROR_MISSING_ELEMENT, JANUS_AUDIOCALL_ERROR_INVALID_ELEMENT);
			if(error_code != 0) {
				/* Hangup the call attempt of the user */
				g_atomic_int_set(&session->incall, 0);
				gateway->close_pc(session->handle);
				goto error;
			}
			json_t *username = json_object_get(root, "username");
			const char *username_text = json_string_value(username);
			if(!strcmp(username_text, session->username)) {
				g_atomic_int_set(&session->incall, 0);
				JANUS_LOG(LOG_ERR, "You can't call yourself... use the EchoTest for that\n");
				error_code = JANUS_AUDIOCALL_ERROR_USE_ECHO_TEST;
				g_snprintf(error_cause, 512, "You can't call yourself... use the EchoTest for that");
				/* Hangup the call attempt of the user */
				gateway->close_pc(session->handle);
				goto error;
			}
			janus_mutex_lock(&sessions_mutex);
			janus_audiocall_session *peer = g_hash_table_lookup(usernames, username_text);
			if(peer == NULL || g_atomic_int_get(&peer->destroyed)) {
				g_atomic_int_set(&session->incall, 0);
				janus_mutex_unlock(&sessions_mutex);
				/* IC: not registered here, is this user on another server? */
				struct sockaddr_in remote_addr;
				char *remote_server = NULL;
				if(link_enabled && janus_audiocall_peer_find_user(username_text, &remote_addr, &remote_server)) {
					/* IC: start a cross-server call, the offer travels over the peer link */
					if(msg_sdp == NULL || msg_sdp_type == NULL) {
						g_free(remote_server);
						JANUS_LOG(LOG_ERR, "Missing SDP\n");
						error_code = JANUS_AUDIOCALL_ERROR_MISSING_SDP;
						g_snprintf(error_cause, 512, "Missing SDP");
						gateway->close_pc(session->handle);
						goto error;
					}
					guint32 call_id = 0;
					while(call_id == 0) {
						call_id = g_random_int();
						janus_audiocall_session *existing = janus_audiocall_remote_get(call_id);
						if(existing != NULL) {
							janus_refcount_decrease(&existing->ref);
							call_id = 0;
						}
					}
					g_atomic_int_set(&session->incall, 1);
					janus_audiocall_remote_add(session, call_id, username_text, &remote_addr, remote_server);
					session->has_audio = TRUE;
					session->has_video = FALSE;
					session->has_data = FALSE;
					g_atomic_int_set(&session->hangingup, 0);
					json_t *call = json_object();
					json_object_set_new(call, "type", json_string("call"));
					json_object_set_new(call, "call_id", json_integer(call_id));
					json_object_set_new(call, "from", json_string(session->username));
					json_object_set_new(call, "to", json_string(username_text));
					json_object_set_new(call, "sdp_type", json_string(msg_sdp_type));
					json_object_set_new(call, "sdp", json_string(msg_sdp));
					json_object_set_new(call, "server", json_string(link_server_name));
					janus_audiocall_link_send_to(&remote_addr, call);
					json_decref(call);
					JANUS_LOG(LOG_INFO, "[AudioCall] Cross-server call %u: %s -> %s (on server '%s')\n",
						call_id, session->username, username_text, remote_server);
					g_free(remote_server);
					result = json_object();
					json_object_set_new(result, "event", json_string("calling"));
					json_object_set_new(result, "remote", json_true());
					goto request_done;
				}
				JANUS_LOG(LOG_ERR, "Username '%s' doesn't exist\n", username_text);
				error_code = JANUS_AUDIOCALL_ERROR_NO_SUCH_USERNAME;
				g_snprintf(error_cause, 512, "Username '%s' doesn't exist", username_text);
				/* Hangup the call attempt of the user */
				gateway->close_pc(session->handle);
				goto error;
			}
			/* If the call attempt proceeds we keep the references */
			janus_refcount_increase(&session->ref);
			janus_refcount_increase(&peer->ref);
			if(g_atomic_int_get(&peer->incall) || peer->peer != NULL) {
				if(g_atomic_int_compare_and_exchange(&session->incall, 1, 0) && peer) {
					janus_refcount_decrease(&session->ref);
					janus_refcount_decrease(&peer->ref);
				}
				janus_mutex_unlock(&sessions_mutex);
				JANUS_LOG(LOG_VERB, "%s is busy\n", username_text);
				result = json_object();
				json_object_set_new(result, "event", json_string("hangup"));
				json_object_set_new(result, "username", json_string(session->username));
				json_object_set_new(result, "reason", json_string("User busy"));
				/* Also notify event handlers */
				if(notify_events && gateway->events_is_enabled()) {
					json_t *info = json_object();
					json_object_set_new(info, "event", json_string("hangup"));
					json_object_set_new(info, "reason", json_string("User busy"));
					gateway->notify_event(&janus_audiocall_plugin, session->handle, info);
				}
				/* Hangup the call attempt of the user */
				gateway->close_pc(session->handle);
			} else {
				/* Any SDP to handle? if not, something's wrong */
				if(!msg_sdp) {
					if(g_atomic_int_compare_and_exchange(&session->incall, 1, 0) && peer) {
						janus_refcount_decrease(&session->ref);
						janus_refcount_decrease(&peer->ref);
					}
					janus_mutex_unlock(&sessions_mutex);
					JANUS_LOG(LOG_ERR, "Missing SDP\n");
					error_code = JANUS_AUDIOCALL_ERROR_MISSING_SDP;
					g_snprintf(error_cause, 512, "Missing SDP");
					goto error;
				}
				char error_str[512];
				janus_sdp *offer = janus_sdp_parse(msg_sdp, error_str, sizeof(error_str));
				if(offer == NULL) {
					if(g_atomic_int_compare_and_exchange(&session->incall, 1, 0) && peer) {
						janus_refcount_decrease(&session->ref);
						janus_refcount_decrease(&peer->ref);
					}
					janus_mutex_unlock(&sessions_mutex);
					JANUS_LOG(LOG_ERR, "Error parsing offer: %s\n", error_str);
					error_code = JANUS_AUDIOCALL_ERROR_INVALID_SDP;
					g_snprintf(error_cause, 512, "Error parsing offer: %s", error_str);
					goto error;
				}
				janus_sdp_destroy(offer);
				g_atomic_int_set(&peer->incall, 1);
				janus_refcount_increase(&session->ref);
				janus_refcount_increase(&peer->ref);
				janus_mutex_lock(&session->mutex);
				session->peer = peer;
				janus_mutex_unlock(&session->mutex);
				janus_mutex_lock(&peer->mutex);
				peer->peer = session;
				janus_mutex_unlock(&peer->mutex);
				session->has_audio = (strstr(msg_sdp, "m=audio") != NULL);
				session->has_video = (strstr(msg_sdp, "m=video") != NULL);
				session->has_data = (strstr(msg_sdp, "DTLS/SCTP") != NULL);
				janus_mutex_unlock(&sessions_mutex);
				JANUS_LOG(LOG_VERB, "%s is calling %s\n", session->username, peer->username);
				JANUS_LOG(LOG_VERB, "This is involving a negotiation (%s) as well:\n%s\n", msg_sdp_type, msg_sdp);
				/* Check if this user will simulcast */
				json_t *msg_simulcast = json_object_get(msg->jsep, "simulcast");
				if(msg_simulcast && json_array_size(msg_simulcast) > 0) {
					size_t i = 0;
					for(i=0; i<json_array_size(msg_simulcast); i++) {
						json_t *s = json_array_get(msg_simulcast, i);
						int mindex = json_integer_value(json_object_get(s, "mindex"));
						JANUS_LOG(LOG_VERB, "AudioCall caller (%s) is going to do simulcasting (#%d)\n", session->username, mindex);
						int rid_ext_id = -1;
						janus_mutex_lock(&session->rid_mutex);
						/* Clear existing RIDs in case this is a renegotiation */
						janus_rtp_simulcasting_cleanup(NULL, NULL, session->rid, NULL);
						janus_rtp_simulcasting_prepare(s, &rid_ext_id, session->ssrc, session->rid);
						session->sim_context.rid_ext_id = rid_ext_id;
						janus_mutex_unlock(&session->rid_mutex);
						session->sim_context.substream_target = 2;	/* Let's aim for the highest quality */
						session->sim_context.templayer_target = 2;	/* Let's aim for all temporal layers */
						/* FIXME We're stopping at the first item, there may be more */
						break;
					}
				}
				/* Send SDP to our peer */
				json_t *call = json_object();
				json_object_set_new(call, "audiocall", json_string("event"));
				json_t *calling = json_object();
				json_object_set_new(calling, "event", json_string("incomingcall"));
				json_object_set_new(calling, "username", json_string(session->username));
				json_object_set_new(call, "result", calling);
				json_t *jsep = json_pack("{ssss}", "type", msg_sdp_type, "sdp", msg_sdp);
				if(session->e2ee)
					json_object_set_new(jsep, "e2ee", json_true());
				g_atomic_int_set(&session->hangingup, 0);
				int ret = gateway->push_event(peer->handle, &janus_audiocall_plugin, NULL, call, jsep);
				JANUS_LOG(LOG_VERB, "  >> Pushing event to peer: %d (%s)\n", ret, janus_get_api_error(ret));
				json_decref(call);
				json_decref(jsep);
				/* Send an ack back */
				result = json_object();
				json_object_set_new(result, "event", json_string("calling"));
				/* Also notify event handlers */
				if(notify_events && gateway->events_is_enabled()) {
					json_t *info = json_object();
					json_object_set_new(info, "event", json_string("calling"));
					gateway->notify_event(&janus_audiocall_plugin, session->handle, info);
				}
			}
		} else if(!strcasecmp(request_text, "accept")) {
			/* IC: accepting a call that came from the peer server? */
			if(session->remote_call) {
				if(msg_sdp == NULL || msg_sdp_type == NULL) {
					JANUS_LOG(LOG_ERR, "Missing SDP\n");
					error_code = JANUS_AUDIOCALL_ERROR_MISSING_SDP;
					g_snprintf(error_cause, 512, "Missing SDP");
					goto error;
				}
				janus_mutex_lock(&session->mutex);
				guint32 call_id = session->call_id;
				char *remote_user = g_strdup(session->remote_username ? session->remote_username : "");
				struct sockaddr_in remote_addr = session->remote_addr;
				janus_mutex_unlock(&session->mutex);
				json_t *accepted = json_object();
				json_object_set_new(accepted, "type", json_string("accepted"));
				json_object_set_new(accepted, "call_id", json_integer(call_id));
				json_object_set_new(accepted, "sdp_type", json_string(msg_sdp_type));
				json_object_set_new(accepted, "sdp", json_string(msg_sdp));
				json_object_set_new(accepted, "server", json_string(link_server_name));
				janus_audiocall_link_send_to(&remote_addr, accepted);
				json_decref(accepted);
				JANUS_LOG(LOG_INFO, "[AudioCall] Cross-server call %u: %s accepted the call from %s\n",
					call_id, session->username, remote_user);
				result = json_object();
				json_object_set_new(result, "event", json_string("accepted"));
				json_object_set_new(result, "username", json_string(remote_user));
				g_free(remote_user);
				goto request_done;
			}
			/* Accept a call from another peer */
			janus_audiocall_session *peer = session->peer;
			if(peer == NULL || !g_atomic_int_get(&session->incall) || !g_atomic_int_get(&peer->incall)) {
				JANUS_LOG(LOG_ERR, "No incoming call to accept\n");
				error_code = JANUS_AUDIOCALL_ERROR_NO_CALL;
				g_snprintf(error_cause, 512, "No incoming call to accept");
				goto error;
			}
			janus_refcount_increase(&peer->ref);
			/* Any SDP to handle? if not, something's wrong */
			if(!msg_sdp) {
				janus_refcount_decrease(&peer->ref);
				JANUS_LOG(LOG_ERR, "Missing SDP\n");
				error_code = JANUS_AUDIOCALL_ERROR_MISSING_SDP;
				g_snprintf(error_cause, 512, "Missing SDP");
				goto error;
			}
			char error_str[512];
			janus_sdp *answer = janus_sdp_parse(msg_sdp, error_str, sizeof(error_str));
			if(answer == NULL) {
				janus_refcount_decrease(&peer->ref);
				JANUS_LOG(LOG_ERR, "Error parsing answer: %s\n", error_str);
				error_code = JANUS_AUDIOCALL_ERROR_INVALID_SDP;
				g_snprintf(error_cause, 512, "Error parsing answer: %s", error_str);
				goto error;
			}
			JANUS_LOG(LOG_VERB, "%s is accepting a call from %s\n", session->username, peer->username);
			JANUS_LOG(LOG_VERB, "This is involving a negotiation (%s) as well:\n%s\n", msg_sdp_type, msg_sdp);
			session->has_audio = (strstr(msg_sdp, "m=audio") != NULL);
			session->has_video = (strstr(msg_sdp, "m=video") != NULL);
			session->has_data = (strstr(msg_sdp, "DTLS/SCTP") != NULL);
			/* Check if this user will simulcast */
			json_t *msg_simulcast = json_object_get(msg->jsep, "simulcast");
			if(msg_simulcast) {
				JANUS_LOG(LOG_VERB, "AudioCall callee (%s) cannot do simulcast.\n", session->username);
			} else {
				janus_rtp_simulcasting_cleanup(NULL, session->ssrc, session->rid, &session->rid_mutex);
			}
			/* Check which codecs we ended up using */
			const char *acodec = NULL, *vcodec = NULL;
			janus_sdp_find_first_codec(answer, JANUS_SDP_AUDIO, -1, &acodec);
			session->acodec = janus_audiocodec_from_name(acodec);
			janus_sdp_find_first_codec(answer, JANUS_SDP_VIDEO, -1, &vcodec);
			session->vcodec = janus_videocodec_from_name(vcodec);
			session->opusred_pt = janus_sdp_get_opusred_pt(answer, -1);
			if(peer) {
				peer->has_audio = session->has_audio;
				peer->has_video = session->has_video;
				peer->has_data = session->has_data;
				peer->acodec = session->acodec;
				peer->vcodec = session->vcodec;
				peer->opusred_pt = session->opusred_pt;
			}
			if(session->acodec == JANUS_AUDIOCODEC_NONE) {
				session->has_audio = FALSE;
				if(peer)
					peer->has_audio = FALSE;
			} else if(peer) {
				peer->acodec = session->acodec;
			}
			if(session->vcodec == JANUS_VIDEOCODEC_NONE) {
				session->has_video = FALSE;
				if(peer)
					peer->has_video = FALSE;
			} else if(peer) {
				peer->vcodec = session->vcodec;
			}
			janus_sdp_destroy(answer);
			/* Send SDP to our peer */
			json_t *jsep = json_pack("{ssss}", "type", msg_sdp_type, "sdp", msg_sdp);
			if(session->e2ee)
				json_object_set_new(jsep, "e2ee", json_true());
			json_t *call = json_object();
			json_object_set_new(call, "audiocall", json_string("event"));
			json_t *calling = json_object();
			json_object_set_new(calling, "event", json_string("accepted"));
			json_object_set_new(calling, "username", json_string(session->username));
			json_object_set_new(call, "result", calling);
			g_atomic_int_set(&session->hangingup, 0);
			int ret = gateway->push_event(peer->handle, &janus_audiocall_plugin, NULL, call, jsep);
			JANUS_LOG(LOG_VERB, "  >> Pushing event to peer: %d (%s)\n", ret, janus_get_api_error(ret));
			json_decref(call);
			json_decref(jsep);
			/* Send an ack back */
			result = json_object();
			json_object_set_new(result, "event", json_string("accepted"));
			/* Also notify event handlers */
			if(notify_events && gateway->events_is_enabled()) {
				json_t *info = json_object();
				json_object_set_new(info, "event", json_string("accepted"));
				gateway->notify_event(&janus_audiocall_plugin, session->handle, info);
			}
			/* Is simulcasting involved on either side? */
			if(session->ssrc[0] || session->rid[0]) {
				peer->sim_context.substream_target = 2;	/* Let's aim for the highest quality */
				peer->sim_context.templayer_target = 2;	/* Let's aim for all temporal layers */
			}
			if(peer->ssrc[0] || peer->rid[0]) {
				session->sim_context.substream_target = 2;	/* Let's aim for the highest quality */
				session->sim_context.templayer_target = 2;	/* Let's aim for all temporal layers */
			}
			/* We don't need this reference anymore, it was already increased by the peer calling us */
			janus_refcount_decrease(&peer->ref);
		} else if(!strcasecmp(request_text, "set")) {
			/* Update the local configuration (audio/video mute/unmute, bitrate cap or recording) */
			JANUS_VALIDATE_JSON_OBJECT(root, set_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOCALL_ERROR_MISSING_ELEMENT, JANUS_AUDIOCALL_ERROR_INVALID_ELEMENT);
			if(error_code != 0)
				goto error;
			json_t *audio = json_object_get(root, "audio");
			json_t *video = json_object_get(root, "video");
			json_t *bitrate = json_object_get(root, "bitrate");
			json_t *record = json_object_get(root, "record");
			json_t *recfile = json_object_get(root, "filename");
			json_t *restart = json_object_get(root, "restart");
			json_t *substream = json_object_get(root, "substream");
			if(substream && (!json_is_integer(substream) || json_integer_value(substream) < 0 || json_integer_value(substream) > 2)) {
				JANUS_LOG(LOG_ERR, "Invalid element (substream should be 0, 1 or 2)\n");
				error_code = JANUS_AUDIOCALL_ERROR_INVALID_ELEMENT;
				g_snprintf(error_cause, 512, "Invalid value (substream should be 0, 1 or 2)");
				goto error;
			}
			json_t *temporal = json_object_get(root, "temporal");
			if(temporal && (!json_is_integer(temporal) || json_integer_value(temporal) < 0 || json_integer_value(temporal) > 2)) {
				JANUS_LOG(LOG_ERR, "Invalid element (temporal should be 0, 1 or 2)\n");
				error_code = JANUS_AUDIOCALL_ERROR_INVALID_ELEMENT;
				g_snprintf(error_cause, 512, "Invalid value (temporal should be 0, 1 or 2)");
				goto error;
			}
			json_t *fallback = json_object_get(root, "fallback");
			if(fallback && (!json_is_integer(fallback) || json_integer_value(fallback) < 0)) {
				JANUS_LOG(LOG_ERR, "Invalid element (fallback should be a positive integer)\n");
				error_code = JANUS_AUDIOCALL_ERROR_INVALID_ELEMENT;
				g_snprintf(error_cause, 512, "Invalid value (fallback should be a positive integer)");
				goto error;
			}
			if(audio) {
				session->audio_active = json_is_true(audio);
				JANUS_LOG(LOG_VERB, "Setting audio property: %s\n", session->audio_active ? "true" : "false");
			}
			if(video) {
				if(!session->video_active && json_is_true(video)) {
					/* Send a PLI */
					JANUS_LOG(LOG_VERB, "Just (re-)enabled video, sending a PLI to recover it\n");
					gateway->send_pli(session->handle);
				}
				session->video_active = json_is_true(video);
				JANUS_LOG(LOG_VERB, "Setting video property: %s\n", session->video_active ? "true" : "false");
			}
			if(bitrate) {
				session->bitrate = json_integer_value(bitrate);
				JANUS_LOG(LOG_VERB, "Setting video bitrate: %"SCNu32"\n", session->bitrate);
				gateway->send_remb(session->handle, session->bitrate ? session->bitrate : 10000000);
			}
			janus_audiocall_session *peer = session->peer;
			if(peer)
				janus_refcount_increase(&peer->ref);
			if(fallback) {
				JANUS_LOG(LOG_VERB, "Setting fallback timer (simulcast): %lld (was %"SCNu32")\n",
					json_integer_value(fallback) ? json_integer_value(fallback) : 250000,
					session->sim_context.drop_trigger ? session->sim_context.drop_trigger : 250000);
				session->sim_context.drop_trigger = json_integer_value(fallback);
			}
			if(substream) {
				session->sim_context.substream_target = json_integer_value(substream);
				if(session->sim_context.substream_target >= 0 && session->sim_context.substream_target <= 2) {
					JANUS_LOG(LOG_VERB, "Setting video SSRC to let through (simulcast): %"SCNu32" (index %d, was %d)\n",
						session->ssrc[session->sim_context.substream_target], session->sim_context.substream_target, session->sim_context.substream);
				}
				if(session->sim_context.substream_target == session->sim_context.substream) {
					/* No need to do anything, we're already getting the right substream, so notify the user */
					json_t *event = json_object();
					json_object_set_new(event, "audiocall", json_string("event"));
					json_t *result = json_object();
					json_object_set_new(result, "event", json_string("simulcast"));
					json_object_set_new(result, "videocodec", json_string(janus_videocodec_name(session->vcodec)));
					json_object_set_new(result, "substream", json_integer(session->sim_context.substream));
					json_object_set_new(event, "result", result);
					gateway->push_event(session->handle, &janus_audiocall_plugin, NULL, event, NULL);
					json_decref(event);
				} else {
					/* We need to change substream, send the peer a PLI */
					JANUS_LOG(LOG_VERB, "Simulcasting substream change, sending a PLI to kickstart it\n");
					if(peer && peer->handle)
						gateway->send_pli(peer->handle);
				}
			}
			if(temporal) {
				session->sim_context.templayer_target = json_integer_value(temporal);
				JANUS_LOG(LOG_VERB, "Setting video temporal layer to let through (simulcast): %d (was %d)\n",
					session->sim_context.templayer_target, session->sim_context.templayer);
				if(session->sim_context.templayer_target == session->sim_context.templayer) {
					/* No need to do anything, we're already getting the right temporal, so notify the user */
					json_t *event = json_object();
					json_object_set_new(event, "audiocall", json_string("event"));
					json_t *result = json_object();
					json_object_set_new(result, "event", json_string("simulcast"));
					json_object_set_new(result, "videocodec", json_string(janus_videocodec_name(session->vcodec)));
					json_object_set_new(result, "temporal", json_integer(session->sim_context.templayer));
					json_object_set_new(event, "result", result);
					gateway->push_event(session->handle, &janus_audiocall_plugin, NULL, event, NULL);
					json_decref(event);
				} else {
					/* We need to change temporal, send a PLI */
					JANUS_LOG(LOG_VERB, "Simulcasting temporal layer change, sending a PLI to kickstart it\n");
					if(peer && peer->handle)
						gateway->send_pli(peer->handle);
				}
			}
			if(msg_sdp && msg_sdp_type && !strcasecmp(msg_sdp_type, "answer")) {
				/* Process the answer to see if there were any changes */
				char error_str[512];
				janus_sdp *answer = janus_sdp_parse(msg_sdp, error_str, sizeof(error_str));
				if(answer == NULL) {
					if(peer)
						janus_refcount_decrease(&peer->ref);
					JANUS_LOG(LOG_ERR, "Error parsing answer: %s\n", error_str);
					error_code = JANUS_AUDIOCALL_ERROR_INVALID_SDP;
					g_snprintf(error_cause, 512, "Error parsing answer: %s", error_str);
					goto error;
				}
				JANUS_LOG(LOG_VERB, "%s is accepting an update from %s\n", session->username, peer ? peer->username : "??");
				session->has_audio = (strstr(msg_sdp, "m=audio") != NULL);
				session->has_video = (strstr(msg_sdp, "m=video") != NULL);
				session->has_data = (strstr(msg_sdp, "DTLS/SCTP") != NULL);
				/* Check if this user will simulcast */
				json_t *msg_simulcast = json_object_get(msg->jsep, "simulcast");
				if(msg_simulcast) {
					JANUS_LOG(LOG_VERB, "AudioCall callee (%s) cannot do simulcast.\n", session->username);
				} else {
					janus_rtp_simulcasting_cleanup(NULL, session->ssrc, session->rid, &session->rid_mutex);
				}
				/* Check which codecs we ended up using */
				const char *acodec = NULL, *vcodec = NULL;
				janus_sdp_find_first_codec(answer, JANUS_SDP_AUDIO, -1, &acodec);
				session->acodec = janus_audiocodec_from_name(acodec);
				janus_sdp_find_first_codec(answer, JANUS_SDP_VIDEO, -1, &vcodec);
				session->vcodec = janus_videocodec_from_name(vcodec);
				session->opusred_pt = janus_sdp_get_opusred_pt(answer, -1);
				janus_audiocall_session *peer = session->peer;
				if(peer) {
					peer->has_audio = session->has_audio;
					peer->has_video = session->has_video;
					peer->has_data = session->has_data;
					peer->acodec = session->acodec;
					peer->vcodec = session->vcodec;
					peer->opusred_pt = session->opusred_pt;
				}
				if(session->acodec == JANUS_AUDIOCODEC_NONE) {
					session->has_audio = FALSE;
					if(peer)
						peer->has_audio = FALSE;
				} else if(peer) {
					peer->acodec = session->acodec;
				}
				if(session->vcodec == JANUS_VIDEOCODEC_NONE) {
					session->has_video = FALSE;
					if(peer)
						peer->has_video = FALSE;
				} else if(peer) {
					peer->vcodec = session->vcodec;
				}
				janus_sdp_destroy(answer);
			}
			if(record) {
				gboolean recording = json_is_true(record);
				const char *recording_base = json_string_value(recfile);
				JANUS_LOG(LOG_VERB, "Recording %s (base filename: %s)\n", recording ? "enabled" : "disabled", recording_base ? recording_base : "not provided");
				janus_mutex_lock(&session->rec_mutex);
				if(!recording) {
					/* Not recording (anymore?) */
					janus_audiocall_recorder_close(session);
				} else {
					/* We've started recording, send a PLI and go on */
					char filename[255];
					gint64 now = janus_get_real_time();
					if(session->has_audio) {
						/* Prepare an audio recording */
						janus_recorder *rc = NULL;
						memset(filename, 0, 255);
						if(recording_base) {
							/* Use the filename and path we have been provided */
							g_snprintf(filename, 255, "%s-audio", recording_base);
							rc = janus_recorder_create(NULL, janus_audiocodec_name(session->acodec), filename);
							if(rc == NULL) {
								/* FIXME We should notify the fact the recorder could not be created */
								JANUS_LOG(LOG_ERR, "Couldn't open an audio recording file for this AudioCall user!\n");
							}
						} else {
							/* Build a filename */
							g_snprintf(filename, 255, "audiocall-%s-%s-%"SCNi64"-audio",
								session->username ? session->username : "unknown",
								(peer && peer->username) ? peer->username : "unknown",
								now);
							rc = janus_recorder_create(NULL, janus_audiocodec_name(session->acodec), filename);
							if(rc == NULL) {
								/* FIXME We should notify the fact the recorder could not be created */
								JANUS_LOG(LOG_ERR, "Couldn't open an audio recording file for this AudioCall user!\n");
							}
						}
						/* If RED is in use, take note of it */
						if(session->opusred_pt > 0)
							janus_recorder_opusred(rc, session->opusred_pt);
						/* If media is encrypted, mark it in the recording */
						if(session->e2ee)
							janus_recorder_encrypted(rc);
						session->arc = rc;
					}
					if(session->has_video) {
						/* Prepare a video recording */
						janus_recorder *rc = NULL;
						memset(filename, 0, 255);
						if(recording_base) {
							/* Use the filename and path we have been provided */
							g_snprintf(filename, 255, "%s-video", recording_base);
							rc = janus_recorder_create(NULL, janus_videocodec_name(session->vcodec), filename);
							if(rc == NULL) {
								/* FIXME We should notify the fact the recorder could not be created */
								JANUS_LOG(LOG_ERR, "Couldn't open an video recording file for this AudioCall user!\n");
							}
						} else {
							/* Build a filename */
							g_snprintf(filename, 255, "audiocall-%s-%s-%"SCNi64"-video",
								session->username ? session->username : "unknown",
								(peer && peer->username) ? peer->username : "unknown",
								now);
							rc = janus_recorder_create(NULL, janus_videocodec_name(session->vcodec), filename);
							if(rc == NULL) {
								/* FIXME We should notify the fact the recorder could not be created */
								JANUS_LOG(LOG_ERR, "Couldn't open an video recording file for this AudioCall user!\n");
							}
						}
						/* Send a PLI */
						JANUS_LOG(LOG_VERB, "Recording video, sending a PLI to kickstart it\n");
						gateway->send_pli(session->handle);
						/* If media is encrypted, mark it in the recording */
						if(session->e2ee)
							janus_recorder_encrypted(rc);
						session->vrc = rc;
					}
					if(session->has_data) {
						/* Prepare a data recording */
						janus_recorder *rc = NULL;
						memset(filename, 0, 255);
						if(recording_base) {
							/* Use the filename and path we have been provided */
							g_snprintf(filename, 255, "%s-data", recording_base);
							rc = janus_recorder_create(NULL, "text", filename);
							if(rc == NULL) {
								/* FIXME We should notify the fact the recorder could not be created */
								JANUS_LOG(LOG_ERR, "Couldn't open a data recording file for this AudioCall user!\n");
							}
						} else {
							/* Build a filename */
							g_snprintf(filename, 255, "audiocall-%s-%s-%"SCNi64"-data",
								session->username ? session->username : "unknown",
								(peer && peer->username) ? peer->username : "unknown",
								now);
							rc = janus_recorder_create(NULL, "text", filename);
							if(rc == NULL) {
								/* FIXME We should notify the fact the recorder could not be created */
								JANUS_LOG(LOG_ERR, "Couldn't open a data recording file for this AudioCall user!\n");
							}
						}
						/* Media encryption doesn't apply to data channels */
						session->drc = rc;
					}
				}
				janus_mutex_unlock(&session->rec_mutex);
			}
			/* Also notify event handlers */
			if(notify_events && gateway->events_is_enabled()) {
				json_t *info = json_object();
				json_object_set_new(info, "event", json_string("configured"));
				json_object_set_new(info, "audio_active", session->audio_active ? json_true() : json_false());
				json_object_set_new(info, "video_active", session->video_active ? json_true() : json_false());
				json_object_set_new(info, "bitrate", json_integer(session->bitrate));
				if(session->arc || session->vrc || session->drc) {
					json_t *recording = json_object();
					if(session->arc && session->arc->filename)
						json_object_set_new(recording, "audio", json_string(session->arc->filename));
					if(session->vrc && session->vrc->filename)
						json_object_set_new(recording, "video", json_string(session->vrc->filename));
					if(session->drc && session->drc->filename)
						json_object_set_new(recording, "data", json_string(session->drc->filename));
					json_object_set_new(info, "recording", recording);
				}
				gateway->notify_event(&janus_audiocall_plugin, session->handle, info);
			}
			/* Send an ack back */
			result = json_object();
			json_object_set_new(result, "event", json_string("set"));
			/* If this is for an ICE restart, prepare the SDP to send back too */
			gboolean do_restart = restart ? json_is_true(restart) : FALSE;
			if(do_restart && !sdp_update) {
				JANUS_LOG(LOG_WARN, "Got a 'restart' request, but no SDP update? Ignoring...\n");
			}
			if(sdp_update && peer != NULL) {
				/* Forward new SDP to the peer */
				json_t *event = json_object();
				json_object_set_new(event, "audiocall", json_string("event"));
				json_t *update = json_object();
				json_object_set_new(update, "event", json_string("update"));
				json_object_set_new(event, "result", update);
				json_t *jsep = json_pack("{ssss}", "type", msg_sdp_type, "sdp", msg_sdp);
				int ret = gateway->push_event(peer->handle, &janus_audiocall_plugin, NULL, event, jsep);
				JANUS_LOG(LOG_VERB, "  >> Pushing event: %d (%s)\n", ret, janus_get_api_error(ret));
				json_decref(event);
				json_decref(jsep);
			}
			if(peer)
				janus_refcount_decrease(&peer->ref);
		} else if(!strcasecmp(request_text, "hangup")) {
			json_t *hangup = json_object_get(root, "reason");
			if(hangup && !json_is_string(hangup)) {
				JANUS_LOG(LOG_WARN, "Invalid element (hangup should be a string), ignoring\n");
				hangup = NULL;
			}
			const char *hangup_text = hangup ? json_string_value(hangup) : "We did the hangup";
			/* IC: ending (or refusing) a cross-server call */
			if(session->remote_call) {
				guint32 call_id = 0;
				char *remote_user = NULL;
				struct sockaddr_in remote_addr;
				if(janus_audiocall_remote_detach(session, &call_id, &remote_user, &remote_addr)) {
					JANUS_LOG(LOG_INFO, "[AudioCall] Cross-server call %u: %s hung up (%s)\n", call_id, session->username, hangup_text);
					janus_audiocall_link_send_hangup(&remote_addr, call_id, hangup_text);
				}
				result = json_object();
				json_object_set_new(result, "event", json_string("hangup"));
				json_object_set_new(result, "username", json_string(session->username));
				json_object_set_new(result, "reason", json_string("Explicit hangup"));
				gateway->close_pc(session->handle);
				g_free(remote_user);
				goto request_done;
			}
			/* Hangup an ongoing call or reject an incoming one */
			janus_audiocall_session *peer = session->peer;
			if(peer == NULL) {
				JANUS_LOG(LOG_WARN, "No call to hangup\n");
			} else {
				JANUS_LOG(LOG_VERB, "%s is hanging up the call with %s (%s)\n", session->username, peer->username, hangup_text);
			}
			/* Check if we still need to remove any reference */
			if(peer && g_atomic_int_compare_and_exchange(&peer->incall, 1, 0)) {
				janus_refcount_decrease(&session->ref);
			}
			if(g_atomic_int_compare_and_exchange(&session->incall, 1, 0) && peer) {
				janus_refcount_decrease(&peer->ref);
			}
			/* Notify the success as an hangup message */
			result = json_object();
			json_object_set_new(result, "event", json_string("hangup"));
			json_object_set_new(result, "username", json_string(session->username));
			json_object_set_new(result, "reason", json_string(hangup_text));
			json_object_set_new(result, "reason", json_string("Explicit hangup"));
			/* Also notify event handlers */
			if(notify_events && gateway->events_is_enabled()) {
				json_t *info = json_object();
				json_object_set_new(info, "event", json_string("hangup"));
				json_object_set_new(info, "reason", json_string("Explicit hangup"));
				gateway->notify_event(&janus_audiocall_plugin, session->handle, info);
			}
			/* Hangup the call on the user, if it's still up */
			gateway->close_pc(session->handle);
			if(peer != NULL) {
				/* Send event to our peer too */
				json_t *call = json_object();
				json_object_set_new(call, "audiocall", json_string("event"));
				json_t *calling = json_object();
				json_object_set_new(calling, "event", json_string("hangup"));
				json_object_set_new(calling, "username", json_string(session->username));
				json_object_set_new(calling, "reason", json_string(hangup_text));
				json_object_set_new(call, "result", calling);
				gateway->close_pc(peer->handle);
				int ret = gateway->push_event(peer->handle, &janus_audiocall_plugin, NULL, call, NULL);
				JANUS_LOG(LOG_VERB, "  >> Pushing event to peer: %d (%s)\n", ret, janus_get_api_error(ret));
				json_decref(call);
				/* Also notify event handlers */
				if(notify_events && gateway->events_is_enabled()) {
					json_t *info = json_object();
					json_object_set_new(info, "event", json_string("hangup"));
					json_object_set_new(info, "reason", json_string("Remote hangup"));
					gateway->notify_event(&janus_audiocall_plugin, peer->handle, info);
				}
				/* Hangup the call on the peer, if it's still up */
				gateway->close_pc(peer->handle);
			}
		} else {
			JANUS_LOG(LOG_ERR, "Unknown request (%s)\n", request_text);
			error_code = JANUS_AUDIOCALL_ERROR_INVALID_REQUEST;
			g_snprintf(error_cause, 512, "Unknown request (%s)", request_text);
			goto error;
		}

request_done:
		{
		/* Prepare JSON event */
		json_t *event = json_object();
		json_object_set_new(event, "audiocall", json_string("event"));
		if(result != NULL)
			json_object_set_new(event, "result", result);
		int ret = gateway->push_event(msg->handle, &janus_audiocall_plugin, msg->transaction, event, NULL);
		JANUS_LOG(LOG_VERB, "  >> Pushing event: %d (%s)\n", ret, janus_get_api_error(ret));
		json_decref(event);
		janus_audiocall_message_free(msg);
		continue;
		}

error:
		{
			/* Prepare JSON error event */
			json_t *event = json_object();
			json_object_set_new(event, "audiocall", json_string("event"));
			json_object_set_new(event, "error_code", json_integer(error_code));
			json_object_set_new(event, "error", json_string(error_cause));
			int ret = gateway->push_event(msg->handle, &janus_audiocall_plugin, msg->transaction, event, NULL);
			JANUS_LOG(LOG_VERB, "  >> Pushing event: %d (%s)\n", ret, janus_get_api_error(ret));
			json_decref(event);
			janus_audiocall_message_free(msg);
		}
	}
	JANUS_LOG(LOG_VERB, "Leaving AudioCall handler thread\n");
	return NULL;
}
