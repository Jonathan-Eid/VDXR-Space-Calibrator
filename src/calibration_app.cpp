// SPDX-License-Identifier: MIT
// Standalone regular calibration UI. Does not use OpenVR or launch SteamVR.
#include "calibration_xr.h"
#include "calibration_scene.h"
#include "calibration_inspection.h"
#include "device_models.h"
#include "spacecal_regular.h"
#include "vdxr_tracker_ipc.h"
#define SURVIVE_ENABLE_FULL_API 1
#define SURVIVE_IMPORT __declspec(dllimport)
#include <libsurvive/survive_api.h>
#include <libsurvive/survive.h>
#undef float_format
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <map>
#include <chrono>
#include <sstream>

using namespace calibration;
using namespace scene;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
static constexpr size_t FastCaptureSamples=100,SlowCaptureSamples=250,ValidationSamples=50;
static constexpr double SampleInterval=.05;
static bool exitRequested=false,mouseDown=false;
static ULONGLONG mouseActiveUntil=0;
static float mouseX=-1000,mouseY=-1000;
static std::string startupError;
static bool boundedRun=false;
static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_CLOSE){exitRequested=true;return 0;}
    if(message==WM_MOUSEMOVE){mouseX=float(short(LOWORD(l)));mouseY=float(short(HIWORD(l)));mouseActiveUntil=GetTickCount64()+1500;return 0;}
    if(message==WM_LBUTTONDOWN){mouseDown=true;mouseActiveUntil=GetTickCount64()+1500;SetCapture(window);return 0;}
    if(message==WM_LBUTTONUP){mouseDown=false;mouseActiveUntil=GetTickCount64()+1500;ReleaseCapture();return 0;}
    if(message==WM_KEYDOWN&&w==VK_ESCAPE){exitRequested=true;return 0;}
    if(message==WM_PAINT&&!startupError.empty()){
        PAINTSTRUCT paint;HDC dc=BeginPaint(window,&paint);RECT bounds;GetClientRect(window,&bounds);FillRect(dc,&bounds,GetSysColorBrush(COLOR_WINDOW));
        SetBkMode(dc,TRANSPARENT);bounds.left+=32;bounds.top+=40;bounds.right-=32;bounds.bottom-=32;
        HFONT font=CreateFontW(24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");const auto old=SelectObject(dc,font);
        const std::string text="VDXR Tracker Calibration\n\n"+startupError+"\n\nConnect the Quest through Virtual Desktop, then reopen this app.\nYour calibration files have not been changed.\n\nClose this window to exit.";
        DrawTextA(dc,text.c_str(),-1,&bounds,DT_LEFT|DT_WORDBREAK);SelectObject(dc,old);DeleteObject(font);EndPaint(window,&paint);return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
static std::string number(double v,int precision=1) { std::ostringstream s;s<<std::fixed<<std::setprecision(precision)<<v;return s.str(); }
static std::string poseText(const Pose& pose) {
    const Eigen::Quaterniond q(pose.rotation());std::ostringstream s;s<<std::setprecision(12)<<pose.translation().x()<<" "<<pose.translation().y()<<" "<<pose.translation().z()<<" "<<q.x()<<" "<<q.y()<<" "<<q.z()<<" "<<q.w()<<"\n";return s.str();
}
static void atomicWrite(const fs::path& path,const std::string& contents) {
    const auto temp=path.wstring()+L".tmp";{std::ofstream file(temp,std::ios::binary);if(!file)throw std::runtime_error("Cannot save profile");file<<contents;if(!file)throw std::runtime_error("Profile write failed");}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot finish profile save");
}
struct RawPose { Pose pose{Pose::Identity()}; device_models::Identity identity; double sampleTime{},age{}; Clock::time_point received{}; bool fresh() const { return sampleTime>0 && std::chrono::duration<double>(Clock::now()-received).count()+age<.1; } };
enum class Step { Devices,Capture,Fitting,Validate,Ready,Saved,Error };
struct Model {
    SurviveSimpleContext* source{};
    std::future<SurviveSimpleContext*> hardware;
    std::future<Fit> pendingFit;
    std::map<std::string,RawPose> devices;
    std::string selected,message="Connecting to tracker receivers...";
    Step step=Step::Devices;int reference=0,role=1;
    bool preview=false,mapLocked=false,hasFit=false,validationRunning=false,service=false;
    bool roomSolverDisabled=false;
    double captureSeconds{},validationSeconds{},instantPosition{},instantAngle{},lastSample{};
    std::vector<Pair> samples;
    std::vector<std::string> captureTrace;
    std::string captureFile,captureExportError;
    Metrics validation;
    Eigen::Vector3d validationMin{Eigen::Vector3d::Zero()},validationMax{Eigen::Vector3d::Zero()};
    Eigen::Matrix3d validationFirst{Eigen::Matrix3d::Identity()};
    double validationRotation{};
    Fit result;
    Located head,grips[2],aims[2];
    device_models::Library models;
    device_models::Identity controllerIdentity[2];
    std::string controllerOverride[2]{"auto","auto"},systemName,interactionProfiles[2];
    bool modelSettings=false;
    bool fastMode=true;
    size_t captureTarget()const{return fastMode ? FastCaptureSamples:SlowCaptureSamples;}
    Clock::time_point inspectionStarted=Clock::now();
    std::string controllerKey(int side)const{return controllerIdentity[side].model+(side ? "-right":"-left");}
    void updateIdentities(){for(int side=0;side<2;++side)controllerIdentity[side]=device_models::controller(systemName,interactionProfiles[side],controllerOverride[side]);}
    Pose panelPose{Pose::Identity()},rawAnchor{Pose::Identity()},rawOrigin{Pose::Identity()};
    bool placed=false,rawPlaced=false;
    Clock::time_point lastTick=Clock::now(),lastPair=Clock::now();
    HANDLE owner{},ipcMutex{},ipcMapping{};void* ipcView{};
    const char* phase() const { switch(step){case Step::Devices:return "devices";case Step::Capture:return "capture";case Step::Fitting:return "fitting";case Step::Validate:return "validate";case Step::Ready:return "ready_to_save";case Step::Saved:return "saved";default:return "error";} }
    bool busy() const { return step==Step::Capture||step==Step::Fitting||validationRunning||service; }
    bool can(const std::string& action) const {
        if(action=="exit")return true;
        if(action=="place_panel")return !service;
        if(action=="show_device_models")return true;
        if(action=="set_fast_mode")return !busy();
        if(action=="select_left_model"||action=="select_right_model")return !busy();
        if(action=="select_reference"||action=="select_tracker"||action=="select_role"||action=="reset")return !busy();
        if(action=="start_capture")return !preview&&!busy()&&mapLocked&&grips[reference].tracked()&&devices.count(selected)&&devices.at(selected).fresh();
        if(action=="fit")return step==Step::Capture;
        if(action=="start_validation")return !preview&&hasFit&&!busy();
        if(action=="stop_validation")return validationRunning;
        if(action=="save_profile")return !preview&&step==Step::Ready;
        if(action=="load_profile")return !preview&&!busy()&&fs::exists("data/profile.json");
        if(action=="start_service")return !preview&&!service&&step==Step::Saved;
        if(action=="lock_map")return !preview&&!busy()&&!mapLocked&&source&&!hardware.valid()&&devices.count(selected)&&devices.at(selected).fresh();
        if(action=="identify_reference")return !preview&&!service;
        return false;
    }
    bool perform(const std::string& action,XrApp* xr,int index=0,const std::string& text="") {
        if(!can(action))return false;
        if(action=="select_reference"){if(index<0||index>1)return false;reference=index;reset();}
        else if(action=="show_device_models"){if(index<0||index>1)return false;modelSettings=index!=0;}
        else if(action=="set_fast_mode"){if(index<0||index>1)return false;atomicWrite(L"data/calibration-settings.json",index ? "{\"fast_mode\":true}":"{\"fast_mode\":false}");fastMode=index!=0;}
        else if(action=="select_left_model"||action=="select_right_model"){
            if(!device_models::controllerChoice(text))return false;controllerOverride[action=="select_right_model" ? 1:0]=text;updateIdentities();
            std::ostringstream json;json<<"{\"left\":\""<<controllerOverride[0]<<"\",\"right\":\""<<controllerOverride[1]<<"\"}";atomicWrite(L"data/device-models.json",json.str());
        }
        else if(action=="select_tracker"){if(!devices.count(text))return false;selected=text;rawPlaced=false;reset();}
        else if(action=="select_role"){if(index<0||index>=14)return false;role=index;}
        else if(action=="start_capture")startCapture();
        else if(action=="fit")fitCapture();
        else if(action=="start_validation"){validation={};validationSeconds=0;validationRunning=true;step=Step::Validate;}
        else if(action=="stop_validation")validationRunning=false;
        else if(action=="save_profile")save();
        else if(action=="load_profile")load();
        else if(action=="start_service")startService();
        else if(action=="reset")reset();
        else if(action=="identify_reference"){if(!xr)return false;xr->identify(reference);}
        else if(action=="lock_map"){auto* previous=source;source=nullptr;devices.clear();initHardware(true,previous);}
        else if(action=="exit")exitRequested=true;
        else if(action=="place_panel"){placed=false;rawPlaced=false;}
        return true;
    }
    ~Model() {
        if(pendingFit.valid()) pendingFit.wait();
        if(hardware.valid()) { try { auto* value=hardware.get();if(value)survive_simple_close(value); }catch(...){} }
        if(source)survive_simple_close(source);
        if(ipcView)UnmapViewOfFile(ipcView);if(ipcMapping)CloseHandle(ipcMapping);if(ipcMutex)CloseHandle(ipcMutex);if(owner)CloseHandle(owner);
    }
    void initHardware(bool lockMap,SurviveSimpleContext* previous=nullptr) {
        mapLocked=lockMap;
        hardware=std::async(std::launch::async,[lockMap,previous]() {
            if(previous)survive_simple_close(previous);
            char program[]="vdxr-calibration",flag[]="--configfile",path[]="./data/lighthouse.json",disable[]="--disable-calibrate",enabled[]="1",disabled[]="0";char* args[]{program,flag,path,disable,lockMap ? enabled:disabled};
            auto* context=survive_simple_init(5,args);if(!context)throw std::runtime_error("Tracker initialization failed");survive_simple_start_thread(context);return context;
        });
    }
    void pollHardware() {
        if(hardware.valid()&&hardware.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try { source=hardware.get();roomSolverDisabled=survive_configi(survive_simple_get_ctx(source),"disable-calibrate",SC_GET,0)!=0;if(mapLocked&&!roomSolverDisabled)throw std::runtime_error("Room map lock was not honored by libsurvive");message=mapLocked ? "Lighthouse map loaded and scene solver disabled. Select the device pair.":"Place the tracker stationary and visible, then lock its Lighthouse map."; }
            catch(const std::exception& error){message=error.what();step=Step::Error;}
        }
        if(!source)return;
        for(int i=0;i<64;++i) {
            const auto* object=survive_simple_get_next_updated(source);if(!object)break;
            if(survive_simple_object_get_type(object)!=SurviveSimpleObject_OBJECT)continue;
            const char* serial=survive_simple_serial_number(object);if(!serial||!serial[0])continue;
            auto& raw=devices[serial];SurvivePose pose;
            if(raw.identity.source=="unavailable")raw.identity=device_models::tracker(survive_simple_json_config(object));
            const double time=survive_simple_object_get_latest_pose(object,&pose);
            const double age=survive_simple_run_time_since_epoch(source)-time;
            Eigen::Quaterniond q(pose.Rot[0],pose.Rot[1],pose.Rot[2],pose.Rot[3]);
            const Eigen::Vector3d p(pose.Pos[0],pose.Pos[1],pose.Pos[2]);
            if(time<=0||!std::isfinite(age)||age<-.005||age>.1||!p.allFinite()||!q.coeffs().allFinite()||std::abs(q.norm()-1)>.01)continue;
            raw.pose=Pose::Identity();raw.pose.linear()=q.normalized().toRotationMatrix();raw.pose.translation()=p;raw.sampleTime=time;raw.age=std::max(0.0,age);raw.received=Clock::now();
            if(selected.empty())selected=serial;
        }
    }
    RawPose* raw() { const auto it=devices.find(selected);return it==devices.end() ? nullptr:&it->second; }
    void reset() { hasFit=false;validationRunning=false;samples.clear();captureTrace.clear();captureSeconds=validationSeconds=0;lastSample=0;validation={};step=Step::Devices;message="Select devices, attach them rigidly, then record."; }
    void startCapture() {
        if(preview){message="Preview contains simulated devices; hardware calibration is disabled.";return;}
        if(!mapLocked){message="Lock a stable Lighthouse map before recording.";return;}
        if(!raw()||!raw()->fresh()||!grips[reference].tracked()){message="Both selected devices must provide live poses.";return;}
        reset();step=Step::Capture;lastPair=Clock::now();message="Keep the pair rigid. Move slowly and rotate around several axes.";
    }
    void fitCapture() {
        try {
            fs::create_directories("data/captures");captureFile="data/captures/"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".csv";
            std::ostringstream csv;csv<<"sx,sy,sz,sqx,sqy,sqz,sqw,tx,ty,tz,tqx,tqy,tqz,tqw,cx,cy,cz,cqx,cqy,cqz,cqw,ox,oy,oz,oqx,oqy,oqz,oqw,clock_seconds,source_time_seconds,source_age_seconds,target_flags,other_flags,reference_side\n";for(const auto& row:captureTrace)csv<<row<<'\n';atomicWrite(fs::path(captureFile).wstring().c_str(),csv.str());captureExportError.clear();
        }catch(const std::exception& error){captureExportError=error.what();captureFile.clear();}
        if(samples.size()<100){step=Step::Error;message="Too few valid samples. Record again with both devices tracked.";return;}
        const auto copy=samples;step=Step::Fitting;message="Checking movement and fitting room + mounting transforms...";
        pendingFit=std::async(std::launch::async,[copy](){return calibration::fitSpaceCalibrator(copy);});
    }
    void tick(XrApp* xr,XrTime time) {
        pollHardware();const auto now=Clock::now();const double delta=std::min(.05,std::chrono::duration<double>(now-lastTick).count());lastTick=now;
        if(pendingFit.valid()&&pendingFit.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try { result=pendingFit.get();hasFit=true;step=Step::Ready;message="Calibrated. The tracker is now displayed in Quest space.";try{save();message="Calibrated and saved. The tracker is now displayed in Quest space.";}catch(const std::exception& error){message=std::string("Alignment applied, but saving failed: ")+error.what();} }
            catch(const std::exception& error){hasFit=false;step=Step::Error;message=error.what();}
        }
        auto* r=raw();bool valid=r&&r->fresh()&&grips[reference].tracked()&&(!xr||xr->focused);
        if(valid&&xr) {
            const double age=r->age+std::chrono::duration<double>(now-r->received).count();
            const auto target=xr->grip(reference,time-XrTime(age*1e9));valid=target.tracked();
            if(valid) {
                const Pair pair{r->pose,target.pose};
                if(hasFit){Metrics metric;metric.add(pair,result.world,result.mount);instantPosition=metric.positionRms();instantAngle=metric.angleRms();}
                if(r->sampleTime!=lastSample && std::chrono::duration<double>(now-lastPair).count()>=SampleInterval) {
                    const double pairDelta=std::min(.05,std::chrono::duration<double>(now-lastPair).count());
                    lastSample=r->sampleTime;lastPair=now;
                    if(step==Step::Capture){
                        samples.push_back(pair);captureSeconds+=pairDelta;
                        const auto other=xr->grip(1-reference,time-XrTime(age*1e9));std::ostringstream row;row<<std::setprecision(17);
                        const auto append=[&](const Pose& p){const Eigen::Quaterniond q(p.rotation());row<<p.translation().x()<<','<<p.translation().y()<<','<<p.translation().z()<<','<<q.x()<<','<<q.y()<<','<<q.z()<<','<<q.w()<<',';};
                        append(pair.source);append(pair.target);append(grips[reference].pose);append(other.pose);row<<std::chrono::duration<double>(now.time_since_epoch()).count()<<','<<r->sampleTime<<','<<age<<','<<target.flags<<','<<other.flags<<','<<reference;captureTrace.push_back(row.str());
                    }
                    if(validationRunning){
                        if(validation.count==0){validationMin=validationMax=pair.target.translation();validationFirst=pair.target.rotation();validationRotation=0;}
                        validationMin=validationMin.cwiseMin(pair.target.translation());validationMax=validationMax.cwiseMax(pair.target.translation());
                        validationRotation=std::max(validationRotation,Eigen::AngleAxisd(validationFirst.transpose()*pair.target.rotation()).angle());
                        validation.add(pair,result.world,result.mount);validationSeconds+=pairDelta;
                    }
                }
            }
        }
        if(step==Step::Capture) {
            message=valid ? "Recording. Keep the pair rigid; use several rotation axes.":"Recording paused: waiting for both devices to be tracked.";
            if(samples.size()>=captureTarget())fitCapture();
        }
        const bool motionCovered=(validationMax-validationMin).norm()>=.25&&validationRotation>=.6;
        if(validationRunning&&validation.count>=ValidationSamples&&!motionCovered)message="Validation needs broader movement: cover 25 cm and rotate around different directions.";
        if(validationRunning&&validation.count>=ValidationSamples&&(motionCovered||validationSeconds>=60)) {
            validationRunning=false;
            if(motionCovered&&validation.passes()){step=Step::Ready;message="Validation passed. Inspect the 3D overlay, then save the profile.";}
            else{step=Step::Validate;message="Validation failed. Inspect the gap, reattach the pair, and record again.";}
        }
        if(service)publish();
    }
    void save() {
        if(preview||step!=Step::Ready)return;
        atomicWrite(L"data/stage.txt",poseText(result.world));atomicWrite(L"data/mount.txt",poseText(result.mount));
        std::ostringstream json;json<<"{\n  \"kind\": \"regular\",\n  \"serial\": \""<<selected<<"\",\n  \"role\": \""<<external_trackers::Roles[role]<<"\",\n  \"fit_holdout_samples\": "<<result.reserved.count<<",\n  \"fit_holdout_position_rms_m\": "<<result.reserved.positionRms()<<",\n  \"fit_holdout_angle_rms_degrees\": "<<result.reserved.angleRms()<<",\n  \"validation_samples\": "<<validation.count<<",\n  \"position_rms_m\": "<<validation.positionRms()<<",\n  \"angle_rms_degrees\": "<<validation.angleRms()<<"\n}\n";
        atomicWrite(L"data/profile.json",json.str());step=Step::Saved;message="Profile saved locally. Recalibrate after recentering or moving base stations.";
    }
    void load() {
        if(preview)return;
        try {
            const auto read=[](const char* path){std::ifstream file(path);double x,y,z,qx,qy,qz,qw;if(!(file>>x>>y>>z>>qx>>qy>>qz>>qw))throw std::runtime_error("Saved transform is missing");Eigen::Quaterniond q(qw,qx,qy,qz);Eigen::Vector3d p(x,y,z);if(!p.allFinite()||!q.coeffs().allFinite()||std::abs(q.norm()-1)>.01)throw std::runtime_error("Saved transform is invalid");Pose value=Pose::Identity();value.linear()=q.normalized().toRotationMatrix();value.translation()=p;return value;};
            std::ifstream profile("data/profile.json");const std::string json((std::istreambuf_iterator<char>(profile)),{});
            const auto value=[&](const std::string& key){auto i=json.find("\""+key+"\"");if(i==std::string::npos)throw std::runtime_error("Profile metadata is incomplete");i=json.find(':',i);i=json.find('"',i);const auto end=json.find('"',i+1);if(i==std::string::npos||end==std::string::npos)throw std::runtime_error("Invalid profile metadata");return json.substr(i+1,end-i-1);};
            const auto serial=value("serial"),savedRole=value("role");int index=-1;for(int i=0;i<14;++i)if(savedRole==external_trackers::Roles[i])index=i;
            if(index<0||serial.empty()||serial.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos)throw std::runtime_error("Unknown profile device or role");
            result={};result.world=read("data/stage.txt");result.mount=read("data/mount.txt");selected=serial;role=index;hasFit=true;validation={};step=Step::Validate;message="Saved profile loaded. Validate it in the current room before enabling tracking.";
        }catch(const std::exception& error){message=error.what();}
    }
    void startService() {
        if(step!=Step::Saved)return;
        ipcMutex=CreateMutexW(nullptr,FALSE,external_trackers::MutexName);ipcMapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(external_trackers::State),external_trackers::MappingName);
        if(ipcMapping)ipcView=MapViewOfFile(ipcMapping,FILE_MAP_WRITE,0,0,sizeof(external_trackers::State));
        if(!ipcMutex||!ipcView){message="Cannot start tracker transport.";return;}
        service=true;message="Tracking service active. Keep this window open while playing.";
    }
    void publish() {
        external_trackers::State state;LARGE_INTEGER frequency,qpc;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&qpc);state.frequency=frequency.QuadPart;state.samples[role].configured=1;
        auto* r=raw();if(r&&r->fresh()) {
            const Pose p=result.world*r->pose;const Eigen::Quaterniond q(p.rotation());auto& sample=state.samples[role];
            const double age=r->age+std::chrono::duration<double>(Clock::now()-r->received).count();sample.qpc=qpc.QuadPart-int64_t(age*frequency.QuadPart);
            for(int i=0;i<3;++i)sample.position[i]=float(p.translation()[i]);sample.orientation[0]=float(q.x());sample.orientation[1]=float(q.y());sample.orientation[2]=float(q.z());sample.orientation[3]=float(q.w());
        }
        const auto status=WaitForSingleObject(ipcMutex,0);if(status==WAIT_OBJECT_0||status==WAIT_ABANDONED){std::memcpy(ipcView,&state,sizeof(state));ReleaseMutex(ipcMutex);}
    }
};

