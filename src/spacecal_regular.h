// SPDX-License-Identifier: MIT
// Adapted from Hyblocker OpenVR-SpaceCalibrator regular calibration:
// src/overlay/calibration.cpp at 797e464dae298a03217e1bbea640e03781d37b9b.
// Copyright (c) 2023-2026 Hyblocker and other contributors
// Copyright (c) 2020-2022 Justin Li and other contributors
// See licenses/SpaceCalibrator-MIT.txt. OpenVR types/UI/driver are not used.
#pragma once
#include "calibration_math.h"
namespace calibration {
inline Fit fitSpaceCalibrator(const std::vector<Pair>& pairs,FitDiagnostics* diagnostics=nullptr) {
    if(pairs.size()<100)throw std::runtime_error("Collect at least 100 valid paired poses");
    std::vector<Pair> train;for(size_t i=0;i<pairs.size();++i)if(i%5)train.push_back(pairs[i]);
    Eigen::Matrix3d cross=Eigen::Matrix3d::Zero();size_t deltas=0;
    // Upstream uses world-space rotation delta axes and Kabsch, rather than
    // solving an unknown local mounting rotation first.
    for(size_t i=0;i<train.size();++i)for(size_t j=0;j<i;++j){
        const Eigen::AngleAxisd reference(train[i].target.rotation()*train[j].target.rotation().transpose());
        const Eigen::AngleAxisd tracker(train[i].source.rotation()*train[j].source.rotation().transpose());
        if(reference.angle()>.4&&tracker.angle()>.4){cross+=reference.axis()*tracker.axis().transpose();++deltas;}
    }
    if(diagnostics)diagnostics->rotationDeltaCount=deltas;
    if(deltas<200)throw std::runtime_error("Space Calibrator: not enough distinct rotation pairs; rotate around several axes");
    Eigen::JacobiSVD<Eigen::Matrix3d> rotational(cross,Eigen::ComputeFullU|Eigen::ComputeFullV);
    const double axisRatio=rotational.singularValues()[1]/std::max(1e-12,rotational.singularValues()[0]);
    if(diagnostics)diagnostics->rotationAxisRatio=axisRatio;
    if(axisRatio<.15)throw std::runtime_error("Space Calibrator: insufficient rotation-axis variety (ratio "+std::to_string(axisRatio)+")");
    Eigen::Matrix3d sign=Eigen::Matrix3d::Identity();if((rotational.matrixU()*rotational.matrixV().transpose()).determinant()<0)sign(2,2)=-1;
    Fit result;result.world.linear()=rotational.matrixU()*sign*rotational.matrixV().transpose();
    const size_t rows=train.size()*(train.size()-1)*3;
    Eigen::MatrixXd coefficients(rows,3);Eigen::VectorXd constants(rows);size_t row=0;
    for(size_t i=0;i<train.size();++i)for(size_t j=0;j<i;++j){
        const Eigen::Vector3d di=train[i].target.translation()-result.world.rotation()*train[i].source.translation();
        const Eigen::Vector3d dj=train[j].target.translation()-result.world.rotation()*train[j].source.translation();
        const Eigen::Matrix3d ai=train[i].target.rotation().transpose(),aj=train[j].target.rotation().transpose();
        coefficients.block<3,3>(row,0)=aj-ai;constants.segment<3>(row)=aj*dj-ai*di;row+=3;
        const Eigen::Matrix3d bi=(result.world.rotation()*train[i].source.rotation()).transpose(),bj=(result.world.rotation()*train[j].source.rotation()).transpose();
        coefficients.block<3,3>(row,0)=bj-bi;constants.segment<3>(row)=bj*dj-bi*di;row+=3;
    }
    Eigen::JacobiSVD<Eigen::MatrixXd> translation(coefficients,Eigen::ComputeThinU|Eigen::ComputeThinV);
    const auto singular=translation.singularValues();const double condition=singular[0]/std::max(1e-12,singular[2]);
    if(diagnostics){diagnostics->translationSmallest=singular[2];diagnostics->translationCondition=condition;}
    if(singular[2]<.01||condition>3)throw std::runtime_error("Space Calibrator: poorly conditioned translation movement");
    result.world.translation()=translation.solve(constants);
    // The original app applies this world transform to the target devices.
    // Estimate the constant mounting transform only for our visualization and
    // held-out quality measurement; do not snap device origins together.
    Eigen::Matrix3d mountRot=Eigen::Matrix3d::Zero();Eigen::Vector3d mountPos=Eigen::Vector3d::Zero();
    for(const auto& pair:train){const Pose mount=pair.source.inverse()*result.world.inverse()*pair.target;mountRot+=mount.rotation();mountPos+=mount.translation();}
    result.mount.linear()=rotation(mountRot/double(train.size()));result.mount.translation()=mountPos/double(train.size());
    for(size_t i=0;i<pairs.size();i+=5)result.reserved.add(pairs[i],result.world,result.mount);
    if(diagnostics)diagnostics->reserved=result.reserved;
    if(!result.reserved.passes())throw std::runtime_error("Space Calibrator alignment inconsistent: reserved position RMS "+std::to_string(result.reserved.positionRms()*1000)+" mm; max angle "+std::to_string(result.reserved.maxAngle)+" degrees");
    return result;
}
}
