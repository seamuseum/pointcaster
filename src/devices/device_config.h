#pragma once

#include "../serialization.h"
#include "../structs.h"
#include <k4a/k4atypes.h>

namespace pc::devices {

using pc::types::Float3;
using pc::types::MinMax;
using pc::types::Short3;
using MinMaxShort = pc::types::MinMax<short>;

struct BodyTrackingConfiguration {
  bool unfolded = false;
  bool enabled = false;
};

struct AutoTiltConfiguration {
  bool enabled = false;
  float lerp_factor = 0.025f;
  float threshold = 1.0f; // degrees
};

struct K4AConfiguration {
  bool unfolded = false;
  int depth_mode = (int)K4A_DEPTH_MODE_NFOV_UNBINNED;
  int exposure = 10000;
  int brightness = 128;
  int contrast = 5;
  int saturation = 31;
  int gain = 128;
  AutoTiltConfiguration auto_tilt; // @optional
};

struct DeviceConfiguration {
  bool flip_x = false; 
  bool flip_y = false;
  bool flip_z = false;
  MinMaxShort crop_x{-10000, 10000}; // @minmax(-10000, 10000)
  MinMaxShort crop_y{-10000, 10000}; // @minmax(-10000, 10000)
  MinMaxShort crop_z{-10000, 10000}; // @minmax(-10000, 10000)
  MinMaxShort bound_x{-10000, 10000}; // @minmax(-10000, 10000)
  MinMaxShort bound_y{-10000, 10000}; // @minmax(-10000, 10000)
  MinMaxShort bound_z{-10000, 10000}; // @minmax(-10000, 10000)
  Float3 translate{0, 0, 0}; // @minmax(-10000, 10000)
  Float3 offset{0, 0, 0};
  Float3 rotation_deg{0, 0, 0}; // @minmax(-360, 360)
  float scale = 1; // @minmax(0, 10)
  int sample = 1; 
  BodyTrackingConfiguration body; // @optional
  K4AConfiguration k4a; // @optional
};

} // namespace pc::devices
