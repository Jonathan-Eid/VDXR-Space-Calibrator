// SPDX-License-Identifier: MIT
#include "device_models.h"
#include <iostream>
static void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(){try{
    using namespace device_models;
    const std::string generic="/interaction_profiles/oculus/touch_controller";
    const auto q3=controller("Meta Quest 3",generic,"auto");require(q3.model=="meta-quest-touch-plus"&&q3.source.find("headset default")==0,"Quest 3 fallback must be labeled as a headset default");
    require(controller("Meta Quest 2",generic,"auto").model=="oculus-touch-v3","Quest 2 default model");
    require(controller("Meta Quest 3", "/interaction_profiles/facebook/touch_controller_pro","auto").model=="meta-quest-touch-pro","Exact profile must take priority over headset");
    require(controller("Unknown",generic,"auto").model=="generic","Unknown headset must remain generic");
    require(controller("Meta Quest 3","/interaction_profiles/valve/index_controller","auto").model=="generic","Do not identify unrelated profiles from headset");
    require(controller("Meta Quest 3",generic,"oculus-touch-v3").source=="manual override","Override must be labeled");
    require(!controllerChoice("../../evil"),"Reject unknown model/path");
    require(tracker("{\"model_number\":\"VIVE Tracker 3.0 MV\"}").model=="vive-tracker-3","Factory tracker model");
    require(tracker("{\"render_model\":\"{htc}vr_tracker_vive_3_0\"}").model=="vive-tracker-3","Factory render-model ID");
    require(tracker("{\"serial_number\":\"LHR-EXAMPLE\",\"model_number\":\"VIVE Tracker 2.0\"}").model=="generic","Serial prefix does not identify tracker generation");
    require(tracker("invalid").model=="generic","Bad metadata remains unknown");
    std::cout<<"Passed device identity precedence, uncertainty, overrides and unknown-device handling\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
