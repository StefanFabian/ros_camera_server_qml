// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "gstreamer_pipeline_appsink.hpp"
#include "../compat.hpp"

#include <QCoreApplication>
#include <QSize>
#include <QVideoFrame>
#if QT_VERSION_MAJOR >= 6
#include <QVideoFrameFormat>
#else
#include <QAbstractPlanarVideoBuffer>
#endif

#include <gst/video/video.h>

#include <algorithm>

#if QT_VERSION_MAJOR < 6
namespace
{

//! Zero-copy video buffer that wraps a GstSample. The GstSample is kept alive
//! (ref-counted) until this buffer is destroyed, so no data needs to be copied.
class GstVideoBuffer : public QAbstractPlanarVideoBuffer
{
public:
  GstVideoBuffer( GstSample *sample, const GstVideoInfo &video_info )
      : QAbstractPlanarVideoBuffer( NoHandle ), sample_( gst_sample_ref( sample ) ),
        video_info_( video_info ), mapped_( false )
  {
  }

  ~GstVideoBuffer() override
  {
    unmap();
    gst_sample_unref( sample_ );
  }

  MapMode mapMode() const override { return mapped_ ? ReadOnly : NotMapped; }

  int map( MapMode mode, int *numBytes, int bytesPerLine[4], uchar *data[4] ) override
  {
    if ( mapped_ || mode == NotMapped )
      return 0;
    GstBuffer *buffer = gst_sample_get_buffer( sample_ );
    if ( !gst_video_frame_map( &frame_, &video_info_, buffer, GST_MAP_READ ) )
      return 0;
    mapped_ = true;
    int n_planes = GST_VIDEO_FRAME_N_PLANES( &frame_ );
    *numBytes = 0;
    for ( int i = 0; i < n_planes; ++i ) {
      data[i] = static_cast<uchar *>( GST_VIDEO_FRAME_PLANE_DATA( &frame_, i ) );
      bytesPerLine[i] = GST_VIDEO_FRAME_PLANE_STRIDE( &frame_, i );
      int plane_height = GST_VIDEO_FRAME_COMP_HEIGHT( &frame_, i );
      *numBytes += bytesPerLine[i] * plane_height;
    }
    return n_planes;
  }

  void unmap() override
  {
    if ( mapped_ ) {
      gst_video_frame_unmap( &frame_ );
      mapped_ = false;
    }
  }

private:
  GstSample *sample_;
  GstVideoInfo video_info_;
  GstVideoFrame frame_;
  bool mapped_;
};

} // namespace
#endif

