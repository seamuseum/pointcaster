#pragma once

#include "../../structs.h"
#include <array>
#include <cmath>
#include <k4a/k4a.hpp>
#include <k4abt.hpp>

namespace bob::k4a_utils {

std::array<float3, 32>
calculateAverageJointPositions(const std::vector<k4abt_skeleton_t> &skeletons) {
  std::array<float3, 32> averages;
  for (int joint_id = 0; joint_id < 32; joint_id++) {
    float3 sum { 0, 0, 0 };
    for (const auto &skeleton : skeletons) {
      const auto joint = skeleton.joints[joint_id];
      const auto position = joint.position.xyz;
      sum.x += position.x;
      sum.y += position.y;
      sum.z += position.z;
    }
    auto skeleton_count = skeletons.size();
    averages[joint_id] = {sum.x / skeleton_count,
			  sum.y / skeleton_count,
			  sum.z / skeleton_count};
  }
  return averages;
}

std::array<float4, 32> calculateAverageJointOrientations(
    const std::vector<k4abt_skeleton_t> &skeletons) {
  std::array<float4, 32> averages;
  for (int joint_id = 0; joint_id < 32; joint_id++) {
    float4 sum{0, 0, 0, 0};
    for (const auto &skeleton : skeletons) {
      const auto joint = skeleton.joints[joint_id];
      const auto orientation = joint.orientation.wxyz;
      sum.w += orientation.w;
      sum.x += orientation.x;
      sum.y += orientation.y;
      sum.z += orientation.z;
    }
    float k = 1.0f / std::sqrt(sum.w * sum.w + sum.x * sum.x
			       + sum.y * sum.y + sum.z * sum.z);
    averages[joint_id] = {sum.w * k, sum.x * k, sum.y * k, sum.z * k};
  }
  return averages;
}

} // namespace bob::k4a
