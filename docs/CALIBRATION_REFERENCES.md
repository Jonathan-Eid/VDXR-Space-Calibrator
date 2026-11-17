# Calibration references and acceptance

## Existing implementation

The regular solver in `src/spacecal_regular.h` is an attributed adaptation of
[Hyblocker's regular calibration](https://github.com/hyblocker/OpenVR-SpaceCalibrator/tree/797e464dae298a03217e1bbea640e03781d37b9b).
It estimates the room rotation from paired world-space rotation axes and solves
translation using device motion. A constant attachment transform is measured for
residual checks; published tracker poses keep the tracker origin.

## a-popp comparison

Reviewed [a-popp/openvr-spacecalibrator](https://github.com/a-popp/openvr-spacecalibrator/tree/2554ff78dcb8973d1b2dcddbc557c00dbea7f57f)
on November 17, 2026, at commit `2554ff78dcb8973d1b2dcddbc557c00dbea7f57f`.
Its documented regular workflow explicitly includes Ultimate Trackers: select a
reference controller and target tracker, hold them rigidly together, and collect
varied positions and rotations.

Relevant implementation details:

- `src/overlay/Calibration.cpp`, `ConvertPose` and `CollectSample`: consume
  OpenVR driver poses and apply the driver's world-from-driver rotation and
  translation to position and orientation before fitting.
- `src/overlay/CalibrationCalc.cpp`, `DeltaRotationSamples`: derive world-space
  rotation axes from relative rotations, rejecting small rotation differences.
- `CalibrateRotation`: this revision fits horizontal X/Z axes and returns yaw
  only. Its test explicitly assumes a pure-yaw room transform. Our raw HTC map
  frame has not yet been independently established as gravity aligned, so this
  assumption must be verified before adopting that restriction.
- `CalibrateTranslation`: uses differences between paired poses to solve a room
  translation despite a fixed physical offset between the two devices.

The reference's Ultimate support receives poses already converted by HTC's
OpenVR driver. It does not establish the correctness of our passive HID field
interpretation, raw basis, tracking origin, or sensor acquisition time. Those
adapter properties need their own physical validation. No a-popp source was
copied for this review; existing attribution remains in the solver and license.

## Hardware result and acceptance boundary

The first Ultimate/right Quest controller capture on November 17, 2026 collected
250 tracked pairs. The regular solver rejected it with a reserved position RMS
of 0.141961 m and a maximum orientation residual of 93.2535 degrees. The alternate
hand-eye diagnostic also rejected rotational consistency. No profile was saved
or tracking service enabled. HID receipt age is not camera-to-host latency.

Offline replay of a separate temporary copy with only the source quaternion
conjugated worsened the reserved residuals to 0.158672 m and 113.461 degrees.
That experiment does not support changing the decoder's quaternion convention.
The original capture was preserved. Selected/current controller pose columns
were identical at every sample despite nonzero HID receipt ages, so this capture
does not demonstrate effective historical-pose time adjustment. Timing,
attachment rigidity, and the raw position/orientation frame relationship remain
unresolved; no decoder or acceptance threshold was changed to force a fit.

Before accepting a new Ultimate profile, require both the solver's reserved
samples and a separate live movement validation in the current Quest STAGE and
HTC map. Changing the selected tracker/reference or recentering invalidates the
fit. Profiles and raw captures remain in ignored local data directories.
