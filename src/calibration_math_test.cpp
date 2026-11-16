// SPDX-License-Identifier: MIT
#include "spacecal_regular.h"
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
    for(int i=0;i<100;++i) { const Pose source=pose(); pairs.push_back({source,world*source*mount}); }
    const auto result=fitSpaceCalibrator(pairs);
    if((result.world.matrix()-world.matrix()).norm()>1e-8 || (result.mount.matrix()-mount.matrix()).norm()>1e-8)
        throw std::runtime_error("Known room/mount transform not recovered");
    bool rejected=false;
    try { fitSpaceCalibrator(std::vector<Pair>(100,{Pose::Identity(),Pose::Identity()})); }
    catch(const std::exception&) { rejected=true; }
    if(!rejected) throw std::runtime_error("Stationary input accepted");
    // A smooth, short hand movement with realistic small measurement noise.
    // Independent random exact poses alone do not exercise this capture regime.
    pairs.clear();const Pose shortMount=[](){Pose p=Pose::Identity();p.translation()=Eigen::Vector3d(.04,.03,.06);p.linear()=Eigen::AngleAxisd(.7,Eigen::Vector3d(1,2,3).normalized()).toRotationMatrix();return p;}();
    for(int i=0;i<100;++i){const double t=i*.05;Pose source=Pose::Identity();source.translation()=Eigen::Vector3d(.3*std::sin(t),.2*std::cos(1.3*t),.25*std::sin(.8*t));source.linear()=(Eigen::AngleAxisd(.9*std::sin(t),Eigen::Vector3d::UnitX())*Eigen::AngleAxisd(.8*std::cos(1.4*t),Eigen::Vector3d::UnitY())*Eigen::AngleAxisd(.6*std::sin(.7*t),Eigen::Vector3d::UnitZ())).toRotationMatrix();Pose target=world*source*shortMount;target.translation()+=Eigen::Vector3d(normal(random),normal(random),normal(random))*.001;target.linear()=target.rotation()*Eigen::AngleAxisd(.003*normal(random),Eigen::Vector3d(1,2,3).normalized()).toRotationMatrix();pairs.push_back({source,target});}
    FitDiagnostics diagnostics;const auto smooth=fitSpaceCalibrator(pairs,&diagnostics);
    if((smooth.world.translation()-world.translation()).norm()>.02||(smooth.mount.translation()-shortMount.translation()).norm()>.02||diagnostics.reserved.count!=20)throw std::runtime_error("Noisy smooth short capture failed recovery");
    std::cout<<"Passed native rigid-pair fit, holdout metrics and degenerate rejection\n"; return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
