#pragma once

#include "../gui_helpers.h"
#include "../pointer.h"
#include "driver.h"
#include "device_config.h"
#include <Corrade/Containers/Pointer.h>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <iterator>
#include <memory>
#include <mutex>
#include <pointclouds.h>
#include <thread>
#include <variant>
#include <vector>

namespace pc::sensors {

enum DeviceType { UnknownDevice, K4A, K4W2, Rs2 };

class Device {
public:
  static std::vector<std::shared_ptr<Device>> attached_devices;
  static std::mutex devices_access;

  std::string name = "";
  bool is_sensor = true;
  bool paused = false;

  DeviceConfiguration config {true, // flip_x
                              false, // flip_y
                              true, // flip_z
                              {-10000, 10000}, // crop_x
                              {-10000, 10000}, // crop_y
                              {-10000, 10000}, // crop_z
                                        {0, -930, 1520}, // offset
                                        {-5, 0, 0}, // rotation_deg
                                        1.2f, // scale
                                        1 // sample
  }; // 

  virtual std::string get_broadcast_id() = 0;

  bool broadcast_enabled() { return _enable_broadcast; }
  auto point_cloud() { return _driver->point_cloud(config); };

  void draw_imgui_controls();

  void serialize_config() const;
  void deserialize_config(std::vector<uint8_t> data);
  void deserialize_config_from_device_id(const std::string &device_id);
  void deserialize_config_from_this_device();

protected:
  std::unique_ptr<Driver> _driver;
  bool _enable_broadcast = true;

  // implement this to add device-specific options with imgui
  virtual void draw_device_controls() {}

  const std::string label(std::string label_text, int index = 0) {
    ImGui::Text("%s", label_text.c_str());
    ImGui::SameLine();
    return "##" + name + "_" + _driver->id() + "_" + label_text + "_" +
           std::to_string(index);
  }
};

extern pc::types::PointCloud synthesized_point_cloud();

extern pc::types::position global_translate;
extern void draw_global_controls();

} // namespace pc::sensors
