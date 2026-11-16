// SPDX-License-Identifier: MIT
// Read-only offline replay; never writes an accepted profile or bypasses fitting gates.
#include "spacecal_regular.h"
#include <cJSON.h>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
static std::vector<std::string> split(const std::string& row){std::vector<std::string> out;std::istringstream input(row);std::string value;while(std::getline(input,value,','))out.push_back(value);return out;}
int main(int argc,char** argv){
    calibration::FitDiagnostics diagnostics;std::vector<calibration::Pair> pairs;bool accepted=false;std::string error;
    try {
        if(argc<2||argc>4)throw std::runtime_error("Usage: calibration-replay CAPTURE.csv [selected|current|other] [spacecal|handeye]");
        const std::string mode=argc>=3 ? argv[2]:"selected";const std::string prefix=mode=="selected" ? "t":mode=="current" ? "c":mode=="other" ? "o":"";if(prefix.empty())throw std::runtime_error("Unknown target mode");
        std::ifstream file(argv[1]);if(!file)throw std::runtime_error("Capture file not found");std::string line;std::getline(file,line);const auto header=split(line);std::map<std::string,size_t> columns;for(size_t i=0;i<header.size();++i){auto name=header[i];if(!name.empty()&&name.back()=='\r')name.pop_back();columns[name]=i;}
        while(std::getline(file,line)){
            if(line.empty())continue;const auto cells=split(line);
            const auto number=[&](const std::string& key){const auto col=columns.find(key);if(col==columns.end()||col->second>=cells.size())throw std::runtime_error("Missing column: "+key);const double value=std::stod(cells[col->second]);if(!std::isfinite(value))throw std::runtime_error("Nonfinite capture value");return value;};
            if(mode=="other"&&number("other_flags")!=15)continue;
            const auto pose=[&](const std::string& p){calibration::Pose value=calibration::Pose::Identity();Eigen::Quaterniond q(number(p+"qw"),number(p+"qx"),number(p+"qy"),number(p+"qz"));if(std::abs(q.norm()-1)>.01)throw std::runtime_error("Invalid capture quaternion");value.linear()=q.normalized().toRotationMatrix();value.translation()=Eigen::Vector3d(number(p+"x"),number(p+"y"),number(p+"z"));return value;};
            pairs.push_back({pose("s"),pose(prefix)});
        }
        const std::string solver=argc==4 ? argv[3]:"spacecal";if(solver=="spacecal")calibration::fitSpaceCalibrator(pairs,&diagnostics);else if(solver=="handeye")calibration::fit(pairs,&diagnostics);else throw std::runtime_error("Unknown solver");accepted=true;
    }catch(const std::exception& failure){error=failure.what();}
    cJSON* root=cJSON_CreateObject();cJSON_AddBoolToObject(root,"accepted",accepted);cJSON_AddNumberToObject(root,"samples",double(pairs.size()));cJSON_AddStringToObject(root,"error",error.c_str());cJSON_AddNumberToObject(root,"rotation_excitation",diagnostics.rotationSecond);cJSON_AddNumberToObject(root,"rotation_consistency_ratio",diagnostics.rotationSecond>0 ? diagnostics.rotationLast/diagnostics.rotationSecond:0);cJSON_AddNumberToObject(root,"rotation_axis_ratio",diagnostics.rotationAxisRatio);cJSON_AddNumberToObject(root,"rotation_delta_count",double(diagnostics.rotationDeltaCount));cJSON_AddNumberToObject(root,"translation_condition",diagnostics.translationCondition);cJSON_AddNumberToObject(root,"translation_excitation",diagnostics.translationSmallest);cJSON_AddNumberToObject(root,"holdout_samples",double(diagnostics.reserved.count));cJSON_AddNumberToObject(root,"holdout_position_rms_m",diagnostics.reserved.positionRms());cJSON_AddNumberToObject(root,"holdout_angle_max_degrees",diagnostics.reserved.maxAngle);char* json=cJSON_Print(root);std::cout<<(json ? json:"{}")<<'\n';cJSON_free(json);cJSON_Delete(root);return accepted ? 0:1;
}
