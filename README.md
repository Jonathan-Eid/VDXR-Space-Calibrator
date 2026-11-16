# VDXR Space Calibrator: regular calibration app

The app opens its own OpenXR session through the VDXR build beside the executable. It renders a floor grid, controller rays, textured Quest controller models and a locally installed HTC Tracker 3.0 model, with the same controls available in a desktop window. Unknown devices or missing assets use generic markers.

## Device identification and models

Quest 3/3S Touch Plus, Quest 2 Touch and Touch Pro have separate left/right WebXR Input Profiles meshes and textures. Models show static unpressed buttons. A specific controller interaction profile takes priority. This VDXR backend currently reports generic Touch, so the app selects the headset's default controller model and labels that source as **headset default (controller generation unreported)**. This is not proof of the physical controller generation. Unknown headsets/profiles stay generic.

Open **Device models** to override each hand independently. Overrides persist in `data/device-models.json`, are labeled **manual override**, and do not change calibration transforms. Returning to Automatic uses runtime identity again. Quest 2 models are included for later use; this does not add headset/controller hardware pairing compatibility.

Vive Tracker 3.0 is identified from factory `model_number` or `render_model` metadata, never its serial prefix. Its HTC model is a local import in `data/models/vive-tracker-3.vdm`. The developer's local package already has it installed from [HTC's official download](https://developer.vive.com/eu/hardware/tracker3/). Public source excludes that asset pending confirmed redistribution terms. `scripts/Install-TrackerModel.ps1 -PackageDirectory PATH` downloads and converts it locally without installing or running SteamVR. The build-time importer requires Python, numpy and Pillow; running the installed app does not.

Provenance and license are in `assets/models/`. Model selection and static textured rendering are verified offline. Model-to-physical-device grip alignment, scale and pointer interaction still require the next headset test.

## Use

1. Connect the Quest to the PC through Virtual Desktop, with **Track Controllers** enabled. Keep SteamVR closed.
2. Open `vdxr-calibration.exe`. Both desktop and controller-trigger interaction operate the same app actions. **Bring panel here** places the panel beside the current headset view.
3. Choose the Quest reference controller and Vive tracker. Trackers appear in a separately anchored **raw-space preview** before an alignment exists; that preview is not their measured position in Quest space.
4. For a new Lighthouse map, leave the tracker stationary with clear sensor visibility, wait for poses, and choose **Lock Lighthouse map**. A previously saved map loads locked. Moving base stations requires rebuilding the map; close the app and move `data/lighthouse.json` aside before reopening.
5. Attach the selected tracker rigidly to the selected controller, keeping tracking sensors uncovered. Choose **Start regular calibration**. **Fast mode** (on by default) collects **100 valid sample pairs at up to 20 Hz** (about 5 seconds with uninterrupted tracking), matching the sample budget and tick rate of [Hyblocker's regular Fast mode](https://github.com/hyblocker/OpenVR-SpaceCalibrator/blob/797e464dae298a03217e1bbea640e03781d37b9b/src/overlay/calibration.h). Switch Fast mode off for 250 samples (about 12.5 seconds). Your choice persists in `data/calibration-settings.json` and cannot change during a run. Move over different positions and rotate about multiple axes. Capture pauses when either selected pose is unavailable; poor movement still fails fitting.
6. The app uses an attributed port of the Hyblocker Space Calibrator regular rotation-delta/Kabsch and translation solver to fit the room transform, then estimates the constant mounting offset for visualization and reserved-sample measurement. It rejects insufficient motion or inconsistent data. After a fit passes, the teal ghost predicts the controller from the tracker pose; compare it with the solid Quest controller.
7. A successful fit immediately applies the alignment to the rendered tracker and automatically saves the local profile. It must still pass motion/degeneracy and reserved-sample residual checks. A visible **CALIBRATED** banner confirms alignment; failures show a reason. The device origins retain their fitted physical mounting offset rather than being forced to exactly overlap. Optionally choose **Check alignment** for another movement with the pair still rigidly attached. This extra check requires **50 fresh sample pairs** (about 2.5 seconds minimum), at least 25 cm of movement, rotation, and acceptable residuals to pass. There is no fixed 12-second minimum; inadequate movement takes longer and prompts you to move more.
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

Actions are `select_reference` (0 left, 1 right), `select_tracker` (serial string), `select_role` (role index), `select_left_model` and `select_right_model` (string model ID: `auto`, `meta-quest-touch-plus`, `oculus-touch-v3`, `meta-quest-touch-pro`, `generic`), `identify_reference`, `lock_map`, `start_capture`, `fit`, `start_validation`, `stop_validation`, `save_profile`, `load_profile`, `reset`, `start_service`, `place_panel` and `exit`. They use the same guarded handlers as the UI. Unavailable actions are rejected; automation cannot bypass validation.

`show_device_models` (0/1) controls the settings view and `state.device_models_open` reports it. `state.device_models` reports each model ID, identity source, reported metadata/profile, override, asset key, load error and generic fallback. `export_frame` accepts `mirror`, `left_eye`, or `model_catalog`. The catalog shows Quest 3 left/right, Quest 2 left/right and the locally installed Tracker 3.0 using the native renderer; it is an offline asset view, not evidence of hardware detection. The acceptance response is not completion: poll `state.render.export_completed` for the returned ticket before reading the exported PNG. Only one export may be pending. Frame export writes to fixed app-owned inspection paths; it accepts no arbitrary file path. Inspect exported images directly. A timed-out mutating command must not be retried blindly; inspect the current state first.

`Test-AppInspection.ps1 -PackageDirectory PATH` checks preview mode, selection, invalid inputs, acceptance guards, native frame exports, color channels and API shutdown. The native math test separately verifies known room/mount transforms and rejects degenerate motion. The identity test covers profile precedence, uncertainty, overrides and unknown hardware; preview tests load both Quest 2 and Quest 3 assets.

## Design provenance

The regular reference/target device workflow is inspired by [Hyblocker's Space Calibrator](https://github.com/hyblocker/OpenVR-SpaceCalibrator), which publishes the Steam version. This app uses an OpenXR/libsurvive integration and an attributed port of the upstream regular rotation/translation math and contains no OpenVR driver or SteamVR dashboard dependency. Its fitting code is native C++ with Eigen; Python is not needed to operate the app.

This source is a standalone **VDXR-Space-Calibrator** app. The VDXR runtime fork supplies the versioned pose protocol. No new public app repository or finished release has been published yet.

## Build from source

Requires Windows x64, Visual Studio 2022 with C++ tools, CMake, Git, and NuGet available for libsurvive dependencies. Run scripts/Build.ps1 with -BuildRoot pointing to a build directory and -RuntimeDirectory pointing to the experimental VDXR DLL/JSON directory. The script initializes pinned source dependencies, builds double-precision libsurvive and the app, tests the solver and inspection API, and assembles a package. Personal room maps, recordings, and profiles belong in ignored data directories.



Fast mode is controlled through `set_fast_mode` (value 0 or 1), with `fast_mode` and `capture_target_samples` in state. State also exposes `alignment_applied`, `profile_saved`, fitted transforms and aligned tracker poses. Phase/message transitions are recorded locally in `data/inspection/events.jsonl` and the latest transition in `session-state.json`. These private inspection files are ignored by Git. A successful regular fit saves automatically; separate validation is optional for a new fit. Loading an older profile still requires validation in the current Quest space.


## Diagnosing rejected captures

Every completed capture is saved locally under `data/captures/` before fitting, including failures. `state.capture_file` identifies it; `capture_export_error` reports a save failure. CSV records include the tracker, selected controller at adjusted sample time, selected controller at current time, other controller at adjusted time, timestamps, age, flags and selected side. These are private local recordings and are excluded from Git.

`calibration-replay.exe CAPTURE.csv [selected|current|other] [spacecal|handeye]` replays the native solver without writing or accepting a profile. It reports rotation excitation/consistency and reserved-sample residuals where available. The alternate targets are diagnostic comparisons, not automatic reference switching. The default is the upstream regular solver; `handeye` compares the original experimental solver. Reserved-sample acceptance remains required. The native tests include a smooth 100-sample capture with small pose noise, exact-transform recovery and stationary-motion rejection.


Locked Lighthouse maps use the actual --disable-calibrate 1 option and verify the effective configuration. State reports room_solver_disabled and calibration_solver. In this pinned library, globalscenesolver=0 does not disable global scene solving. The build applies patches/libsurvive-windows-steamvr-import.patch to fix Windows database file reads, unsigned 32-bit station IDs, and error propagation. The real importer is tested without starting devices. Source dependencies remain pinned, with this patch tracked explicitly.

SpaceOverride was inspected as a reference. Its current headset-mounted continuous pose-override behavior is outside this regular-calibration app scope; no SpaceOverride source was copied.