namespace ros_camera_server_qml_common
{

std::shared_ptr<GStreamerPipelineAppSink> GStreamerPipelineAppSink::create(
    const CameraStreamInfo &stream, const CameraInfo &camera_info,
    const std::function<void( const QVideoFrame &, StreamStatistics )> &callback )
{
  std::shared_ptr<GStreamerPipelineAppSink> pipeline(
      new GStreamerPipelineAppSink( stream, camera_info, callback ),
      []( GStreamerPipelineAppSink *p ) {
        // The last reference may be dropped on a GStreamer streaming thread (a frame callback that
        // locked the weak_ptr). Marshal the actual delete onto the main thread so the watchdog
        // QTimer is destroyed on its own thread and teardownPipeline does not run there. If the
        // application is already gone (static teardown), the queued delete cannot run; the object
        // then leaks, which is harmless at process exit.
        QMetaObject::invokeMethod(
            QCoreApplication::instance(), [p]() { delete p; }, Qt::QueuedConnection );
      } );
  pipeline->start();
  return pipeline;
}

GStreamerPipelineAppSink::GStreamerPipelineAppSink(
    const CameraStreamInfo &stream, const CameraInfo &camera_info,
    const std::function<void( const QVideoFrame &, StreamStatistics )> &callback )
    : GStreamerPipelineBase( stream, camera_info,
                             []() -> int64_t {
                               const auto &node = hcs_qml_ros2::Ros2Qml::getInstance().node();
                               if ( node )
                                 return node->get_clock()->now().nanoseconds();
                               return 0;
                             } ),
      callback_( callback )
{
}

GStreamerPipelineAppSink::~GStreamerPipelineAppSink() = default;

void GStreamerPipelineAppSink::onSampleReady( GstSample *sample, const StreamStatistics &stats )
{
  GstCaps *caps = gst_sample_get_caps( sample );

  GstVideoInfo video_info;
  gst_video_info_init( &video_info );
  if ( !gst_video_info_from_caps( &video_info, caps ) )
    return;

  int width = GST_VIDEO_INFO_WIDTH( &video_info );
  int height = GST_VIDEO_INFO_HEIGHT( &video_info );

#if QT_VERSION_MAJOR >= 6
  QVideoFrameFormat::PixelFormat qt_format;
  switch ( GST_VIDEO_INFO_FORMAT( &video_info ) ) {
  case GST_VIDEO_FORMAT_NV12:
    qt_format = QVideoFrameFormat::Format_NV12;
    break;
  case GST_VIDEO_FORMAT_I420:
    qt_format = QVideoFrameFormat::Format_YUV420P;
    break;
  case GST_VIDEO_FORMAT_RGBA:
    qt_format = QVideoFrameFormat::Format_RGBA8888;
    break;
  default:
    return;
  }

  GstBuffer *buffer = gst_sample_get_buffer( sample );
  GstVideoFrame gst_frame;
  if ( !gst_video_frame_map( &gst_frame, &video_info, buffer, GST_MAP_READ ) )
    return;

  QVideoFrameFormat frame_format( QSize( width, height ), qt_format );
  QVideoFrame frame( frame_format );

  bool ok = false;
  if ( frame.map( QVideoFrame::WriteOnly ) ) {
    int n_planes = GST_VIDEO_FRAME_N_PLANES( &gst_frame );
    for ( int i = 0; i < n_planes; ++i ) {
      const uchar *src = static_cast<const uchar *>( GST_VIDEO_FRAME_PLANE_DATA( &gst_frame, i ) );
      uchar *dst = frame.bits( i );
      int src_stride = GST_VIDEO_FRAME_PLANE_STRIDE( &gst_frame, i );
      int dst_stride = frame.bytesPerLine( i );
      // Luma plane has full height; chroma planes have half height for 4:2:0 formats
      int plane_height = ( i == 0 ) ? height : ( height + 1 ) / 2;

      if ( src_stride == dst_stride ) {
        memcpy( dst, src, static_cast<size_t>( src_stride ) * plane_height );
      } else {
        int row_bytes = std::min( src_stride, dst_stride );
        for ( int y = 0; y < plane_height; ++y ) {
          memcpy( dst, src, row_bytes );
          src += src_stride;
          dst += dst_stride;
        }
      }
    }
    frame.unmap();
    ok = true;
  }

  gst_video_frame_unmap( &gst_frame );

  if ( ok )
    callback_( frame, stats );
#else
  QVideoFrame::PixelFormat qt_format;
  switch ( GST_VIDEO_INFO_FORMAT( &video_info ) ) {
  case GST_VIDEO_FORMAT_NV12:
    qt_format = QVideoFrame::Format_NV12;
    break;
  case GST_VIDEO_FORMAT_I420:
    qt_format = QVideoFrame::Format_YUV420P;
    break;
  case GST_VIDEO_FORMAT_BGRA:
    qt_format = QVideoFrame::Format_BGRA32;
    break;
  default:
    return;
  }

  // Zero-copy: GstVideoBuffer wraps the GstSample and keeps it alive until
  // the QVideoFrame (and all its implicit-sharing copies) are destroyed.
  auto *video_buffer = new GstVideoBuffer( sample, video_info );
  QVideoFrame frame( video_buffer, QSize( width, height ), qt_format );
  callback_( frame, stats );
#endif
}

} // namespace ros_camera_server_qml_common
