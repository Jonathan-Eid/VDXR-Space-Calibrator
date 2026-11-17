# VDXR Space Calibrator: regular calibration app

The app opens its own OpenXR session through the VDXR build beside the executable. It renders a floor grid, controller rays, textured Quest controller models, and locally imported Vive Tracker 3.0 and VIVE Ultimate Tracker models, with the same controls available in a desktop window. Unknown devices or missing assets use generic markers.

## Device identification and models

Quest 3/3S Touch Plus, Quest 2 Touch and Touch Pro have separate left/right WebXR Input Profiles meshes and textures. Models show static unpressed buttons. A specific controller interaction profile takes priority. This VDXR backend can report a generic headset/controller profile. When the headset name does not identify the generation, automatic selection stays generic. Choose **Quest 2 / Touch** or **Quest 3 / Touch Plus** in **Device models** to match the connected controllers; this is labeled **manual override** and is a visual choice, not hardware detection. Unknown headsets/profiles stay generic.

Open **Device models** to override each hand independently. Overrides persist in `data/device-models.json`, are labeled **manual override**, and do not change calibration transforms. Returning to Automatic uses runtime identity again.

Vive Tracker 3.0 is identified from factory `model_number` or `render_model` metadata, never its serial prefix. Its HTC model is a local import in `data/models/vive-tracker-3.vdm`. `scripts/Install-TrackerModel.ps1 -PackageDirectory PATH` downloads and converts it locally without installing or running SteamVR. The build-time importer requires Python, numpy and Pillow; running the installed app does not.

In Ultimate mode, explicit selection of the Ultimate HID backend identifies the device as **VIVE Ultimate Tracker**; its visible ID remains a radio ID, not a factory serial. The official model installed by VIVE Hub can be imported to `data/models/vive-ultimate-tracker.vdm` with `scripts/Install-UltimateTrackerModel.ps1 -PackageDirectory PATH`. It detects standard VIVE Hub install locations or accepts `-SourceDirectory PATH`. It imports the installed OBJ and its MTL-referenced base-color texture, records file hashes and provenance, and leaves HTC's original files untouched. It does not download or redistribute the manufacturer mesh. An unavailable asset uses the generic marker.

Provenance and license are in `assets/models/` and [calibration references](docs/CALIBRATION_REFERENCES.md). Model selection and static textured rendering are verified offline. Model-to-physical-device grip alignment, scale and pointer interaction still require headset validation.

## Use

This app is a Nightfall/VDXR OpenXR client, not a standalone runtime. Launch it through Nightfall's per-app runtime/bootstrap profile so the packaged VDXR DLL, `LibOVRRT64_1.dll`, and `NIGHTFALL_OVR_BRIDGE` environment are supplied. It does not register or replace the global OpenXR runtime.

