<!-- SPDX-License-Identifier: MIT -->
# OnePlus 9 Darwin 27.2 status

The public, sanitized status page for this fork is
[docs/STATUS.md](../docs/STATUS.md). It records only verified facts and does not contain device
serials, ECIDs, private run IDs, private artifact URLs or absolute build-host paths.

The durable technical conclusions are:

- Darwin 27.2 (24B119) reaches the lock screen in QEMU and SpringBoard on a OnePlus 9 LE2110 RAM
  boot.
- The S6SY792 touch controller, display/backlight path and EPSS CPU-frequency profile have been
  observed on the phone.
- The software-rendered UI can reach approximately 60 FPS in the recorded validation profile.
- The Adreno 660 kernel GPU path starts, but the 27.2 HakiosMetal user-space tables are still
  A635-only and reject A660. Full GPU/Metal acceleration is not complete.
- Wi-Fi, Bluetooth, cellular, ADSP/audio, camera, battery/buttons/sensors and UFS persistence are
  not verified.

See [docs/LIMITATIONS.md](../docs/LIMITATIONS.md) for the full boundary and
[docs/REPRODUCE.md](../docs/REPRODUCE.md) for the source-only reproduction path. The original
upstream laboratory notebook remains in the upstream HakiOS repository.
