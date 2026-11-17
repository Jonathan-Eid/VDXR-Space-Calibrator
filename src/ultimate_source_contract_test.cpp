// SPDX-License-Identifier: MIT
#include "ultimate_source_contract.h"
#include <iostream>
#include <stdexcept>

static void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() try {
    ultimate_hid::Pose p{}; p.device_id = {0x01,0x23,0x45,0x67,0x89,0xab};
    p.flags=0x12; p.received_qpc=100; p.position[0]=1.25f; p.orientation_xyzw[3]=1;
    double age{};
    check(ultimate_source_contract::deviceKey(p)=="rf:0123456789ab", "stable device key mismatch");
    check(ultimate_source_contract::validate(p,110,1000,age) && age==0.01, "full pose/source status 0x12 rejected");
    p.flags=0x13; check(!ultimate_source_contract::validate(p,110,1000,age), "non-full pose accepted");
    p.flags=0x12; check(!ultimate_source_contract::validate(p,99,1000,age), "future pose accepted");
    check(!ultimate_source_contract::validate(p,201,1000,age), "stale pose accepted");
    p.received_qpc=100; const auto pose=ultimate_source_contract::toCalibrationPose(p);
    check((pose.translation()-Eigen::Vector3d(1.25,0,0)).norm()<1e-9, "position basis conversion changed values");
    check((pose.rotation()-Eigen::Matrix3d::Identity()).norm()<1e-9, "quaternion XYZW conversion incorrect");
    std::cout << "Passed Ultimate source identity, status, freshness, and raw pose conversion fixtures\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
