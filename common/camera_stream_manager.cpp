// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "camera_stream_manager.hpp"
#include "internal/smart_camera_stream.hpp"
#include "logging.hpp"

#include <cmath>
#include <utility>

namespace ros_camera_server_qml_common
{

namespace
{
void calculateFramerate( RingBuffer<std::chrono::steady_clock::time_point, 10> &frame_times,
                         StreamStatistics &stats )
{
  frame_times.push( std::chrono::steady_clock::now() );
  if ( frame_times.size() > 1 ) {
    auto duration = frame_times.back() - frame_times.front();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( duration ).count();
    if ( ms > 0 ) {
      double framerate = ( frame_times.size() - 1 ) * 1000.0 / ms;
      if ( framerate >= 9.5 ) {
        framerate = std::round( framerate );
      } else {
        framerate = std::round( framerate * 10 ) / 10;
      }
      stats.framerate = framerate;
    }
  }
}
} // namespace

CameraStreamManager &CameraStreamManager::getInstance()
{
  static CameraStreamManager instance;
  return instance;
}

struct CameraStreamManager::Subscription : public CameraStreamManager::SubscriptionBase {
  Subscription( std::string robot, std::string camera_id, FrameCallback callback, int stream_index,
                const QSize &preferred_size )
      : callback( std::move( callback ) ), robot( std::move( robot ) ),
        camera_id( std::move( camera_id ) ), stream_index( stream_index ),
        preferred_size( preferred_size )
  {
  }

  ~Subscription() override { CameraStreamManager::getInstance().removeCallback( this ); }

  FrameCallback callback;
  std::string robot;
  std::string camera_id;
  int stream_index;
  QSize preferred_size;
};

QSize CameraStreamManager::largestPreferredSize( const StreamData &stream_data )
{
  int max_width = 0;
  int max_height = 0;
  for ( const auto *sub : stream_data.subscriptions ) {
    max_width = std::max( max_width, sub->preferred_size.width() );
    max_height = std::max( max_height, sub->preferred_size.height() );
  }
  return { max_width, max_height };
}

std::shared_ptr<CameraStreamManager::SubscriptionBase>
CameraStreamManager::createSubscription( const std::string &robot, const std::string &camera_id,
                                         const CameraStreamManager::FrameCallback &callback,
                                         int stream_index, const QSize &preferred_size )
{
  std::string id = robot + "//" + camera_id + "//" + std::to_string( stream_index );
  auto it = streams_.find( id );
  if ( it == streams_.end() ) {
    auto stream_data = std::make_unique<StreamData>( robot, camera_id, stream_index, preferred_size );
    it = streams_.emplace( id, std::move( stream_data ) ).first;
    HCS_LOG_DEBUG( "Created new cameraId stream for %s", id.c_str() );
  }
  auto subscription =
      std::make_shared<Subscription>( robot, camera_id, callback, stream_index, preferred_size );
  auto &stream_data = it->second;
  stream_data->subscriptions.push_back( subscription.get() );
  stream_data->stream->setPreferredSize( largestPreferredSize( *stream_data ) );
  subscription->transport = stream_data->stream->currentStream().transport();
  subscription->codec = stream_data->stream->currentStream().codec();
  return subscription;
}

void CameraStreamManager::updatePreferredSize( const SubscriptionBase *subscription,
                                               const QSize &preferred_size )
{
  auto *sub = const_cast<Subscription *>( static_cast<const Subscription *>( subscription ) );
  sub->preferred_size = preferred_size;
  auto it = streams_.find( sub->robot + "//" + sub->camera_id + "//" +
                           std::to_string( sub->stream_index ) );
  if ( it == streams_.end() )
    return;
  it->second->stream->setPreferredSize( largestPreferredSize( *it->second ) );
}

void CameraStreamManager::removeCallback( const CameraStreamManager::Subscription *subscription )
{
  auto it = streams_.find( subscription->robot + "//" + subscription->camera_id + "//" +
                           std::to_string( subscription->stream_index ) );
  if ( it == streams_.end() ) {
    return;
  }
  auto &subscriptions = it->second->subscriptions;
  subscriptions.erase( std::remove( subscriptions.begin(), subscriptions.end(), subscription ),
                       subscriptions.end() );
  if ( subscriptions.empty() ) {
    streams_.erase( it );
  } else {
    it->second->stream->setPreferredSize( largestPreferredSize( *it->second ) );
  }
}

CameraStreamManager::StreamData::StreamData( const std::string &robot, const std::string &camera_id,
                                             int stream_index, const QSize &preferred_size )
{
  stream = std::make_unique<SmartCameraStream>(
      robot, camera_id, stream_index, preferred_size,
      [this]( const QVideoFrame &image, StreamStatistics stats ) {
        calculateFramerate( frame_times_, stats );
        for ( auto subscription : subscriptions ) { subscription->callback( image, stats ); }
      } );
  QObject::connect( stream.get(), &SmartCameraStream::currentStreamChanged, [this]() {
    for ( auto subscription : subscriptions ) {
      subscription->transport = stream->currentStream().transport();
      subscription->codec = stream->currentStream().codec();
    }
  } );
}
} // namespace ros_camera_server_qml_common