1. Connect the Quest to the Nightfall/VDXR streaming host with **Track Controllers** enabled. Keep SteamVR closed.
2. Launch the app through the Nightfall per-app profile. Set its Windows PowerShell child command to:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File "<package>\scripts\Start-UltimateCalibration.ps1"
   ```

   The per-app bootstrap supplies the VDXR session environment; the script selects the Ultimate source. Both desktop and controller-trigger interaction operate the same app actions. **Bring panel here** places the panel beside the current headset view.
3. Choose the Quest reference controller and Vive tracker. Trackers appear in a separately anchored **raw-space preview** before an alignment exists; that preview is not their measured position in Quest space.
4. For a new Lighthouse map, leave the tracker stationary with clear sensor visibility, wait for poses, and choose **Lock Lighthouse map**. A previously saved map loads locked. Moving base stations requires rebuilding the map; close the app and move `data/lighthouse.json` aside before reopening.
5. Attach the selected tracker rigidly to the selected controller, keeping tracking sensors uncovered. Choose **Start regular calibration**. **Fast mode** (on by default) collects **100 valid sample pairs at up to 20 Hz** (about 5 seconds with uninterrupted tracking), matching the sample budget and tick rate of [Hyblocker's regular Fast mode](https://github.com/hyblocker/OpenVR-SpaceCalibrator/blob/797e464dae298a03217e1bbea640e03781d37b9b/src/overlay/calibration.h). Switch Fast mode off for 250 samples (about 12.5 seconds). Your choice persists in `data/calibration-settings.json` and cannot change during a run. Move over different positions and rotate about multiple axes. Capture pauses when either selected pose is unavailable; poor movement still fails fitting.
6. The app uses an attributed port of the Hyblocker Space Calibrator regular rotation-delta/Kabsch and translation solver to fit the room transform, then estimates the constant mounting offset for visualization and reserved-sample measurement. It rejects insufficient motion or inconsistent data. After a fit passes, the teal ghost predicts the controller from the tracker pose; compare it with the solid Quest controller.
7. A successful fit immediately applies the alignment to the rendered tracker. It must pass motion/degeneracy and reserved-sample residual checks. A visible **CALIBRATED** banner confirms alignment; failures show a reason. The device origins retain their fitted physical mounting offset rather than being forced to exactly overlap. Lighthouse mode automatically saves a successful fit and offers an optional **Check alignment** step. Ultimate mode requires **Validate alignment** before it enables profile saving or the tracker service. In either mode, validation requires **50 fresh sample pairs** (about 2.5 seconds minimum), at least 25 cm of movement, rotation, and acceptable residuals to pass. There is no fixed 12-second minimum; inadequate movement takes longer and prompts you to move more.
8. A saved profile can start the **fixed tracking service**. This closes the calibration VR session and publishes physical tracker poses for a compatible VDXR runtime while the window stays open. The calibration transform remains fixed.

### VIVE Ultimate Tracker source

The optional passive Windows HID backend is selected explicitly with:

```powershell
./vdxr-calibration.exe --tracker-source ultimate
```

The default `--tracker-source lighthouse` keeps the existing LibSurvive/Lighthouse workflow. In Ultimate mode, VIVE Hub remains responsible for pairing and its room map; this app does not pair, unpair, rebuild, or reset that map. The adapter accepts only fresh full-pose reports and labels device identity as a radio ID because HID does not expose the factory serial or HTC map ID. It timestamps report arrival with Windows QPC; that is not the tracker camera's measurement timestamp, so capture timing must not be interpreted as sensor-exposure latency.

Ultimate calibration data is stored separately under `data/ultimate/` (profile, `stage.txt`, and `mount.txt`). The profile records source ID and explicitly marks the HTC map ID unknown. New fits and loaded profiles require validation against the current Quest and active tracker map before saving or starting the fixed publisher. Publishing uses the runtime's `Local\\VDXR.Ultimate.v1` protocol; Lighthouse profiles, transforms, and `data/lighthouse.json` stay on their original path.

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

`state` includes the mode, phase, frame/time, available actions, selections, coordinate frames, device poses/validity/age, operation progress, residuals, model identities/asset status, catalog status, and render export receipts. Preview mode is explicitly `synthetic_preview` and cannot capture or save calibration data. In `--tracker-source ultimate` preview mode, the synthetic device uses the Ultimate identity and local Ultimate asset when installed. Startup failures remain inspectable with `state` and `exit`, and are also written to `data/inspection/error.json`.

Actions are `select_reference` (0 left, 1 right), `select_tracker` (serial string), `select_role` (role index), `select_left_model` and `select_right_model` (string model ID: `auto`, `meta-quest-touch-plus`, `oculus-touch-v3`, `meta-quest-touch-pro`, `generic`), `identify_reference`, `lock_map`, `start_capture`, `fit`, `start_validation`, `stop_validation`, `save_profile`, `load_profile`, `reset`, `start_service`, `place_panel` and `exit`. They use the same guarded handlers as the UI. Unavailable actions are rejected; automation cannot bypass validation.

`show_device_models` (0/1) controls the settings view and `state.device_models_open` reports it. `state.device_models` reports each model ID, identity source, reported metadata/profile, override, asset key, load error and generic fallback; `state.model_catalog` reports each catalog asset's load state. `export_frame` accepts `mirror`, `left_eye`, or `model_catalog`. The catalog shows Quest 3 left/right, Quest 2 left/right, Touch Pro left/right, and locally installed Tracker 3.0 and VIVE Ultimate Tracker meshes using the native renderer; it is an offline asset view, not evidence of hardware detection. The acceptance response is not completion: poll `state.render.export_completed` for the returned ticket before reading the exported PNG. Only one export may be pending. Frame export writes to fixed app-owned inspection paths; it accepts no arbitrary file path. Inspect exported images directly. A timed-out mutating command must not be retried blindly; inspect the current state first.

`Test-AppInspection.ps1 -PackageDirectory PATH` checks preview mode, selection, invalid inputs, acceptance guards, native frame exports, color channels and API shutdown. The native math test separately verifies known room/mount transforms and rejects degenerate motion. The identity test covers profile precedence, uncertainty, overrides and unknown hardware; preview tests load both Quest 2 and Quest 3 assets.

## Design provenance

The regular reference/target device workflow is inspired by [Hyblocker's Space Calibrator](https://github.com/hyblocker/OpenVR-SpaceCalibrator), which publishes the Steam version. This app uses an OpenXR/libsurvive integration and an attributed port of the upstream regular rotation/translation math and contains no OpenVR driver or SteamVR dashboard dependency. Its fitting code is native C++ with Eigen; Python is not needed to operate the app.

This is the **VDXR-Space-Calibrator** client app. The VDXR runtime fork supplies the versioned pose protocol. No new public app repository or finished release has been published yet.

## Build from source

Requires Windows x64, Visual Studio C++ tools, CMake, and Git. The normal build remains the existing Lighthouse build. For an Ultimate-only app build, pass the VDXR source folder and an Eigen include root separately; this skips initializing/building LibSurvive while retaining Eigen for the shared calibration solver:

```powershell
./scripts/Build.ps1 -BuildRoot C:/build/calibrator-ultimate `
  -RuntimeDirectory C:/build/vdxr/Release `
  -UltimateOnly `
  -UltimateSourceDirectory C:/src/vdxr/tools/ultimate-trackers `
  -UltimateModelSourceDirectory "C:/Program Files/VIVE Hub/VIVE Ultimate Tracker/ViveUTOpenVrDriver/vive_ultimate_tracker/resources/rendermodels/vive_ultimate_tracker" `
  -EigenIncludeDirectory C:/deps/eigen
```

