// SPDX-License-Identifier: MIT
#include "calibration_math.h"
#include <iostream>
#include <random>
int main() try {
    using namespace calibration;
    std::mt19937 random(42); std::normal_distribution<double> normal;
    const auto pose=[&]() {
        Pose p=Pose::Identity(); Eigen::Quaterniond q(normal(random),normal(random),normal(random),normal(random));
        p.linear()=q.normalized().toRotationMatrix();
        p.translation()=Eigen::Vector3d(normal(random),normal(random),normal(random)); return p;
    };
    const Pose world=pose(), mount=pose();
    std::vector<Pair> pairs;
    for(int i=0;i<200;++i) { const Pose source=pose(); pairs.push_back({source,world*source*mount}); }
    const auto result=fit(pairs);
    if((result.world.matrix()-world.matrix()).norm()>1e-8 || (result.mount.matrix()-mount.matrix()).norm()>1e-8)
        throw std::runtime_error("Known room/mount transform not recovered");
    bool rejected=false;
    try { fit(std::vector<Pair>(200,{Pose::Identity(),Pose::Identity()})); }
    catch(const std::exception&) { rejected=true; }
    if(!rejected) throw std::runtime_error("Stationary input accepted");
    std::cout<<"Passed native rigid-pair fit, holdout metrics and degenerate rejection\n"; return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
