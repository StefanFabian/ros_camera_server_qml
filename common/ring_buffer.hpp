// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef ROS_CAMERA_SERVER_QML_COMMON_RING_BUFFER_HPP
#define ROS_CAMERA_SERVER_QML_COMMON_RING_BUFFER_HPP

#include <array>
#include <cstddef>

namespace ros_camera_server_qml_common
{

template<typename T, size_t Size>
class RingBuffer
{
private:
  std::array<T, Size> buffer;
  size_t head = 0;
  size_t tail = 0;
  bool full = false;

public:
  void push( const T &value )
  {
    buffer[head] = value;
    if ( full ) {
      tail = ( tail + 1 ) % Size;
    }
    head = ( head + 1 ) % Size;
    full = ( head == tail );
  }

  //! The oldest element in the buffer.
  const T &front() const { return buffer[tail]; }

  //! The newest element in the buffer.
  const T &back()
  {
    int index = int( head ) - 1;
    if ( index < 0 ) {
      index += Size;
    }
    return buffer[index];
  }

  T &operator[]( size_t index )
  {
    index = tail + index;
    if ( index >= Size ) {
      index -= Size;
    }
    return buffer[index];
  }

  const T &operator[]( size_t index ) const
  {
    index = tail + index;
    if ( index >= Size ) {
      index -= Size;
    }
    return buffer[index];
  }

  bool empty() const { return ( !full && ( head == tail ) ); }

  size_t size() const
  {
    if ( full ) {
      return Size;
    }
    return ( head >= tail ) ? ( head - tail ) : ( Size + head - tail );
  }

  void clear()
  {
    full = false;
    head = 0;
    tail = 0;
  }

  void pop_front()
  {
    if ( empty() ) {
      return; // Nothing to pop
    }
    full = false;
    tail = ( tail + 1 ) % Size;
  }
};
} // namespace ros_camera_server_qml_common

#endif // ROS_CAMERA_SERVER_QML_COMMON_RING_BUFFER_HPP