static void drawUi(Model& m,XrApp* xr) {
    const auto action=[&](const char* name,int index=0,const std::string& text=""){m.perform(name,xr,index,text);};
    ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({620,820});
    ImGui::Begin("VDXR Tracker Calibration",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("VDXR TRACKER CALIBRATION");ImGui::SameLine(490);if(ImGui::SmallButton("Exit"))action("exit");
    if(m.preview)ImGui::TextColored({.7f,.25f,.05f,1},"DESIGN PREVIEW - simulated devices");
    ImGui::TextWrapped("Regular calibration: Devices > Capture > Aligned and saved");ImGui::Separator();
    ImGui::Text("Quest: %s",m.service?"VR session closed; tracker service active":m.head.tracked()?"Tracked":"Waiting");
    ImGui::SameLine(390);ImGui::BeginDisabled(m.service);if(ImGui::SmallButton("Bring panel here"))action("place_panel");ImGui::EndDisabled();
    ImGui::Text("Left controller: %s   Right: %s",m.grips[0].tracked()?"Tracked":"Not tracked",m.grips[1].tracked()?"Tracked":"Not tracked");
    if(ImGui::Button(m.modelSettings ? "Back to calibration":"Device models",{-1,36}))action("show_device_models",m.modelSettings ? 0:1);
    if(m.modelSettings) {
        const char* ids[]{"auto","meta-quest-touch-plus","oculus-touch-v3","meta-quest-touch-pro","generic"};
        const char* labels[]{"Automatic","Quest 3 / Touch Plus","Quest 2 / Touch","Touch Pro","Generic"};
        for(int side=0;side<2;++side){int choice=0;for(int i=0;i<5;++i)if(m.controllerOverride[side]==ids[i])choice=i;ImGui::PushID(side);ImGui::SetNextItemWidth(-1);if(ImGui::Combo(side ? "##right_model":"##left_model",&choice,labels,5))action(side ? "select_right_model":"select_left_model",0,ids[choice]);ImGui::TextWrapped("%s: %s",side ? "Right":"Left",m.controllerIdentity[side].name.c_str());ImGui::TextWrapped("Source: %s",m.controllerIdentity[side].source.c_str());if(!m.models.loaded(m.controllerKey(side)))ImGui::TextUnformatted("Model unavailable: generic marker shown");ImGui::PopID();}
        if(auto* tracker=m.raw()){ImGui::TextWrapped("Tracker: %s (%s)",tracker->identity.name.c_str(),tracker->identity.source.c_str());if(!m.models.loaded(tracker->identity.model))ImGui::TextUnformatted("Tracker model unavailable: generic marker shown");}
        ImGui::Spacing();ImGui::TextWrapped("Model choices affect appearance only; tracking and calibration transforms stay the same.");
        ImGui::End();return;
    }
    auto* raw=m.raw();ImGui::Text("Vive tracker: %s",raw&&raw->fresh()?"Pose received":"Waiting for poses");
    if(m.hasFit&&!m.validationRunning)ImGui::TextColored({.05f,.5f,.25f,1},"CALIBRATED - alignment applied");
    if(m.step==Step::Error)ImGui::TextColored({.75f,.12f,.08f,1},"CALIBRATION FAILED - see reason below");
    ImGui::TextWrapped("%s",m.message.c_str());ImGui::Spacing();ImGui::Separator();
    const bool busy=m.step==Step::Capture||m.step==Step::Fitting||m.validationRunning||m.service;
    const bool choosing=m.step==Step::Devices||m.step==Step::Error;
    if(choosing) {
    ImGui::BeginDisabled(busy);
    const char* controllers[]{"Left Quest controller","Right Quest controller"};int reference=m.reference;ImGui::SetNextItemWidth(-1);if(ImGui::Combo("##reference",&reference,controllers,2))action("select_reference",reference);
    ImGui::SetNextItemWidth(-1);if(ImGui::BeginCombo("##tracker",m.selected.empty()?"Choose Vive tracker":m.selected.c_str())) {
        for(const auto& entry:m.devices)if(ImGui::Selectable(entry.first.c_str(),m.selected==entry.first))action("select_tracker",0,entry.first);ImGui::EndCombo();
    }
    ImGui::TextUnformatted("Tracker role");ImGui::SetNextItemWidth(-1);const char* roles[14];for(int i=0;i<14;++i)roles[i]=external_trackers::Roles[i];int role=m.role;if(ImGui::Combo("##role",&role,roles,14))action("select_role",role);
    ImGui::EndDisabled();
    if(ImGui::Button("Identify reference controller",{-1,44}))action("identify_reference");
    if(!m.mapLocked&&!m.hardware.valid()&&m.source) {
        ImGui::TextWrapped("Keep the tracker stationary with clear base-station visibility before locking its map.");
        if(ImGui::Button("Lock Lighthouse map",{-1,44}))action("lock_map");
    }
    ImGui::Spacing();ImGui::TextWrapped("Attach the tracker firmly to the reference controller. Keep sensors uncovered. Move slowly through broad positions and rotations.");
    } else {ImGui::Text("Reference: %s controller",m.reference ? "Right":"Left");ImGui::Text("Tracker: %s",m.selected.c_str());}
    ImGui::BeginDisabled(!m.can("set_fast_mode"));bool fast=m.fastMode;if(ImGui::Checkbox("Fast mode",&fast))action("set_fast_mode",fast ? 1:0);ImGui::SameLine();ImGui::Text("%zu samples / ~%.1f seconds",m.captureTarget(),m.captureTarget()*SampleInterval);ImGui::EndDisabled();
    if(m.step==Step::Capture){ImGui::ProgressBar(float(m.samples.size())/m.captureTarget(),{-1,24});ImGui::Text("%zu / %zu samples (20 Hz)",m.samples.size(),m.captureTarget());if(ImGui::Button("Stop and fit",{-1,48}))action("fit");}
    else if(m.step==Step::Fitting)ImGui::TextUnformatted("Fitting and checking reserved samples...");
    else if(!m.hasFit) {ImGui::BeginDisabled(!m.can("start_capture"));if(ImGui::Button("Start regular calibration",{-1,48}))action("start_capture");ImGui::EndDisabled();}
    if(m.hasFit&&!m.service) {
        ImGui::Separator();ImGui::TextUnformatted("LIVE VALIDATION");ImGui::Text("Position gap: %.1f mm    Angle gap: %.1f deg",m.instantPosition*1000,m.instantAngle);
        ImGui::TextWrapped("Teal ghost predicts the controller from the tracker. It should stay over the solid controller while the pair is attached.");
    if(m.validationRunning){ImGui::ProgressBar(float(std::min(1.0,double(m.validation.count)/ValidationSamples)),{-1,24});ImGui::Text("%zu / %zu samples; %.0f cm motion",m.validation.count,ValidationSamples,(m.validationMax-m.validationMin).norm()*100);if(ImGui::Button("Stop validation",{-1,40}))action("stop_validation");}
        else if(!m.service&&ImGui::Button("Check alignment (optional)",{-1,44}))action("start_validation");
        ImGui::Text("Validation RMS: %.1f mm / %.1f deg",m.validation.positionRms()*1000,m.validation.angleRms());
    }
    ImGui::BeginDisabled(!m.can("save_profile"));if(ImGui::Button("Save profile",{278,44}))action("save_profile");ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(!m.can("reset"));if(ImGui::Button("Start over",{278,44}))action("reset");ImGui::EndDisabled();
    if(m.can("load_profile")&&ImGui::Button("Load saved profile",{-1,40}))action("load_profile");
    if(m.can("start_service")&&ImGui::Button("Start tracking service",{-1,44}))action("start_service");
    ImGui::Spacing();if(ImGui::Button("Exit app",{-1,44}))action("exit");
    ImGui::TextWrapped("Device models follow identity metadata; headset defaults and manual overrides are labeled. Unknown devices use generic markers. Before alignment, trackers appear in a separate raw-space preview. Residuals are not measured physical accuracy.");
    ImGui::End();
}

static DirectX::XMMATRIX projection(const XrFovf& fov) {
    constexpr float nearPlane=.05f;return DirectX::XMMatrixPerspectiveOffCenterRH(std::tan(fov.angleLeft)*nearPlane,std::tan(fov.angleRight)*nearPlane,std::tan(fov.angleDown)*nearPlane,std::tan(fov.angleUp)*nearPlane,nearPlane,50);
}
static void geometry(Model& m,std::vector<Vertex>& triangles,std::vector<Vertex>& lines) {
    for(int i=-16;i<=16;++i){const double x=i*.25;line(lines,{x,0,-4},{x,0,4},{.48f,.58f,.63f,1});line(lines,{-4,0,x},{4,0,x},{.48f,.58f,.63f,1});}
    axes(lines,Pose::Identity(),.3);
    for(int side=0;side<2;++side)if(m.grips[side].tracked()){if(!m.models.loaded(m.controllerKey(side)))controller(triangles,m.grips[side].pose,side==0);axes(lines,m.grips[side].pose,.07);}
    auto* raw=m.raw();if(raw&&raw->fresh()) {
        if(!m.rawPlaced&&m.placed){m.rawOrigin=raw->pose;m.rawPlaced=true;}
        const Pose world=m.hasFit ? m.result.world:Pose(m.rawAnchor*m.rawOrigin.inverse());
        for(const auto& entry:m.devices)if(entry.second.fresh()){const Pose p=world*entry.second.pose;if(!m.models.loaded(entry.second.identity.model))tracker(triangles,p,entry.first==m.selected ? Teal:Dark);axes(lines,p,.07);}
        const Pose p=world*raw->pose;
        if(m.hasFit){const Pose predicted=p*m.result.mount;if(!m.models.loaded(m.controllerKey(m.reference)))controller(triangles,predicted,m.reference==0,true);line(lines,predicted.translation(),m.grips[m.reference].pose.translation(),Orange);}
    }
}

static void renderDevices(Model& m,Renderer& renderer,DirectX::FXMMATRIX vp) {
    for(int side=0;side<2;++side)if(m.grips[side].tracked())m.models.draw(renderer,m.controllerKey(side),m.grips[side].pose,vp);
    auto* raw=m.raw();if(!raw||!raw->fresh())return;
    const Pose world=m.hasFit ? m.result.world:Pose(m.rawAnchor*m.rawOrigin.inverse());
    for(const auto& entry:m.devices)if(entry.second.fresh())m.models.draw(renderer,entry.second.identity.model,Pose(world*entry.second.pose),vp);
    if(m.hasFit)m.models.draw(renderer,m.controllerKey(m.reference),Pose(world*raw->pose*m.result.mount),vp,true);
}

static std::string snapshot(Model& model,uint64_t frame,uint64_t requested,uint64_t exported,const std::string& exportPath) {
    cJSON* root=cJSON_CreateObject();cJSON_AddNumberToObject(root,"schema",1);cJSON_AddNumberToObject(root,"pid",GetCurrentProcessId());cJSON_AddNumberToObject(root,"frame",double(frame));cJSON_AddNumberToObject(root,"uptime_ms",std::chrono::duration<double,std::milli>(Clock::now()-model.inspectionStarted).count());
    cJSON_AddStringToObject(root,"mode",model.preview ? "synthetic_preview":model.service ? "fixed_tracking_service":"hardware_vr");cJSON_AddStringToObject(root,"phase",model.phase());cJSON_AddStringToObject(root,"message",model.message.c_str());
    cJSON_AddStringToObject(root,"reference",model.reference ? "right":"left");cJSON_AddStringToObject(root,"tracker",model.selected.c_str());cJSON_AddStringToObject(root,"role",external_trackers::Roles[model.role]);cJSON_AddBoolToObject(root,"map_locked",model.mapLocked);cJSON_AddBoolToObject(root,"has_fit",model.hasFit);
    cJSON_AddNumberToObject(root,"capture_seconds",model.captureSeconds);cJSON_AddNumberToObject(root,"capture_samples",double(model.samples.size()));cJSON_AddBoolToObject(root,"validation_running",model.validationRunning);cJSON_AddNumberToObject(root,"validation_seconds",model.validationSeconds);cJSON_AddNumberToObject(root,"validation_samples",double(model.validation.count));cJSON_AddNumberToObject(root,"position_gap_m",model.instantPosition);cJSON_AddNumberToObject(root,"angle_gap_degrees",model.instantAngle);cJSON_AddNumberToObject(root,"validation_position_rms_m",model.validation.positionRms());cJSON_AddNumberToObject(root,"validation_angle_rms_degrees",model.validation.angleRms());
    const auto pose=[](const Pose& p){cJSON* object=cJSON_CreateObject();const Eigen::Quaterniond q(p.rotation());const double position[]{p.translation().x(),p.translation().y(),p.translation().z()},orientation[]{q.x(),q.y(),q.z(),q.w()};cJSON_AddItemToObject(object,"position_m",cJSON_CreateDoubleArray(position,3));cJSON_AddItemToObject(object,"orientation_xyzw",cJSON_CreateDoubleArray(orientation,4));return object;};
    cJSON* devices=cJSON_AddArrayToObject(root,"devices");
    for(int i=0;i<3;++i){const Located& value=i==0 ? model.head:model.grips[i-1];cJSON* device=pose(value.pose);cJSON_AddStringToObject(device,"name",i==0 ? "head":i==1 ? "left_controller":"right_controller");cJSON_AddStringToObject(device,"space","Quest STAGE");cJSON_AddNumberToObject(device,"location_flags",double(value.flags));cJSON_AddBoolToObject(device,"tracked",value.tracked());cJSON_AddItemToArray(devices,device);}
    for(const auto& entry:model.devices){cJSON* device=pose(entry.second.pose);cJSON_AddStringToObject(device,"name",entry.first.c_str());cJSON_AddStringToObject(device,"space","raw libsurvive world");cJSON_AddBoolToObject(device,"fresh",entry.second.fresh());cJSON_AddNumberToObject(device,"age_seconds",entry.second.age+std::chrono::duration<double>(Clock::now()-entry.second.received).count());cJSON_AddItemToArray(devices,device);}
    cJSON_AddStringToObject(root,"system_name",model.systemName.c_str());
    cJSON_AddStringToObject(root,"calibration_solver","hyblocker_regular_kabsch_translation");cJSON_AddBoolToObject(root,"room_solver_disabled",model.roomSolverDisabled);
    cJSON_AddStringToObject(root,"capture_file",model.captureFile.c_str());cJSON_AddStringToObject(root,"capture_export_error",model.captureExportError.c_str());
    cJSON_AddBoolToObject(root,"alignment_applied",model.hasFit);cJSON_AddBoolToObject(root,"profile_saved",model.step==Step::Saved);cJSON_AddNumberToObject(root,"fit_holdout_samples",double(model.result.reserved.count));cJSON_AddNumberToObject(root,"fit_holdout_position_rms_m",model.result.reserved.positionRms());
    if(model.hasFit){cJSON_AddItemToObject(root,"quest_from_tracker_world",pose(model.result.world));cJSON_AddItemToObject(root,"tracker_from_controller_mount",pose(model.result.mount));cJSON* aligned=cJSON_AddArrayToObject(root,"aligned_trackers");for(const auto& entry:model.devices){cJSON* device=pose(Pose(model.result.world*entry.second.pose));cJSON_AddStringToObject(device,"name",entry.first.c_str());cJSON_AddStringToObject(device,"space","Quest STAGE");cJSON_AddItemToArray(aligned,device);}}
    cJSON_AddBoolToObject(root,"fast_mode",model.fastMode);cJSON_AddNumberToObject(root,"capture_target_samples",double(model.captureTarget()));cJSON_AddNumberToObject(root,"validation_target_samples",double(ValidationSamples));cJSON_AddNumberToObject(root,"sample_rate_hz",1/SampleInterval);
    cJSON_AddBoolToObject(root,"device_models_open",model.modelSettings);
    cJSON* identities=cJSON_AddArrayToObject(root,"device_models");
    const auto identity=[&](const std::string& device,const device_models::Identity& id,const std::string& key,const std::string& choice){cJSON* item=cJSON_CreateObject();cJSON_AddStringToObject(item,"device",device.c_str());cJSON_AddStringToObject(item,"model_id",id.model.c_str());cJSON_AddStringToObject(item,"name",id.name.c_str());cJSON_AddStringToObject(item,"identity_source",id.source.c_str());cJSON_AddStringToObject(item,"reported",id.reported.c_str());cJSON_AddStringToObject(item,"selection",choice.c_str());cJSON_AddStringToObject(item,"asset_key",key.c_str());const auto* asset=model.models.find(key);cJSON_AddBoolToObject(item,"asset_loaded",asset&&asset->loaded());cJSON_AddStringToObject(item,"asset_error",asset ? asset->error.c_str():"No matching model");cJSON_AddBoolToObject(item,"generic_fallback",!asset||!asset->loaded());cJSON_AddItemToArray(identities,item);};
    for(int side=0;side<2;++side)identity(side ? "right_controller":"left_controller",model.controllerIdentity[side],model.controllerKey(side),model.controllerOverride[side]);
    for(const auto& entry:model.devices)identity(entry.first,entry.second.identity,entry.second.identity.model,"auto");
    cJSON* actions=cJSON_AddObjectToObject(root,"actions");for(const char* action: {"select_reference","select_tracker","select_role","select_left_model","select_right_model","show_device_models","set_fast_mode","identify_reference","lock_map","start_capture","fit","start_validation","stop_validation","save_profile","load_profile","reset","start_service","place_panel","exit"})cJSON_AddBoolToObject(actions,action,model.can(action));
    cJSON* render=cJSON_AddObjectToObject(root,"render");cJSON_AddNumberToObject(render,"export_requested",double(requested));cJSON_AddNumberToObject(render,"export_completed",double(exported));cJSON_AddStringToObject(render,"last_export",exportPath.c_str());cJSON_AddStringToObject(render,"models","identity-selected textured meshes; generic fallback");
    return inspectionJson(root);
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    HWND window=nullptr;
    CalibrationInspection inspection;
    try {
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        wchar_t executable[MAX_PATH];GetModuleFileNameW(nullptr,executable,MAX_PATH);const fs::path home=fs::path(executable).parent_path();SetCurrentDirectoryW(home.c_str());fs::create_directories(L"data");
        int argc;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);bool preview=false;double stopAfter=0;std::wstring runtime=(home/L"runtime/virtualdesktop-openxr.dll").wstring();
        for(int i=1;i<argc;++i){const std::wstring arg=argv[i];if(arg==L"--preview")preview=true;else if(arg==L"--runtime"&&i+1<argc)runtime=argv[++i];else if(arg==L"--seconds"&&i+1<argc){stopAfter=std::stod(argv[++i]);boundedRun=true;}}LocalFree(argv);
        WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=instance;wc.lpszClassName=L"VDXRCalibrationWindow";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);
        RECT rect{0,0,1280,840};AdjustWindowRect(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE);
        window=CreateWindowW(wc.lpszClassName,L"VDXR Tracker Calibration",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);ShowWindow(window,SW_SHOW);
        Model model;model.preview=preview;XrApp xr;
        {std::ifstream file("data/calibration-settings.json");const std::string json((std::istreambuf_iterator<char>(file)),{});cJSON* settings=cJSON_Parse(json.c_str());const auto* value=cJSON_GetObjectItemCaseSensitive(settings,"fast_mode");if(cJSON_IsBool(value))model.fastMode=cJSON_IsTrue(value);cJSON_Delete(settings);}
        if(!preview) {
            model.owner=CreateMutexW(nullptr,TRUE,L"Local\\VDXR.Libsurvive.Writer.v1");if(!model.owner||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("Close the other tracker provider or calibration app first");
            xr.init(runtime);
        } else {if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&xr.device,nullptr,nullptr)))throw std::runtime_error("Create preview device");model.message="Design preview. Calibration and saving are disabled.";}
        Renderer renderer;renderer.init(xr.device.Get(),window);model.models.init(xr.device.Get());
        {std::ifstream file("data/device-models.json");const std::string json((std::istreambuf_iterator<char>(file)),{});cJSON* saved=cJSON_Parse(json.c_str());for(int side=0;side<2;++side){const auto* value=cJSON_GetObjectItemCaseSensitive(saved,side ? "right":"left");if(cJSON_IsString(value)&&device_models::controllerChoice(value->valuestring))model.controllerOverride[side]=value->valuestring;}cJSON_Delete(saved);}
        if(preview){model.systemName="Quest 3 (synthetic preview)";model.interactionProfiles[0]=model.interactionProfiles[1]="/interaction_profiles/oculus/touch_controller";}else model.systemName=xr.systemName;model.updateIdentities();
        IMGUI_CHECKVERSION();ImGui::CreateContext();ImGui::StyleColorsLight();auto& style=ImGui::GetStyle();style.WindowPadding={22,18};style.ItemSpacing={9,8};style.FramePadding={10,9};style.FrameRounding=6;style.WindowRounding=0;
        auto& io=ImGui::GetIO();io.DisplaySize={620,820};io.IniFilename=nullptr;io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",22);ImGui_ImplDX11_Init(renderer.device.Get(),renderer.context.Get());
        if(!preview){bool map=false;std::ifstream file(L"data/lighthouse.json");const std::string contents((std::istreambuf_iterator<char>(file)),{});map=contents.find("\"PositionSet\":\"1\"")!=std::string::npos;model.initHardware(map);}
        const auto started=Clock::now();auto previous=started;
        bool oldTrigger=false;int pointingSide=0;uint64_t frameNumber=0,exportRequested=0,exportCompleted=0;std::string pendingExport,lastExport,lastLoggedState;
        fs::create_directories(L"data/inspection");
        while(!exitRequested&&(!xr.quit||model.service)) {
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            inspection.process([&](const std::string& input){
                cJSON* request=cJSON_Parse(input.c_str());cJSON* operation=cJSON_GetObjectItemCaseSensitive(request,"op");cJSON* reply=cJSON_CreateObject();bool ok=false;
                if(cJSON_IsString(operation)) {
                    const std::string op=operation->valuestring;
                    if(op=="export_frame") {
                        const cJSON* target=cJSON_GetObjectItemCaseSensitive(request,"view");const std::string view=cJSON_IsString(target) ? target->valuestring:"mirror";
                        if(pendingExport.empty()&&(view=="mirror"||view=="model_catalog"||(view=="left_eye"&&!preview&&!model.service))){pendingExport=view;++exportRequested;ok=true;cJSON_AddBoolToObject(reply,"accepted",true);cJSON_AddNumberToObject(reply,"export_ticket",double(exportRequested));}
                    }else {
                        const cJSON* value=cJSON_GetObjectItemCaseSensitive(request,"value");const int index=cJSON_IsNumber(value) ? value->valueint:0;const std::string text=cJSON_IsString(value) ? value->valuestring:"";
                        if(op!="set_fast_mode"||(cJSON_IsNumber(value)&&(value->valuedouble==0||value->valuedouble==1)))ok=model.perform(op,preview||model.service ? nullptr:&xr,index,text);
                    }
                }
                inspection.update(snapshot(model,frameNumber,exportRequested,exportCompleted,lastExport));
                cJSON_Delete(request);cJSON_AddBoolToObject(reply,"ok",ok);cJSON_AddNumberToObject(reply,"frame",double(frameNumber));if(!ok)cJSON_AddStringToObject(reply,"error","Action unavailable or invalid parameters; inspect state.actions");return inspectionJson(reply);
            });
            const auto now=Clock::now();const double elapsed=std::chrono::duration<double>(now-started).count();if(stopAfter>0&&elapsed>stopAfter)break;
            io.DeltaTime=float(std::max(.001,std::chrono::duration<double>(now-previous).count()));previous=now;
            if(model.service&&xr.session){xr.close();model.head.flags=0;for(auto& grip:model.grips)grip.flags=0;for(auto& aim:model.aims)aim.flags=0;}
            const bool vrLive=!preview&&!model.service;
            XrFrameState frame{XR_TYPE_FRAME_STATE};XrTime time=0;
            if(vrLive){if(!xr.begin(frame)){Sleep(10);continue;}time=xr.now();if(frameNumber%90==0){xr.identities();for(int side=0;side<2;++side)model.interactionProfiles[side]=xr.profiles[side];model.updateIdentities();}model.head=xr.head(time);for(int side=0;side<2;++side){model.grips[side]=xr.grip(side,time);model.aims[side]=xr.aim(side,time);}if(xr.recentered){model.reset();model.placed=false;model.message="Quest reference changed. Recalibrate before saving or using a profile.";xr.recentered=false;}}
            else if(preview) {model.head={Pose::Identity(),15};model.head.pose.translation()=Eigen::Vector3d(0,1.55,0);for(int side=0;side<2;++side){model.grips[side]={Pose::Identity(),15};model.grips[side].pose.translation()=Eigen::Vector3d(side ? .25:-.25,1.05,-.5);model.grips[side].pose.linear()=Eigen::AngleAxisd(-.4,Eigen::Vector3d::UnitX()).toRotationMatrix();}RawPose r;r.pose=Pose::Identity();r.pose.translation()=Eigen::Vector3d(.15,1,-.65);r.received=Clock::now();r.sampleTime=1;r.identity={"vive-tracker-3","Vive Tracker 3.0","synthetic preview fixture",""};model.devices["PREVIEW TRACKER"]=r;model.selected="PREVIEW TRACKER";}
            if(!model.placed&&model.head.tracked()&&(preview||(frameNumber>=10&&model.head.pose.translation().norm()>.0001))){const auto forward=model.head.pose.rotation()*Eigen::Vector3d(0,0,-1);const double yaw=std::atan2(-forward.x(),-forward.z());Pose anchor=Pose::Identity();anchor.linear()=Eigen::AngleAxisd(yaw,Eigen::Vector3d::UnitY()).toRotationMatrix();anchor.translation()=model.head.pose.translation();model.panelPose=anchor;model.panelPose.translation()=anchor*Eigen::Vector3d(-.72,0,-1.2);model.rawAnchor=anchor;model.rawAnchor.translation()=anchor*Eigen::Vector3d(.35,-.45,-1.0);model.placed=true;}
            model.tick(vrLive ? &xr:nullptr,time);
            float uiX=mouseX*620/620,uiY=mouseY*820/840;bool pressed=mouseDown;bool rayHit=false;Eigen::Vector3d rayEnd=Eigen::Vector3d::Zero();
            if(mouseX>620)uiX=-1000;
            if(vrLive){const bool left=xr.trigger(0)>.6f,right=xr.trigger(1)>.6f;if(right&&!oldTrigger)pointingSide=1;if(left&&!oldTrigger)pointingSide=0;const bool trigger=pointingSide ? right:left;oldTrigger=left||right;
                const auto aim=model.aims[pointingSide];if(aim.tracked()&&GetTickCount64()>mouseActiveUntil) {
                    const Eigen::Vector3d origin=aim.pose.translation(),direction=aim.pose.rotation()*Eigen::Vector3d(0,0,-1),normal=model.panelPose.rotation()*Eigen::Vector3d::UnitZ();const double denominator=normal.dot(direction);rayEnd=origin+direction*1.2;
                    if(std::abs(denominator)>.0001){const double distance=normal.dot(model.panelPose.translation()-origin)/denominator;if(distance>0&&distance<4){const Eigen::Vector3d local=model.panelPose.inverse()*(origin+distance*direction);if(std::abs(local.x())<.325&&std::abs(local.y())<.43){uiX=float((local.x()/.65+.5)*620);uiY=float((.5-local.y()/.86)*820);pressed=trigger;rayHit=true;rayEnd=origin+distance*direction;}}}
                }
            }
            io.AddMousePosEvent(uiX,uiY);io.AddMouseButtonEvent(0,pressed);io.MouseDrawCursor=rayHit;
            ImGui_ImplDX11_NewFrame();ImGui::NewFrame();drawUi(model,vrLive ? &xr:nullptr);ImGui::Render();renderer.setTarget(renderer.uiView.Get(),620,820,false);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            std::vector<Vertex> triangles,lines;geometry(model,triangles,lines);if(!preview&&model.aims[pointingSide].tracked())line(lines,model.aims[pointingSide].pose.translation(),rayEnd,rayHit ? Teal:Orange);
            const auto panelVertices=scene::panel(model.panelPose,.65f,.86f);
            const auto render=[&](DirectX::FXMMATRIX vp,bool includePanel){renderer.draw(triangles,vp);renderer.draw(lines,vp,true);renderDevices(model,renderer,vp);if(includePanel&&model.placed)renderer.draw(panelVertices,vp,false,renderer.uiResource.Get());};
            int renderedEye=0;
            if(vrLive)xr.end(frame,[&](const XrView& eye,ID3D11RenderTargetView* target){renderer.setTarget(target,1440,1440);const auto vp=scene::matrix(fromXr(eye.pose).inverse())*projection(eye.fov);render(vp,true);if(renderedEye++==0&&pendingExport=="left_eye"){renderer.exportPng(target,L"data/inspection/left-eye.png");pendingExport.clear();exportCompleted=exportRequested;lastExport="data/inspection/left-eye.png";}});
            renderer.setTarget(renderer.mirrorView.Get(),1280,840);D3D11_VIEWPORT viewport{620,0,660,840,0,1};renderer.context->RSSetViewports(1,&viewport);
            const auto camera=model.head.pose;const Eigen::Vector3d eye=camera.translation()+Eigen::Vector3d(0,0,.2),look=camera.translation()+camera.rotation()*Eigen::Vector3d(0,-.45,-.8);
            const auto view=DirectX::XMMatrixLookAtRH(DirectX::XMVectorSet(float(eye.x()),float(eye.y()),float(eye.z()),1),DirectX::XMVectorSet(float(look.x()),float(look.y()),float(look.z()),1),DirectX::XMVectorSet(0,1,0,0));
            render(view*DirectX::XMMatrixPerspectiveFovRH(1.3f,660.f/840,.05f,50),false);
            viewport={0,0,620,840,0,1};renderer.context->RSSetViewports(1,&viewport);renderer.draw(scene::panel(Pose::Identity(),2,2),DirectX::XMMatrixIdentity(),false,renderer.uiResource.Get(),true);
            if(pendingExport=="mirror"){renderer.exportPng(renderer.mirrorView.Get(),L"data/inspection/mirror.png");pendingExport.clear();exportCompleted=exportRequested;lastExport="data/inspection/mirror.png";}
            renderer.mirror->Present(1,0);
            if(pendingExport=="model_catalog") {
                D3D11_TEXTURE2D_DESC desc{};desc.Width=1280;desc.Height=840;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
                ComPtr<ID3D11Texture2D> image;check(renderer.device->CreateTexture2D(&desc,nullptr,&image),"Model catalog image");ComPtr<ID3D11RenderTargetView> target;check(renderer.device->CreateRenderTargetView(image.Get(),nullptr,&target),"Model catalog view");renderer.setTarget(target.Get(),1280,840);
                const auto view=DirectX::XMMatrixLookAtRH(DirectX::XMVectorSet(0,.36f,.52f,1),DirectX::XMVectorSet(0,0,0,1),DirectX::XMVectorSet(0,1,0,0));const auto vp=view*DirectX::XMMatrixPerspectiveFovRH(.9f,1280.f/840,.01f,5);
                const char* keys[]{"meta-quest-touch-plus-left","meta-quest-touch-plus-right","oculus-touch-v3-left","oculus-touch-v3-right","vive-tracker-3"};
                for(int i=0;i<5;++i){Pose pose=Pose::Identity();pose.translation()=Eigen::Vector3d(i<2 ? -.22:i<4 ? 0:.22,0,i<4 ? (i%2 ? .09:-.09):0);model.models.draw(renderer,keys[i],pose,vp);}
                renderer.exportPng(target.Get(),L"data/inspection/model-catalog.png");pendingExport.clear();exportCompleted=exportRequested;lastExport="data/inspection/model-catalog.png";
            }
            ++frameNumber;const auto state=snapshot(model,frameNumber,exportRequested,exportCompleted,lastExport);inspection.update(state);
            const std::string transition=std::string(model.phase())+":"+model.message;
            if(transition!=lastLoggedState){try{atomicWrite(L"data/inspection/session-state.json",state);std::ofstream events("data/inspection/events.jsonl",std::ios::app);events<<state<<'\n';lastLoggedState=transition;}catch(...){} }
        }
        ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();xr.close();DestroyWindow(window);return 0;
    }catch(const std::exception& error){
        startupError=error.what();
        try{fs::create_directories("data/inspection");cJSON* failure=cJSON_CreateObject();cJSON_AddNumberToObject(failure,"schema",1);cJSON_AddNumberToObject(failure,"pid",GetCurrentProcessId());cJSON_AddStringToObject(failure,"phase","error");cJSON_AddStringToObject(failure,"error",error.what());cJSON* actions=cJSON_AddObjectToObject(failure,"actions");cJSON_AddBoolToObject(actions,"exit",true);const auto json=inspectionJson(failure);inspection.update(json);atomicWrite(L"data/inspection/error.json",json);}catch(...){}
        if(window)InvalidateRect(window,nullptr,TRUE);
        while(!boundedRun&&!exitRequested){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}inspection.process([](const std::string& input){cJSON* request=cJSON_Parse(input.c_str());cJSON* op=cJSON_GetObjectItemCaseSensitive(request,"op");const bool exit=cJSON_IsString(op)&&std::string(op->valuestring)=="exit";cJSON_Delete(request);if(exit)exitRequested=true;return exit ? "{\"ok\":true}":"{\"ok\":false,\"error\":\"startup failed; only state and exit are available\"}";});Sleep(20);}
        if(window)DestroyWindow(window);return 1;
    }
}