`-UltimateModelSourceDirectory` is optional; pass it to import the installed VIVE Hub model during packaging. Otherwise run `scripts/Install-UltimateTrackerModel.ps1` against the package when VIVE Hub is installed.

The package includes the selected VDXR runtime DLL, JSON manifest, and matching `LibOVRRT64_1.dll`. Personal room maps, recordings, and profiles belong in ignored data directories.



Fast mode is controlled through `set_fast_mode` (value 0 or 1), with `fast_mode` and `capture_target_samples` in state. State also exposes `alignment_applied`, `profile_saved`, fitted transforms and aligned tracker poses. Phase/message transitions are recorded locally in `data/inspection/events.jsonl` and the latest transition in `session-state.json`. These private inspection files are ignored by Git. Lighthouse fits save automatically after reserved-sample checks; a separate live validation remains optional for a new fit, while loaded profiles require validation. Ultimate fits and loaded profiles require live validation before saving or starting the service.


## Diagnosing rejected captures

Every completed capture is saved locally under `data/captures/` before fitting, including failures. `state.capture_file` identifies it; `capture_export_error` reports a save failure. CSV records include the tracker, selected controller at adjusted sample time, selected controller at current time, other controller at adjusted time, timestamps, age, flags and selected side. These are private local recordings and are excluded from Git.

`calibration-replay.exe CAPTURE.csv [selected|current|other] [spacecal|handeye]` replays the native solver without writing or accepting a profile. It reports rotation excitation/consistency and reserved-sample residuals where available. The alternate targets are diagnostic comparisons, not automatic reference switching. The default is the upstream regular solver; `handeye` compares the original experimental solver. Reserved-sample acceptance remains required. The native tests include a smooth 100-sample capture with small pose noise, exact-transform recovery and stationary-motion rejection.


Locked Lighthouse maps use the actual --disable-calibrate 1 option and verify the effective configuration. State reports room_solver_disabled and calibration_solver. In this pinned library, globalscenesolver=0 does not disable global scene solving. The build applies patches/libsurvive-windows-steamvr-import.patch to fix Windows database file reads, unsigned 32-bit station IDs, and error propagation. The real importer is tested without starting devices. Source dependencies remain pinned, with this patch tracked explicitly.

SpaceOverride was inspected as a reference. Its current headset-mounted continuous pose-override behavior is outside this regular-calibration app scope; no SpaceOverride source was copied.

