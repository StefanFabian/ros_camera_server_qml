// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef ROS_CAMERA_SERVER_QML_COMMON_WEBRTC_CLIENT_HPP
#define ROS_CAMERA_SERVER_QML_COMMON_WEBRTC_CLIENT_HPP

#include <QObject>
#include <QTimer>
#include <QWebSocket>
#include <gst/gst.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>

class QJsonObject;

namespace ros_camera_server_qml_common
{

class WebRTCClient
{
  //! Liveness guard shared by the libnice ICE-state callback and the GStreamer promise callbacks,
  //! all of which can fire on threads that outlive destruction (destroyWebRTCBin drives webrtcbin
  //! to NULL on a detached thread, replying any pending promise there).
  struct IceCallbackGuard {
    //! Recursive because a promise callback holds it across a nested webrtcbin signal emit
    //! (create-answer): if the bin has begun closing on the detached teardown thread, that emit
    //! replies the new promise inline on this same thread, re-entering the guard.
    std::recursive_mutex mutex;
    bool destroyed = false;
    WebRTCClient *client = nullptr;
  };

  struct PromiseContext {
    std::shared_ptr<IceCallbackGuard> guard;
    uint64_t connection_id;
  };

public:
  WebRTCClient( const QString &url, GstElement *pipeline );
  ~WebRTCClient();

private:
  void onConnected();
  void onDisconnected();
  void onTextMessageReceived( const QString &message );
  void createWebRTCBin();
  void destroyWebRTCBin();

  // Signaling handlers
  void handleSdp( const QString &type, const QString &sdpStr );
  void handleIceCandidate( int mlineIndex, const QString &candidate );

  // GStreamer callbacks
  static void onRemoteDescriptionSetSt( GstPromise *promise, gpointer user_data );
  void onRemoteDescriptionSet( GstPromise *promise, uint64_t conn_id );

  static void onAnswerCreatedSt( GstPromise *promise, gpointer user_data );
  void onAnswerCreated( GstPromise *promise, uint64_t conn_id );

  static void onIceCandidateSt( GstElement *, guint mlineIndex, gchar *candidate, gpointer user_data );

  static void onPadAddedSt( GstElement *element, GstPad *pad, gpointer user_data );
  void onPadAdded( GstPad *pad );

  static void onIceConnectionStateChangedSt( GstElement *webrtcbin, GParamSpec *pspec,
                                             gpointer user_data );
  void onIceConnectionStateChanged( GstElement *webrtcbin );

  //! Schedules a reconnect after a randomized delay (base + jitter) so that many clients
  //! reconnecting after a server restart do not do so in lockstep (thundering herd).
  void scheduleReconnect();

  // Signaling senders
  void sendIceCandidate( int mlineIndex, const gchar *candidate );
  void sendSdp( const QString &type, const QString &sdp );
  //! Serializes the signaling text-message send onto the socket's (main) thread. Callable from any
  //! thread (GStreamer promise/libnice threads emit candidates and answers).
  void sendJson( const QJsonObject &msg );
  //! Closes the signaling socket to trigger a reconnect. Marshals the close onto the socket's (main)
  //! thread, so it is callable from the GStreamer promise / libnice threads detecting a failed session.
  void closeSignaling();

  GstElement *pipeline_ = nullptr;
  GstElement *webrtcbin_ = nullptr;
  std::atomic<uint64_t> connection_id_{ 0 };
  QWebSocket socket_;
  QString url_;
  QTimer reconnect_timer_;
  std::mt19937 rng_{ std::random_device{}() };

  //! Liveness guard for the ICE-state notify and promise callbacks (see IceCallbackGuard above). The
  //! signal closure / promise contexts keep it alive via shared_ptr past this object's destruction.
  std::shared_ptr<IceCallbackGuard> ice_guard_;
  //! Handler ids of the webrtcbin signal connections. All carry the shared guard (not `this`) as
  //! user_data so an off-thread callback (libnice ICE / webrtcbin streaming thread) cannot touch a
  //! freed client; destroyWebRTCBin disconnects each by id rather than via disconnect_by_data(this).
  gulong ice_state_handler_id_ = 0;
  gulong ice_candidate_handler_id_ = 0;
  gulong pad_added_handler_id_ = 0;
};

} // namespace ros_camera_server_qml_common

#endif // ROS_CAMERA_SERVER_QML_COMMON_WEBRTC_CLIENT_HPP
