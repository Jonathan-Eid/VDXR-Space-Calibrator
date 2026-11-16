// SPDX-License-Identifier: MIT
#pragma once
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <unknwn.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "calibration_math.h"

namespace calibration {
inline void xrCheck(XrResult result,const char* name) { if(XR_FAILED(result)) throw std::runtime_error(std::string(name)+": "+std::to_string(result)); }
inline Pose fromXr(const XrPosef& input) {
    Pose p=Pose::Identity();const auto& q=input.orientation;
    p.linear()=Eigen::Quaterniond(q.w,q.x,q.y,q.z).normalized().toRotationMatrix();
    p.translation()=Eigen::Vector3d(input.position.x,input.position.y,input.position.z);return p;
}
struct Located { Pose pose{Pose::Identity()}; XrSpaceLocationFlags flags{}; bool tracked() const { return (flags&15)==15; } };
#define CAL_XR_FUNCTIONS(F) \
F(xrGetSystem) F(xrGetSystemProperties) F(xrGetCurrentInteractionProfile) F(xrPathToString) F(xrCreateSession) F(xrDestroySession) F(xrDestroyInstance) F(xrCreateReferenceSpace) F(xrPollEvent) \
F(xrBeginSession) F(xrEndSession) F(xrRequestExitSession) F(xrWaitFrame) F(xrBeginFrame) F(xrEndFrame) \
F(xrStringToPath) F(xrCreateActionSet) F(xrDestroyActionSet) F(xrCreateAction) F(xrSuggestInteractionProfileBindings) \
F(xrAttachSessionActionSets) F(xrCreateActionSpace) F(xrSyncActions) F(xrGetActionStateFloat) F(xrApplyHapticFeedback) \
F(xrLocateSpace) F(xrLocateViews) F(xrCreateSwapchain) F(xrEnumerateSwapchainImages) F(xrAcquireSwapchainImage) \
F(xrWaitSwapchainImage) F(xrReleaseSwapchainImage) F(xrGetD3D11GraphicsRequirementsKHR) F(xrConvertWin32PerformanceCounterToTimeKHR)
class XrApp {
    HMODULE library{};PFN_xrGetInstanceProcAddr api{};
#define DECLARE(name) PFN_##name name{};
    CAL_XR_FUNCTIONS(DECLARE)
#undef DECLARE
    XrSystemId system{};XrActionSet actionSet{};XrAction gripAction{},aimAction{},triggerAction{},hapticAction{};
    XrPath hands[2]{};XrSpace grips[2]{},aims[2]{},view{};
    XrSwapchain swapchains[2]{};
    std::vector<XrSwapchainImageD3D11KHR> images[2];
    std::vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>> targets[2];
    bool running{};
public:
    XrInstance instance{};XrSession session{};XrSpace stage{};
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    bool quit{},recentered{},focused{};
    std::string systemName,profiles[2];
    ~XrApp() { close(); }
    void init(const std::wstring& path) {
        library=LoadLibraryW(path.c_str());if(!library)throw std::runtime_error("Cannot load the app's VDXR runtime");
        const auto negotiate=reinterpret_cast<PFN_xrNegotiateLoaderRuntimeInterface>(GetProcAddress(library,"xrNegotiateLoaderRuntimeInterface"));
        if(!negotiate)throw std::runtime_error("Runtime negotiation entry missing");
        XrNegotiateLoaderInfo loader{XR_LOADER_INTERFACE_STRUCT_LOADER_INFO,XR_LOADER_INFO_STRUCT_VERSION,sizeof(XrNegotiateLoaderInfo),1,1,XR_MAKE_VERSION(1,0,0),XR_CURRENT_API_VERSION};
        XrNegotiateRuntimeRequest request{XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST,XR_RUNTIME_INFO_STRUCT_VERSION,sizeof(XrNegotiateRuntimeRequest)};
        xrCheck(negotiate(&loader,&request),"Negotiate VDXR");api=request.getInstanceProcAddr;
        PFN_xrVoidFunction function;xrCheck(api(XR_NULL_HANDLE,"xrCreateInstance",&function),"Resolve create instance");
        const char* extensions[]{XR_KHR_D3D11_ENABLE_EXTENSION_NAME,XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME};
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};std::strcpy(info.applicationInfo.applicationName,"VDXR Tracker Calibration");info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);info.enabledExtensionCount=2;info.enabledExtensionNames=extensions;
        xrCheck(reinterpret_cast<PFN_xrCreateInstance>(function)(&info,&instance),"Create calibration instance");
#define RESOLVE(name) xrCheck(api(instance,#name,&function),#name);name=reinterpret_cast<PFN_##name>(function);
        CAL_XR_FUNCTIONS(RESOLVE)
#undef RESOLVE
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        xrCheck(xrGetSystem(instance,&systemInfo,&system),"Connect Virtual Desktop to the Quest first");
        XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};xrCheck(xrGetSystemProperties(instance,system,&properties),"Read headset identity");systemName=properties.systemName;
        XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};xrCheck(xrGetD3D11GraphicsRequirementsKHR(instance,system,&requirements),"Query graphics requirements");
        using Microsoft::WRL::ComPtr;ComPtr<IDXGIFactory1> factory;if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))throw std::runtime_error("Create GPU factory");
        ComPtr<IDXGIAdapter1> adapter;
        for(UINT i=0;;++i) { if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND)throw std::runtime_error("Find headset GPU");DXGI_ADAPTER_DESC1 desc;adapter->GetDesc1(&desc);if(!std::memcmp(&desc.AdapterLuid,&requirements.adapterLuid,sizeof(LUID)))break;adapter.Reset(); }
        const auto feature=requirements.minFeatureLevel;
        if(FAILED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,&feature,1,D3D11_SDK_VERSION,&device,nullptr,nullptr)))throw std::runtime_error("Create headset graphics device");
        XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};std::strcpy(set.actionSetName,"calibration");std::strcpy(set.localizedActionSetName,"Calibration");xrCheck(xrCreateActionSet(instance,&set,&actionSet),"Create input set");
        xrCheck(xrStringToPath(instance,"/user/hand/left",&hands[0]),"Left hand path");xrCheck(xrStringToPath(instance,"/user/hand/right",&hands[1]),"Right hand path");
        const auto action=[&](const char* name,XrActionType type,XrAction& output) {
            XrActionCreateInfo a{XR_TYPE_ACTION_CREATE_INFO};std::strcpy(a.actionName,name);std::strcpy(a.localizedActionName,name);a.actionType=type;a.countSubactionPaths=2;a.subactionPaths=hands;xrCheck(xrCreateAction(actionSet,&a,&output),"Create controller action");
        };
        action("grip",XR_ACTION_TYPE_POSE_INPUT,gripAction);action("aim",XR_ACTION_TYPE_POSE_INPUT,aimAction);action("select",XR_ACTION_TYPE_FLOAT_INPUT,triggerAction);action("identify",XR_ACTION_TYPE_VIBRATION_OUTPUT,hapticAction);
        std::vector<XrActionSuggestedBinding> bindings;
        for(int side=0;side<2;++side) for(const auto& input:std::vector<std::pair<XrAction,const char*>>{{gripAction,"/input/grip/pose"},{aimAction,"/input/aim/pose"},{triggerAction,"/input/trigger/value"},{hapticAction,"/output/haptic"}}) {
            XrPath path;const std::string full=std::string(side ? "/user/hand/right":"/user/hand/left")+input.second;xrCheck(xrStringToPath(instance,full.c_str(),&path),"Input binding path");bindings.push_back({input.first,path});
        }
        XrPath profile;xrCheck(xrStringToPath(instance,"/interaction_profiles/oculus/touch_controller",&profile),"Touch controller profile");
        XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};suggested.interactionProfile=profile;suggested.countSuggestedBindings=uint32_t(bindings.size());suggested.suggestedBindings=bindings.data();xrCheck(xrSuggestInteractionProfileBindings(instance,&suggested),"Bind controller input");
        XrGraphicsBindingD3D11KHR graphics{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};graphics.device=device.Get();XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};sessionInfo.next=&graphics;sessionInfo.systemId=system;xrCheck(xrCreateSession(instance,&sessionInfo,&session),"Create calibration VR session");
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&actionSet;xrCheck(xrAttachSessionActionSets(session,&attach),"Attach controls");
        XrReferenceSpaceCreateInfo reference{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};reference.poseInReferenceSpace.orientation.w=1;reference.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_STAGE;xrCheck(xrCreateReferenceSpace(session,&reference,&stage),"Create room reference");reference.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;xrCheck(xrCreateReferenceSpace(session,&reference,&view),"Create headset reference");
        for(int side=0;side<2;++side) for(int kind=0;kind<2;++kind) {
            XrActionSpaceCreateInfo a{XR_TYPE_ACTION_SPACE_CREATE_INFO};a.action=kind ? aimAction:gripAction;a.subactionPath=hands[side];a.poseInActionSpace.orientation.w=1;xrCheck(xrCreateActionSpace(session,&a,kind ? &aims[side]:&grips[side]),"Create controller pose space");
        }
        for(int eye=0;eye<2;++eye) {
            XrSwapchainCreateInfo s{XR_TYPE_SWAPCHAIN_CREATE_INFO};s.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;s.format=DXGI_FORMAT_R8G8B8A8_UNORM;s.sampleCount=1;s.width=s.height=1440;s.faceCount=s.arraySize=s.mipCount=1;xrCheck(xrCreateSwapchain(session,&s,&swapchains[eye]),"Create eye image");
            uint32_t count;xrCheck(xrEnumerateSwapchainImages(swapchains[eye],0,&count,nullptr),"Count eye images");images[eye].resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});xrCheck(xrEnumerateSwapchainImages(swapchains[eye],count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images[eye].data())),"Read eye images");
            for(auto& image:images[eye]) { D3D11_RENDER_TARGET_VIEW_DESC desc{};desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;ComPtr<ID3D11RenderTargetView> target;if(FAILED(device->CreateRenderTargetView(image.texture,&desc,&target)))throw std::runtime_error("Create eye render target");targets[eye].push_back(target); }
        }
    }
    bool begin(XrFrameState& frame) {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        while(xrPollEvent(instance,&event)==XR_SUCCESS) {
            if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING)recentered=true;
            if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                const auto state=reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state;
                if(state==XR_SESSION_STATE_READY&&!running) { XrSessionBeginInfo info{XR_TYPE_SESSION_BEGIN_INFO};info.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;xrCheck(xrBeginSession(session,&info),"Begin calibration");running=true; }
                if(state==XR_SESSION_STATE_STOPPING) { xrCheck(xrEndSession(session),"End calibration");running=false;quit=true; }
                if(state==XR_SESSION_STATE_EXITING||state==XR_SESSION_STATE_LOSS_PENDING)quit=true;
            }
            event={XR_TYPE_EVENT_DATA_BUFFER};
        }
        if(!running||quit)return false;
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};frame={XR_TYPE_FRAME_STATE};xrCheck(xrWaitFrame(session,&wait,&frame),"Wait calibration frame");XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};xrCheck(xrBeginFrame(session,&begin),"Begin calibration frame");
        XrActiveActionSet active{actionSet,XR_NULL_PATH};XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&active;const auto result=xrSyncActions(session,&sync);xrCheck(result,"Read controls");focused=result==XR_SUCCESS;return true;
    }
    XrTime now() { LARGE_INTEGER qpc;QueryPerformanceCounter(&qpc);XrTime time;xrCheck(xrConvertWin32PerformanceCounterToTimeKHR(instance,&qpc,&time),"Read pose time");return time; }
    void identities() {
        for(int side=0;side<2;++side){XrInteractionProfileState state{XR_TYPE_INTERACTION_PROFILE_STATE};profiles[side].clear();if(XR_SUCCEEDED(xrGetCurrentInteractionProfile(session,hands[side],&state))&&state.interactionProfile){char text[XR_MAX_PATH_LENGTH]{};uint32_t count;if(XR_SUCCEEDED(xrPathToString(instance,state.interactionProfile,sizeof(text),&count,text)))profiles[side]=text;}}
    }
    Located locate(XrSpace space,XrTime time) { XrSpaceLocation l{XR_TYPE_SPACE_LOCATION};xrCheck(xrLocateSpace(space,stage,time,&l),"Read device pose");return {fromXr(l.pose),l.locationFlags}; }
    Located head(XrTime time) { return locate(view,time); }
    Located grip(int side,XrTime time) { return locate(grips[side],time); }
    Located aim(int side,XrTime time) { return locate(aims[side],time); }
    float trigger(int side) { XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};get.action=triggerAction;get.subactionPath=hands[side];XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};xrCheck(xrGetActionStateFloat(session,&get,&state),"Read trigger");return state.isActive ? state.currentState:0; }
    void identify(int side) { XrHapticActionInfo a{XR_TYPE_HAPTIC_ACTION_INFO};a.action=hapticAction;a.subactionPath=hands[side];XrHapticVibration v{XR_TYPE_HAPTIC_VIBRATION};v.amplitude=.4f;v.duration=150000000;v.frequency=XR_FREQUENCY_UNSPECIFIED;xrApplyHapticFeedback(session,&a,reinterpret_cast<const XrHapticBaseHeader*>(&v)); }
    void end(const XrFrameState& frame,const std::function<void(const XrView&,ID3D11RenderTargetView*)>& render) {
        XrCompositionLayerProjectionView projections[2]{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.space=stage;projection.viewCount=2;projection.views=projections;
        XrView views[2]{{XR_TYPE_VIEW},{XR_TYPE_VIEW}};XrViewLocateInfo get{XR_TYPE_VIEW_LOCATE_INFO};get.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;get.displayTime=frame.predictedDisplayTime;get.space=stage;XrViewState state{XR_TYPE_VIEW_STATE};uint32_t count;xrCheck(xrLocateViews(session,&get,&state,2,&count,views),"Locate stereo eyes");
        if(frame.shouldRender) for(int eye=0;eye<2;++eye) {
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};uint32_t index;xrCheck(xrAcquireSwapchainImage(swapchains[eye],&acquire,&index),"Acquire eye");XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;xrCheck(xrWaitSwapchainImage(swapchains[eye],&wait),"Wait eye");render(views[eye],targets[eye][index].Get());XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};xrCheck(xrReleaseSwapchainImage(swapchains[eye],&release),"Release eye");projections[eye].pose=views[eye].pose;projections[eye].fov=views[eye].fov;projections[eye].subImage.swapchain=swapchains[eye];projections[eye].subImage.imageRect.extent={1440,1440};
        }
        const auto* layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=frame.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=frame.shouldRender ? 1:0;end.layers=&layer;xrCheck(xrEndFrame(session,&end),"Submit calibration view");
    }
    void close() noexcept {
        if(session&&xrDestroySession) { if(running&&xrRequestExitSession&&xrEndSession) { xrRequestExitSession(session);XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};while(xrPollEvent(instance,&event)==XR_SUCCESS)event={XR_TYPE_EVENT_DATA_BUFFER};xrEndSession(session); }xrDestroySession(session);session=XR_NULL_HANDLE; }
        if(actionSet&&xrDestroyActionSet){xrDestroyActionSet(actionSet);actionSet=XR_NULL_HANDLE;}
        if(instance&&xrDestroyInstance){xrDestroyInstance(instance);instance=XR_NULL_HANDLE;}
        if(library){FreeLibrary(library);library=nullptr;}
    }
};
#undef CAL_XR_FUNCTIONS
} // namespace calibration
