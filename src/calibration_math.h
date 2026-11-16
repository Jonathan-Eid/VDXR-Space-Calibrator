// SPDX-License-Identifier: MIT
#pragma once
#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <Eigen/SVD>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <string>

namespace calibration {
using Pose = Eigen::Isometry3d;
struct Pair { Pose source, target; };
struct Metrics {
    size_t count{};
    double squaredPosition{}, squaredAngle{}, maxPosition{}, maxAngle{};
    void add(const Pair& pair, const Pose& world, const Pose& mount) {
        const Pose predicted = world * pair.source * mount;
        const double p = (predicted.translation()-pair.target.translation()).norm();
        const double a = Eigen::AngleAxisd(predicted.rotation().transpose()*pair.target.rotation()).angle()*180.0/3.141592653589793;
        if (!std::isfinite(p) || !std::isfinite(a)) throw std::runtime_error("Invalid pose data");
        ++count; squaredPosition+=p*p; squaredAngle+=a*a;
        maxPosition=std::max(maxPosition,p); maxAngle=std::max(maxAngle,a);
    }
    double positionRms() const { return count ? std::sqrt(squaredPosition/count) : 0; }
    double angleRms() const { return count ? std::sqrt(squaredAngle/count) : 0; }
    bool passes() const { return count>=20 && positionRms()<=0.03 && maxPosition<=0.06 && maxAngle<=10; }
};
struct Fit { Pose world{Pose::Identity()}, mount{Pose::Identity()}; Metrics reserved; };
struct FitDiagnostics { double rotationSecond{},rotationLast{},rotationAxisRatio{},translationSmallest{},translationCondition{};size_t rotationDeltaCount{};Metrics reserved; };
inline Eigen::Matrix3d rotation(const Eigen::Matrix3d& matrix) {
    Eigen::JacobiSVD<Eigen::Matrix3d> svd(matrix,Eigen::ComputeFullU|Eigen::ComputeFullV);
    Eigen::Matrix3d sign=Eigen::Matrix3d::Identity();
    sign(2,2)=(svd.matrixU()*svd.matrixV().transpose()).determinant();
    return svd.matrixU()*sign*svd.matrixV().transpose();
}
inline Fit fit(const std::vector<Pair>& pairs,FitDiagnostics* diagnostics=nullptr) {
    if(pairs.size()<100) throw std::runtime_error("Collect more valid samples before fitting");
    std::vector<Pair> train;
    for(size_t i=0;i<pairs.size();++i) if(i%5) train.push_back(pairs[i]);
    std::vector<Pair> sampled;
    const size_t count=std::min<size_t>(120,train.size());
    for(size_t i=0;i<count;++i) sampled.push_back(train[i*(train.size()-1)/(count-1)]);
    std::vector<std::pair<Pose,Pose>> relative;
    for(size_t step : {size_t(1),std::max<size_t>(2,count/3)})
        for(size_t i=step;i<count;++i)
            relative.emplace_back(sampled[i-step].source.inverse()*sampled[i].source,
                                  sampled[i-step].target.inverse()*sampled[i].target);
    Eigen::MatrixXd equations(relative.size()*9,9);
    Eigen::MatrixXd lhs(relative.size()*3,3);
    for(size_t n=0;n<relative.size();++n) {
        const auto ar=relative[n].first.rotation();
        const auto br=relative[n].second.rotation();
        for(int row=0;row<9;++row) for(int col=0;col<9;++col) {
            const int ri=row%3, rj=row/3, ci=col%3, cj=col/3;
            equations(int(n)*9+row,col)=(rj==cj ? ar(ri,ci) : 0)-(ri==ci ? br(cj,rj) : 0);
        }
        lhs.block<3,3>(int(n)*3,0)=ar-Eigen::Matrix3d::Identity();
    }
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(equations,Eigen::ComputeThinV);
    const auto singular=svd.singularValues();
    if(diagnostics){diagnostics->rotationSecond=singular[7];diagnostics->rotationLast=singular[8];}
    if(singular[7]<0.05)throw std::runtime_error("Not enough independent rotation to solve the mounting offset (excitation "+std::to_string(singular[7])+")");
    if(singular[8]>0.5*singular[7])throw std::runtime_error("Tracker/controller rotations disagree (consistency ratio "+std::to_string(singular[8]/singular[7])+", limit 0.5). See capture diagnostics.");
    Eigen::Matrix3d mr=Eigen::Map<const Eigen::Matrix<double,3,3,Eigen::ColMajor>>(svd.matrixV().col(8).data());
    if(mr.determinant()<0) mr=-mr;
    mr=rotation(mr);
    Eigen::VectorXd rhs(relative.size()*3);
    for(size_t n=0;n<relative.size();++n)
        rhs.segment<3>(int(n)*3)=mr*relative[n].second.translation()-relative[n].first.translation();
    Eigen::JacobiSVD<Eigen::MatrixXd> translationSolver(lhs,Eigen::ComputeThinU|Eigen::ComputeThinV);
    if(diagnostics)diagnostics->translationSmallest=translationSolver.singularValues()[2];
    if(translationSolver.singularValues()[2]<0.1)
        throw std::runtime_error("Rotate around more than one axis to measure the mounting offset");
    Fit result;
    result.mount.linear()=mr;
    result.mount.translation()=translationSolver.solve(rhs);
    Eigen::Matrix3d rotations=Eigen::Matrix3d::Zero();
    Eigen::Vector3d translations=Eigen::Vector3d::Zero();
    for(const auto& pair:sampled) {
        const Pose world=pair.target*result.mount.inverse()*pair.source.inverse();
        rotations+=world.rotation(); translations+=world.translation();
    }
    result.world.linear()=rotation(rotations/double(count));
    result.world.translation()=translations/double(count);
    for(size_t i=0;i<pairs.size();i+=5) result.reserved.add(pairs[i],result.world,result.mount);
    if(diagnostics)diagnostics->reserved=result.reserved;
    if(!result.reserved.passes()) throw std::runtime_error("Alignment inconsistent: holdout position RMS "+std::to_string(result.reserved.positionRms()*1000)+" mm, max "+std::to_string(result.reserved.maxPosition*1000)+" mm; angle max "+std::to_string(result.reserved.maxAngle)+" deg. See capture diagnostics.");
    return result;
}
} // namespace calibration
