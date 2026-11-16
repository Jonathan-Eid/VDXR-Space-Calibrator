# VDXR Space Calibrator: regular calibration app

The app opens its own OpenXR session through the VDXR build beside the executable. It renders a floor grid, controller rays, simplified ringless Quest controller models and Vive-style tracker models, with the same controls available in a desktop window. These are procedural device representations, not manufacturer meshes.

## Use

1. Connect the Quest to the PC through Virtual Desktop, with **Track Controllers** enabled. Keep SteamVR closed.
2. Open `vdxr-calibration.exe`. Both desktop and controller-trigger interaction operate the same app actions. **Bring panel here** places the panel beside the current headset view.
3. Choose the Quest reference controller and Vive tracker. Trackers appear in a separately anchored **raw-space preview** before an alignment exists; that preview is not their measured position in Quest space.
4. For a new Lighthouse map, leave the tracker stationary with clear sensor visibility, wait for poses, and choose **Lock Lighthouse map**. A previously saved map loads locked. Moving base stations requires rebuilding the map; close the app and move `data/lighthouse.json` aside before reopening.
5. Attach the selected tracker rigidly to the selected controller, keeping tracking sensors uncovered. Choose **Start regular calibration**. Move slowly over different positions and rotate about multiple axes. Capture pauses when either selected pose is unavailable.
6. The app fits the room transform and the mounting offset. It rejects insufficient motion or inconsistent data. After a fit passes, the teal ghost predicts the controller from the tracker pose; compare it with the solid Quest controller.
7. Choose **Run separate validation** and perform another movement with the pair still rigidly attached. The app requires fresh data, movement coverage, and acceptable residuals before **Save profile** becomes available.
8. A saved profile can start the **fixed tracking service**. This closes the calibration VR session and publishes physical tracker poses for a compatible VDXR runtime while the window stays open. The calibration transform remains fixed.

`data/stage.txt`, `data/mount.txt` and `data/profile.json` remain local. **Load saved profile** requires validation in the current Quest tracking space before tracking is enabled. Recenter or boundary changes invalidate the current calibration. The app does not change the global active OpenXR runtime.

The positional and angular values are alignment residuals, not independently measured physical accuracy. Hardware quality, occlusion, latency, drift, mounting orientation and application compatibility still need real testing. The runtime bridge currently reports valid poses without asserting optical TRACKED confidence.

## Inspection and verification

Use the app's own local named-pipe API, not computer use. The endpoint is `\\.\pipe\VDXRCalibration.<pid>` and rejects remote clients. Every response is one UTF-8 JSON message; acknowledge receipt with `{"ack":true}`. A request is limited to 32 KiB. One client is served at a time.

```powershell
./Inspect-App.ps1 -Request '{"op":"state"}'
./Inspect-App.ps1 -Request '{"op":"select_reference","value":1}'
./Inspect-App.ps1 -Request '{"op":"export_frame","view":"mirror"}'
./Inspect-App.ps1 -Request '{"op":"export_frame","view":"left_eye"}'
./Inspect-App.ps1 -Request '{"op":"exit"}'
```

`state` includes the mode, phase, frame/time, available actions, selections, coordinate frames, device poses/validity/age, operation progress, residuals, and render export receipts. Preview mode is explicitly `synthetic_preview` and cannot capture or save calibration data. Startup failures remain inspectable with `state` and `exit`, and are also written to `data/inspection/error.json`.

Actions are `select_reference` (0 left, 1 right), `select_tracker` (serial string), `select_role` (role index), `identify_reference`, `lock_map`, `start_capture`, `fit`, `start_validation`, `stop_validation`, `save_profile`, `load_profile`, `reset`, `start_service`, `place_panel` and `exit`. They use the same guarded handlers as the UI. Unavailable actions are rejected; automation cannot bypass validation.

`export_frame` accepts `mirror` or `left_eye`. The acceptance response is not completion: poll `state.render.export_completed` for the returned ticket before reading the exported PNG. Only one export may be pending. Frame export writes to fixed app-owned inspection paths; it accepts no arbitrary file path. Inspect exported images directly. A timed-out mutating command must not be retried blindly; inspect the current state first.

`Test-AppInspection.ps1 -PackageDirectory PATH` checks preview mode, selection, invalid inputs, acceptance guards, native frame exports, color channels and API shutdown. The native math test separately verifies known room/mount transforms and rejects degenerate motion.

## Design provenance

The regular reference/target device workflow is inspired by [Hyblocker's Space Calibrator](https://github.com/hyblocker/OpenVR-SpaceCalibrator), which publishes the Steam version. This app uses a new OpenXR/libsurvive implementation and contains no OpenVR driver or SteamVR dashboard dependency. Its fitting code is native C++ with Eigen; Python is not needed to operate the app.

This source is a standalone **VDXR-Space-Calibrator** app. The VDXR runtime fork supplies the versioned pose protocol. No new public app repository or finished release has been published yet.

## Build from source

Requires Windows x64, Visual Studio 2022 with C++ tools, CMake, Git, and NuGet available for libsurvive dependencies. Run scripts/Build.ps1 with -BuildRoot pointing to a build directory and -RuntimeDirectory pointing to the experimental VDXR DLL/JSON directory. The script initializes pinned source dependencies, builds double-precision libsurvive and the app, tests the solver and inspection API, and assembles a package. Personal room maps, recordings, and profiles belong in ignored data directories.

