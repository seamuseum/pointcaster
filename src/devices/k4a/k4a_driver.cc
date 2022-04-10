#include "k4a_driver.h"
#include <cstdint>
#include <k4a/k4a.h>
#include <k4a/k4atypes.h>
#include <limits>
#include <spdlog/spdlog.h>

namespace bob::sensors {

using namespace Magnum;
using namespace std::chrono_literals;

K4ADriver::K4ADriver(int device_index_) {
  device_index = device_index_;
  open();
}

K4ADriver::~K4ADriver() { close(); }

bool K4ADriver::open() {
  _device = k4a::device::open(device_index);

  // TODO move the rest of this to the cpp api

  // TODO make config dynamic
  _config = K4A_DEVICE_CONFIG_INIT_DISABLE_ALL;
  _config.color_format = K4A_IMAGE_FORMAT_COLOR_BGRA32;
  _config.color_resolution = K4A_COLOR_RESOLUTION_720P;
  _config.depth_mode = K4A_DEPTH_MODE_WFOV_2X2BINNED;
  _config.camera_fps = K4A_FRAMES_PER_SECOND_30;
  _config.synchronized_images_only = true;

  _device.start_cameras(&_config);

  // need to store the calibration and transformation data, as they are used
  // later to transform colour camera to point cloud space
  _calibration =
      _device.get_calibration(_config.depth_mode, _config.color_resolution);
  _transformation = k4a::transformation(_calibration);

  // start a thread that captures new frames and dumps them into raw buffers
  std::thread capture_loop([&]() {
    constexpr int color_pixel_size = 4 * static_cast<int>(sizeof(uint8_t));
    constexpr int depth_pixel_size = static_cast<int>(sizeof(int16_t));
    constexpr int depth_point_size = 3 * static_cast<int>(sizeof(int16_t));

    while (isOpen()) {
      k4a::capture capture;
      bool result = _device.get_capture(&capture, 1000ms);
      if (!result)
	continue;
      auto depth_image = capture.get_depth_image();
      const int depth_width = depth_image.get_width_pixels();
      const int depth_height = depth_image.get_height_pixels();
      auto color_image = capture.get_color_image();

      // transform the color image to the depth image
      k4a::image transformed_color_image =
	  _transformation.color_image_to_depth_camera(depth_image, color_image);
      // transform the depth image to a point cloud
      k4a::image point_cloud_image = _transformation.depth_image_to_point_cloud(
	  depth_image, K4A_CALIBRATION_TYPE_DEPTH);

      auto colors_buffer = transformed_color_image.get_buffer();
      auto colors_buffer_size = transformed_color_image.get_size();

      auto positions_buffer =
	  reinterpret_cast<int16_t *>(point_cloud_image.get_buffer());
      const auto positions_buffer_size = point_cloud_image.get_size();

      std::lock_guard<std::mutex> lock(_buffer_mutex);
      _point_count = depth_width * depth_height;
      _positions_buffer.resize(positions_buffer_size);
      memcpy(_positions_buffer.data(), positions_buffer, positions_buffer_size);
      _colors_buffer.resize(colors_buffer_size);
      memcpy(_colors_buffer.data(), colors_buffer, colors_buffer_size);
      _buffers_updated = true;
    }
  });
  capture_loop.detach();

  _open = true;
  return true;
}

bool K4ADriver::close() {
  _open = false;
  _device.close();
  return true;
}

PointCloud K4ADriver::getPointCloud() {
  if (!_buffers_updated)
    return _point_cloud;
  std::lock_guard<std::mutex> lock(_buffer_mutex);

  std::vector<float3> positions(_point_count);
  std::vector<float> colors(_point_count);
  memcpy(colors.data(), _colors_buffer.data(), _point_count * sizeof(float));

  // TODO the following transform from short to float should be done in the
  // shader
  for (int i = 0; i < _point_count; i++) {
    const int pos = i * 3;
    const float x = (_positions_buffer[pos] / -1100.0f) + 1.25f;
    const float y = (_positions_buffer[pos + 1] / -1100.0f) + 0.55f;
    const float z = _positions_buffer[pos + 2] / -1100.0f;
    positions[i] = (float3{x, y, z});
  }

  _point_cloud = PointCloud{positions, colors};
  _buffers_updated = false;

  return _point_cloud;
}

} // namespace bob::sensors
