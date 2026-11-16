# Device assets

Controller meshes/textures are converted from the MIT-licensed WebXR Input Profiles assets:
https://github.com/immersive-web/webxr-input-profiles
Source commit: f4992299601614adbfefd398dc8e281556bb7444

Included profiles: meta-quest-touch-plus (Quest 3/3S), oculus-touch-v3 (Quest 2),
meta-quest-touch-pro. Each has separate left/right geometry. See LICENSE-WebXR.md.
Manufacturer marks remain their owners' trademarks and imply no endorsement.

The VDM1 files preserve GLB scene transforms, native meter dimensions, UVs and
base-color textures. They show static unpressed buttons; input animation is not
implemented. Mild baked normal shading is added for the diagnostic renderer.
Rebuild using scripts/Import-DeviceModels.py SOURCE.glb OUTPUT.vdm (Python,
numpy, Pillow). Python is not needed to run the app.

The HTC tracker asset is **not distributed here**. Install it locally from HTC's
official download (no SteamVR installation is needed):

    ./scripts/Install-TrackerModel.ps1 -PackageDirectory PATH

That script downloads https://dl.vive.com/Tracker/3D_model/VIVE_Tracker_3.0_3D.zip
and imports its textured OBJ into ignored data/models/vive-tracker-3.vdm. It
requires Python, numpy and Pillow at import time; running the app does not.
It saves source/output hashes in a local provenance receipt. Keep imported HTC
files out of public source/releases until redistribution terms are confirmed.

An absent/failed asset is reported through the inspection API and falls back to
a generic marker. Never identify tracker generation from its serial prefix.
