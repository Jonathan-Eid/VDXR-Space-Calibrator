// SPDX-License-Identifier: MIT
#pragma once
#include "calibration_math.h"
#include "ultimate_hid_source.h"
#include <iomanip>
#include <sstream>

namespace ultimate_source_contract {
inline std::string deviceKey(const ultimate_hid::Pose& pose) {
    std::ostringstream key;
    key << "rf:" << std::hex << std::setfill('0');
    for (const auto byte : pose.device_id) key << std::setw(2) << unsigned(byte);
    return key.str();
}

inline bool validate(const ultimate_hid::Pose& pose, std::int64_t nowQpc, std::int64_t frequency, double& ageSeconds) {
    ageSeconds = 0;
    if ((pose.flags & 0x07u) != 0x02u || pose.received_qpc <= 0 || frequency <= 0 || nowQpc < pose.received_qpc) return false;
    ageSeconds = double(nowQpc - pose.received_qpc) / double(frequency);
    if (!std::isfinite(ageSeconds) || ageSeconds > 0.1) return false;
    for (float value : pose.position) if (!std::isfinite(value)) return false;
    double norm = 0;
    for (float value : pose.orientation_xyzw) {
        if (!std::isfinite(value)) return false;
        norm += double(value) * value;
    }
    return std::abs(norm - 1.0) <= 0.01;
}

inline calibration::Pose toCalibrationPose(const ultimate_hid::Pose& pose) {
    calibration::Pose result = calibration::Pose::Identity();
    const Eigen::Quaterniond q(pose.orientation_xyzw[3], pose.orientation_xyzw[0],
                               pose.orientation_xyzw[1], pose.orientation_xyzw[2]);
    result.linear() = q.normalized().toRotationMatrix();
    result.translation() = Eigen::Vector3d(pose.position[0], pose.position[1], pose.position[2]);
    return result;
}
} // namespace ultimate_source_contract
