# Experimental validation status

As of November 15, 2026, successful physical controller/tracker calibration has
**not** been established. Do not describe this project as validated full-body
tracking or a finished replacement for SteamVR tracking.

Verified in this session:

- Native tests pass for known room/mount transforms, smooth short movement with
  small measurement noise, stationary-motion rejection and device identity.
- The actual Windows libsurvive database importer passes CRLF, unsigned 32-bit
  base-station ID and parse-error propagation tests without device startup.
- Inspection API tests pass for device model selection, capture-speed changes
  and persistence, preview acceptance guards, frame export and API shutdown.
- Hardware poses and textured device models render through VDXR. Tracker factory
  metadata identifies Tracker 3.0; controller generation remains a labeled
  headset default when VDXR reports generic Touch.
- The Lighthouse map lock now uses `--disable-calibrate 1`, with its effective
  configuration reported through the app API. The former
  `globalscenesolver=0` setting did not disable scene solving.
- A local cached-map import, with the Windows importer fixes applied, loaded
  all four base-station poses. A 15-second probe recorded 789 poses with no
  orientation steps above 30 degrees; its maximum step was about 3.4 degrees.
  This probe alone does not establish dynamic tracking accuracy.

The subsequent 250-sample paired hardware capture still failed reserved-sample
validation: position RMS **118.57 mm**, maximum orientation error **30.89 degrees**.
No alignment/profile was accepted or applied. Replaying against current-time
controller poses produced similar error; the other controller did not provide
the required rotational movement. The underlying cause of this remaining error
is unresolved. Investigate pose timing and remaining dynamic tracking errors
using the saved recordings before requesting more captures.

The app uses an attributed port of the Hyblocker regular Kabsch/translation
solver. It retains reserved-sample acceptance checks; upstream's regular-mode
forced-application behavior is not used to turn a failed fit into success.

Raw captures, Lighthouse maps, profiles, proprietary local model imports and
inspection logs stay in ignored local data directories. They are not published.
