// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "webrtc_client.hpp"
#include "../logging.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include <thread>
#define GST_USE_UNSTABLE_API
#include <gst/sdp/gstsdpmessage.h>
#include <gst/webrtc/webrtc.h>

namespace ros_camera_server_qml_common
{

WebRTCClient::WebRTCClient( const QString &url, GstElement *pipeline )
    : pipeline_( pipeline ), url_( url )
{
  gst_object_ref( pipeline_ ); // Keep reference

  // Connect via lambdas; socket_ is explicitly disconnected in the destructor before teardown.
  QObject::connect( &socket_, &QWebSocket::connected, [this]() { onConnected(); } );
  QObject::connect( &socket_, &QWebSocket::textMessageReceived,
                    [this]( const QString &msg ) { onTextMessageReceived( msg ); } );
  QObject::connect( &socket_, &QWebSocket::disconnected, [this]() { onDisconnected(); } );

  // Reconnection timer. The interval is set per attempt by scheduleReconnect() (base + jitter).
  reconnect_timer_.setSingleShot( true );
  QObject::connect( &reconnect_timer_, &QTimer::timeout, [this]() {
    HCS_LOG_INFO( "Attempting to reconnect to WebRTC signaling server..." );
    socket_.open( QUrl( url_ ) );
  } );

  // Error handling
  // Qt 6.5+ has errorOccurred signal, Qt5 has error signal,
  // Qt 6.0-6.4 has no error signal (error() is just a getter method).
#if QT_VERSION >= QT_VERSION_CHECK( 6, 5, 0 )
  QObject::connect( &socket_,
                    QOverload<QAbstractSocket::SocketError>::of( &QWebSocket::errorOccurred ),
                    [this]( QAbstractSocket::SocketError ) {
                      HCS_LOG_ERROR( "WebSocket error: %s", qPrintable( socket_.errorString() ) );
                    } );
#elif QT_VERSION_MAJOR < 6
  QObject::connect( &socket_, QOverload<QAbstractSocket::SocketError>::of( &QWebSocket::error ),
                    [this]( QAbstractSocket::SocketError ) {
                      HCS_LOG_ERROR( "WebSocket error: %s", qPrintable( socket_.errorString() ) );
                    } );
#endif

  // Liveness guard shared with the ICE-state callback (see header). client points back to this and
  // stays valid for this object's lifetime; the destructor flips destroyed under the guard lock.
  ice_guard_ = std::make_shared<IceCallbackGuard>();
  ice_guard_->client = this;

  createWebRTCBin();

  HCS_LOG_INFO( "Connecting to WebRTC signaling server: %s", qPrintable( url ) );
  socket_.open( QUrl( url ) );
}

WebRTCClient::~WebRTCClient()
{
  // Block the ICE-state callback (libnice thread) from touching this object from here on, and wait
  // out any call already holding the guard. The guard outlives this object, so an in-flight
  // callback can still safely check the flag even though `this` is being destroyed.
  {
    std::lock_guard lock( ice_guard_->mutex );
    ice_guard_->destroyed = true;
  }

  // Prevent any further signals from socket (especially disconnected which triggers timer)
  socket_.disconnect();
  reconnect_timer_.stop();

  destroyWebRTCBin();
  if ( pipeline_ ) {
    gst_object_unref( pipeline_ );
  }
}

void WebRTCClient::createWebRTCBin()
{
  if ( webrtcbin_ )
    return;

  webrtcbin_ = gst_element_factory_make( "webrtcbin", nullptr );
  if ( !webrtcbin_ ) {
    HCS_LOG_ERROR( "Failed to create webrtcbin" );
    return;
  }

  g_object_set( webrtcbin_, "bundle-policy", 3 /* max-bundle */, nullptr );

  // libnice's ICE agent runs UPnP-IGD discovery by default, sending SSDP M-SEARCH
  // packets to 239.255.255.250 to find a router for automatic port mapping. We rely
  // solely on signaling for connectivity, not router port mapping, so disable UPnP.
  {
    GObject *ice = nullptr;
    g_object_get( webrtcbin_, "ice-agent", &ice, nullptr );
    if ( ice != nullptr ) {
      GObject *nice_agent = nullptr;
      g_object_get( ice, "agent", &nice_agent, nullptr );
      if ( nice_agent != nullptr ) {
        // The property only exists when libnice was built with GUPnP
        if ( g_object_class_find_property( G_OBJECT_GET_CLASS( nice_agent ), "upnp" ) != nullptr ) {
          g_object_set( nice_agent, "upnp", FALSE, nullptr );
        }
        g_object_unref( nice_agent );
      }
      g_object_unref( ice );
    }
  }

  if ( !gst_bin_add( GST_BIN( pipeline_ ), webrtcbin_ ) ) {
    HCS_LOG_ERROR( "Failed to add webrtcbin to pipeline" );
    gst_object_unref( webrtcbin_ );
    webrtcbin_ = nullptr;
    return;
  }

  // Connect signals. All three handlers can fire on threads that outlive this object (libnice ICE
  // thread, webrtcbin streaming thread), and disconnecting a handler does not wait out one already
  // running. So each carries the shared liveness guard (not `this`) as user_data and is recorded by
  // id; destroyWebRTCBin disconnects them by id and the trampolines bail under the guard if destroyed.
  const auto connect_guarded = [this]( const char *signal, GCallback cb ) {
    return g_signal_connect_data(
        webrtcbin_, signal, cb, new std::shared_ptr<IceCallbackGuard>( ice_guard_ ),
        []( gpointer data, GClosure * ) {
          delete static_cast<std::shared_ptr<IceCallbackGuard> *>( data );
        },
        static_cast<GConnectFlags>( 0 ) );
  };
  ice_candidate_handler_id_ = connect_guarded( "on-ice-candidate", G_CALLBACK( onIceCandidateSt ) );
  pad_added_handler_id_ = connect_guarded( "pad-added", G_CALLBACK( onPadAddedSt ) );
  // Watch the ICE transport so we can recover before the server notices and closes signaling.
  ice_state_handler_id_ =
      connect_guarded( "notify::ice-connection-state", G_CALLBACK( onIceConnectionStateChangedSt ) );

  // Sync state with pipeline
  gst_element_sync_state_with_parent( webrtcbin_ );
}

void WebRTCClient::destroyWebRTCBin()
{
  if ( !webrtcbin_ )
    return;

  // Invalidate all outstanding promise callbacks
  ++connection_id_;

  // All handlers carry the shared guard as user_data (not `this`), so disconnect each by id.
  // Disconnect does not wait out an in-flight callback, but the guard makes that safe: such a
  // callback either already bailed on the destroyed flag or holds the guard, which the destructor's
  // lock and (for the promise callbacks) the connection_id bump above serialize against.
  const auto disconnect = [this]( gulong &id ) {
    if ( id != 0 ) {
      g_signal_handler_disconnect( webrtcbin_, id );
      id = 0;
    }
  };
  disconnect( ice_candidate_handler_id_ );
  disconnect( pad_added_handler_id_ );
  disconnect( ice_state_handler_id_ );

  // Explicitly unlink webrtcbin from input_queue before removal
  GstElement *queue = gst_bin_get_by_name( GST_BIN( pipeline_ ), "input_queue" );
  if ( queue ) {
    GstPad *sink_pad = gst_element_get_static_pad( queue, "sink" );
    if ( sink_pad ) {
      GstPad *peer = gst_pad_get_peer( sink_pad );
      if ( peer ) {
        gst_pad_unlink( peer, sink_pad );
        gst_object_unref( peer );
      }
      gst_object_unref( sink_pad );
    }
    gst_object_unref( queue );
  }

  gst_element_set_locked_state( webrtcbin_, TRUE );

  // Take an extra ref before removing from pipeline so the element stays alive for async cleanup
  gst_object_ref( webrtcbin_ );
  gst_bin_remove( GST_BIN( pipeline_ ), webrtcbin_ );

  // Move state transition + final unref to a background thread to avoid blocking the UI thread.
  // The webrtcbin's ICE/DTLS sockets are only released once the NULL state transition completes.
  GstElement *element = webrtcbin_;
  webrtcbin_ = nullptr;
  std::thread( [element]() {
    gst_element_set_state( element, GST_STATE_NULL );
    gst_element_get_state( element, nullptr, nullptr, 5 * GST_SECOND );
    gst_object_unref( element );
  } ).detach();
}

void WebRTCClient::onConnected()
{
  HCS_LOG_INFO( "WebSocket connected!" );
  createWebRTCBin();
  reconnect_timer_.stop();
}

void WebRTCClient::onDisconnected()
{
  destroyWebRTCBin();
  scheduleReconnect();
}

void WebRTCClient::scheduleReconnect()
{
  // Base delay plus jitter so many clients reconnecting after a server restart spread their
  // attempts instead of hammering the signaling server in lockstep.
  constexpr int RECONNECT_BASE_MS = 2000;
  constexpr int RECONNECT_JITTER_MS = 1000;
  std::uniform_int_distribution<int> jitter( 0, RECONNECT_JITTER_MS );
  const int delay = RECONNECT_BASE_MS + jitter( rng_ );
  HCS_LOG_INFO( "WebSocket disconnected. Reconnecting in %d ms...", delay );
  reconnect_timer_.start( delay );
}

void WebRTCClient::onTextMessageReceived( const QString &message )
{
  QJsonDocument doc = QJsonDocument::fromJson( message.toUtf8() );
  QJsonObject json = doc.object();

  if ( json.contains( "type" ) && json.contains( "sdp" ) ) {
    QString type = json["type"].toString();
    QString sdp = json["sdp"].toString();
    HCS_LOG_DEBUG( "WebRTCClient: Received SDP: %s", qPrintable( sdp ) );
    handleSdp( type, sdp );
  } else if ( json.contains( "ice" ) ) {
    QJsonObject ice = json["ice"].toObject();
    if ( ice.contains( "candidate" ) && ice.contains( "sdpMLineIndex" ) ) {
      handleIceCandidate( ice["sdpMLineIndex"].toInt(), ice["candidate"].toString() );
    }
  }
}

void WebRTCClient::handleSdp( const QString &type, const QString &sdpStr )
{
  if ( !webrtcbin_ )
    return;

  GstSDPMessage *sdp;
  if ( gst_sdp_message_new( &sdp ) != GST_SDP_OK ) {
    HCS_LOG_ERROR( "Failed to create SDP message" );
    return;
  }

  std::string sdpStd = sdpStr.toStdString();
  if ( gst_sdp_message_parse_buffer( (const guint8 *)sdpStd.c_str(), sdpStd.size(), sdp ) !=
       GST_SDP_OK ) {
    HCS_LOG_ERROR( "Failed to parse SDP" );
    gst_sdp_message_free( sdp );
    return;
  }

  GstWebRTCSDPType sdpType =
      ( type == "offer" ) ? GST_WEBRTC_SDP_TYPE_OFFER : GST_WEBRTC_SDP_TYPE_ANSWER;
  GstWebRTCSessionDescription *desc = gst_webrtc_session_description_new( sdpType, sdp );

  auto *ctx = new PromiseContext{ ice_guard_, connection_id_.load() };
  GstPromise *promise =
      gst_promise_new_with_change_func( onRemoteDescriptionSetSt, ctx, []( gpointer data ) {
        delete static_cast<PromiseContext *>( data );
      } );
  g_signal_emit_by_name( webrtcbin_, "set-remote-description", desc, promise );
  gst_webrtc_session_description_free( desc );
}

void WebRTCClient::onRemoteDescriptionSetSt( GstPromise *promise, gpointer user_data )
{
  auto *ctx = static_cast<PromiseContext *>( user_data );
  // The promise may be replied on a webrtcbin/streaming thread during teardown: destroyWebRTCBin
  // drives the element to NULL on a detached thread that outlives ~WebRTCClient. Hold the liveness
  // guard across the handler so the destructor cannot free the client under us, then bail if it
  // already did (same guard the ICE-state callback uses). The guard is recursive because
  // onRemoteDescriptionSet emits create-answer under this lock: once the bin has begun closing,
  // that emit replies its new promise inline on this thread, re-entering onAnswerCreatedSt, which
  // then bails on the connection_id instead of self-deadlocking.
  std::lock_guard lock( ctx->guard->mutex );
  if ( ctx->guard->destroyed ) {
    gst_promise_unref( promise );
    return;
  }
  WebRTCClient *client = ctx->guard->client;
  if ( ctx->connection_id != client->connection_id_.load() ) {
    HCS_LOG_WARN( "Ignoring stale set-remote-description callback (old connection)" );
    gst_promise_unref( promise );
    return;
  }
  client->onRemoteDescriptionSet( promise, ctx->connection_id );
}

void WebRTCClient::onRemoteDescriptionSet( GstPromise *promise, uint64_t conn_id )
{
  if ( gst_promise_wait( promise ) != GST_PROMISE_RESULT_REPLIED ) {
    HCS_LOG_ERROR( "Failed to set remote description: Promise not replied" );
    gst_promise_unref( promise );
    if ( conn_id == connection_id_.load() )
      closeSignaling();
    return;
  }

  const GstStructure *reply = gst_promise_get_reply( promise );
  if ( reply && gst_structure_has_field( reply, "error" ) ) {
    GError *err = nullptr;
    gst_structure_get( reply, "error", G_TYPE_ERROR, &err, nullptr );
    HCS_LOG_ERROR( "Set remote description error: %s", err ? err->message : "unknown" );
    if ( err )
      g_error_free( err );

    gst_promise_unref( promise );
    if ( conn_id == connection_id_.load() )
      closeSignaling();
    return;
  }

  gst_promise_unref( promise );

  if ( conn_id != connection_id_.load() ) {
    HCS_LOG_WARN( "Connection changed during remote description set, not creating answer" );
    return;
  }

  // Create answer
  auto *ctx = new PromiseContext{ ice_guard_, conn_id };
  GstPromise *answerPromise = gst_promise_new_with_change_func(
      onAnswerCreatedSt, ctx, []( gpointer data ) { delete static_cast<PromiseContext *>( data ); } );
  g_signal_emit_by_name( webrtcbin_, "create-answer", nullptr, answerPromise );
}

void WebRTCClient::onAnswerCreatedSt( GstPromise *promise, gpointer user_data )
{
  auto *ctx = static_cast<PromiseContext *>( user_data );
  // See onRemoteDescriptionSetSt: hold the liveness guard so the client cannot be freed by a
  // concurrent teardown while this runs, and bail if it already was.
  std::lock_guard lock( ctx->guard->mutex );
  if ( ctx->guard->destroyed ) {
    gst_promise_unref( promise );
    return;
  }
  WebRTCClient *client = ctx->guard->client;
  if ( ctx->connection_id != client->connection_id_.load() ) {
    HCS_LOG_WARN( "Ignoring stale create-answer callback (old connection)" );
    gst_promise_unref( promise );
    return;
  }
  client->onAnswerCreated( promise, ctx->connection_id );
}

void WebRTCClient::onAnswerCreated( GstPromise *promise, uint64_t conn_id )
{
  const GstStructure *reply = gst_promise_get_reply( promise );

  if ( reply && gst_structure_has_field( reply, "error" ) ) {
    GError *err = nullptr;
    gst_structure_get( reply, "error", G_TYPE_ERROR, &err, nullptr );
    HCS_LOG_ERROR( "Create answer error: %s", err ? err->message : "unknown" );
    if ( err )
      g_error_free( err );
  }

  GstWebRTCSessionDescription *answer = nullptr;
  if ( reply ) {
    gst_structure_get( reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, nullptr );
  }
  gst_promise_unref( promise );

  if ( !answer ) {
    HCS_LOG_ERROR( "Failed to create answer" );
    if ( conn_id == connection_id_.load() )
      closeSignaling();
    return;
  }

  if ( conn_id != connection_id_.load() ) {
    HCS_LOG_WARN( "Connection changed during answer creation, discarding answer" );
    gst_webrtc_session_description_free( answer );
    return;
  }

  g_signal_emit_by_name( webrtcbin_, "set-local-description", answer, nullptr );

  gchar *sdpCStr = gst_sdp_message_as_text( answer->sdp );
  QString sdp = QString::fromUtf8( sdpCStr );
  g_free( sdpCStr );
  gst_webrtc_session_description_free( answer );

  sendSdp( "answer", sdp );
}

void WebRTCClient::handleIceCandidate( int mlineIndex, const QString &candidate )
{
  if ( webrtcbin_ ) {
    g_signal_emit_by_name( webrtcbin_, "add-ice-candidate", mlineIndex,
                           candidate.toStdString().c_str() );
  }
}

void WebRTCClient::onPadAddedSt( GstElement *, GstPad *pad, gpointer user_data )
{
  // Runs on a webrtcbin streaming thread that can outlive destruction; hold the guard so the client
  // cannot be freed under us and bail if it already was (see onIceConnectionStateChangedSt).
  auto guard = *static_cast<std::shared_ptr<IceCallbackGuard> *>( user_data );
  std::lock_guard lock( guard->mutex );
  if ( guard->destroyed )
    return;
  guard->client->onPadAdded( pad );
}

void WebRTCClient::onPadAdded( GstPad *pad )
{
  if ( !pipeline_ )
    return;

  GstElement *queue = gst_bin_get_by_name( GST_BIN( pipeline_ ), "input_queue" );
  if ( !queue ) {
    HCS_LOG_ERROR( "Failed to find input_queue in pipeline" );
    return;
  }

  GstPad *sink_pad = gst_element_get_static_pad( queue, "sink" );
  if ( !gst_pad_is_linked( sink_pad ) ) {
    if ( gst_pad_link( pad, sink_pad ) != GST_PAD_LINK_OK ) {
      HCS_LOG_ERROR( "Failed to link webrtcbin pad to input_queue" );
    } else {
      HCS_LOG_INFO( "Successfully linked webrtcbin pad to input_queue" );
      // Flush downstream elements to reset their internal state (depay, parser, decoder)
      // so they accept fresh data from the new WebRTC session.
      // reset_time must be FALSE: resetting the running time redistributes the pipeline's
      // base time while the new session already streams, so its first packets get stamped
      // on the old timeline and the rest on the new one. The jitterbuffer then sees a
      // backwards PTS jump and stalls for a gap-recovery period of unpredictable length
      // (observed up to ~15 s of frozen/inflated latency readings after a reconnect).
      gst_pad_send_event( sink_pad, gst_event_new_flush_start() );
      gst_pad_send_event( sink_pad, gst_event_new_flush_stop( FALSE ) );
    }
  }
  gst_object_unref( sink_pad );
  gst_object_unref( queue );
}

void WebRTCClient::onIceConnectionStateChangedSt( GstElement *webrtcbin, GParamSpec *,
                                                  gpointer user_data )
{
  // Runs on a GStreamer/libnice thread. Copy the shared guard so it (and its mutex) stay alive for
  // this call even if the client is destroyed and the closure freed mid-call. Lock it, then bail if
  // the client is gone; otherwise hold the lock across the work so the destructor's matching lock
  // blocks until we return, keeping the object alive throughout.
  auto guard = *static_cast<std::shared_ptr<IceCallbackGuard> *>( user_data );
  std::lock_guard lock( guard->mutex );
  if ( guard->destroyed )
    return;
  guard->client->onIceConnectionStateChanged( webrtcbin );
}

void WebRTCClient::onIceConnectionStateChanged( GstElement *webrtcbin )
{
  // Called with ice_guard_->mutex held (see onIceConnectionStateChangedSt), so `this` stays alive.
  // g_object_get writes a gint for enum properties; reading into the enum-typed variable
  // directly would corrupt the stack under -fshort-enums. Read as gint, then narrow.
  gint state_raw = 0;
  g_object_get( webrtcbin, "ice-connection-state", &state_raw, nullptr );

  // Only FAILED is terminal; DISCONNECTED is often transient and may recover to CONNECTED on
  // its own (a premature reconnect would needlessly drop a recoverable session). A DISCONNECTED
  // state that never recovers stops frames and is caught by the pipeline stall watchdog instead.
  if ( static_cast<GstWebRTCICEConnectionState>( state_raw ) ==
       GST_WEBRTC_ICE_CONNECTION_STATE_FAILED ) {
    // closeSignaling() marshals the close onto the socket's thread, so it is safe even if the
    // object is torn down before the queued close runs.
    HCS_LOG_WARN( "WebRTC ICE connection failed, closing signaling to trigger reconnect" );
    closeSignaling();
  }
}

void WebRTCClient::onIceCandidateSt( GstElement *, guint mlineIndex, gchar *candidate,
                                     gpointer user_data )
{
  // Runs on a libnice ICE thread that can outlive destruction; hold the guard so the client cannot
  // be freed under us and bail if it already was (see onIceConnectionStateChangedSt).
  auto guard = *static_cast<std::shared_ptr<IceCallbackGuard> *>( user_data );
  std::lock_guard lock( guard->mutex );
  if ( guard->destroyed )
    return;
  guard->client->sendIceCandidate( mlineIndex, candidate );
}

void WebRTCClient::sendIceCandidate( int mlineIndex, const gchar *candidate )
{
  QJsonObject ice;
  ice["candidate"] = QString::fromUtf8( candidate );
  ice["sdpMLineIndex"] = mlineIndex;
  QJsonObject msg;
  msg["ice"] = ice;
  sendJson( msg );
}

void WebRTCClient::sendSdp( const QString &type, const QString &sdp )
{
  QJsonObject msg;
  msg["type"] = type;
  msg["sdp"] = sdp;
  sendJson( msg );
}

void WebRTCClient::sendJson( const QJsonObject &msg )
{
  // Senders are invoked from GStreamer promise / libnice threads; marshal the actual send onto the
  // socket's (main) thread so QWebSocket is only ever touched there.
  QString jsonStr = QString::fromUtf8( QJsonDocument( msg ).toJson( QJsonDocument::Compact ) );
  QMetaObject::invokeMethod(
      &socket_, [this, jsonStr]() { socket_.sendTextMessage( jsonStr ); }, Qt::QueuedConnection );
}

void WebRTCClient::closeSignaling()
{
  QMetaObject::invokeMethod( &socket_, "close", Qt::QueuedConnection );
}

} // namespace ros_camera_server_qml_common
