#pragma once

#include "serialization.h"
#include "structs.h"
#include <array>

using pc::types::Int2;
using pc::types::Float2;
using pc::types::MinMax;

enum class ScaleMode {
  Span = 0,
  Letterbox = 1,
  Count = 2
};

enum class LetterboxMode {
  Aspect16x9 = 0,
  Aspect16x10 = 1,
  Count = 2
};

struct PointCloudRendererConfiguration {
  bool unfolded = false;
  Int2 resolution{3840, 2160};
  int scale_mode{(int)ScaleMode::Span};
  int letterbox_mode{(int)LetterboxMode::Aspect16x9};
  bool orthographic = false;
  Float2 orthographic_size = {5, 5};
  MinMax<float> clipping = {0.001f, 200};
  float point_size = 0.0015f;
  bool ground_grid = true;
  bool skeletons = true;
  bool snapshots = true;

  DERIVE_SERDE(
      PointCloudRendererConfiguration,
      (&Self::unfolded, "unfolded")
      (&Self::resolution, "resolution")
      (&Self::scale_mode, "scale_mode")
      (&Self::letterbox_mode, "letterbox_mode")
      (&Self::orthographic, "orthographic")
      (&Self::orthographic_size, "orthographic_size")
      (&Self::clipping, "clipping")
      (&Self::point_size, "point_size")
      (&Self::ground_grid, "ground_grid")
      (&Self::skeletons, "skeletons")
      (&Self::snapshots, "snapshots"))

  using MemberTypes =
      pc::reflect::type_list<bool, Int2, int, int, bool, Float2,
			     MinMax<float>, float, bool, bool, bool>;
  static const std::size_t MemberCount = 11;
};
