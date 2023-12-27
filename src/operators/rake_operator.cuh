#include "../structs.h"
#include "rake_operator.h"
#include <thrust/functional.h>
#include <Eigen/Geometry>

namespace pc::operators {

using pc::types::color;
using pc::types::position;

__device__ point_t RakeOperator::operator()(point_t point) const {

  position pos = thrust::get<0>(point);
  color col = thrust::get<1>(point);

  pc::types::Float3 pos_f{pos.x, pos.y, pos.z};

  auto& depth = _config.depth_min_max;
  auto& height = _config.height_min_max;

  float normalised_depth = (float)(pos.z - depth.min) / (float)(depth.max - depth.min);
  float mapped_height = normalised_depth * (height.max - height.min) + height.min;

  // Add the mapped height to the y position to create a "raked" floor
  pos_f.y += mapped_height;

  position pos_out = { (short)__float2int_rd(pos_f.x),
					  (short)__float2int_rd(pos_f.y),
					  (short)__float2int_rd(pos_f.z), 0 };

  return thrust::make_tuple(pos_out, col);
};

} // namespace pc::operators
