# Launcher resume, episode sky and presentation handoffs

## Follow-up: vid1.mp4 graphics corruption

The native CPU continuation fix did not carry the PPU upload ports. Generic
PPU serialization saves visible registers and VRAM/OAM/CGRAM memory, but omits
VMAIN (increment-on-high, increment step and remapping), VRAM address/read latch,
palette write latches and scroll/Mode7 write latches. Fresh-process PPU reset
selects increment-on-low, while Doom relies on increment-on-high for subsequent
animated face and menu uploads. The next upload interleaves the wrong byte pairs
even though the initially restored VRAM is correct. Level initialization writes
VMAIN again, explaining recovery after Restart Level in the recording.

Doom's extra snapshot chunk now preserves these port settings/latches, and its
footer advances from EX1 to EX3 so incomplete states fail preflight rather than
silently resuming with reset defaults. A new native PPU test reproduces corrupted
tile words under reset defaults, verifies correct successive tile words after
restoring the ports, and resumes an interrupted palette-word write.

`resume-graphics-baseline-v1/after-restart.bmp` reproduces the distorted face
using the previous executable. `resume-graphics-fixed-v1/after-restart.bmp`
shows a correct face without restarting the level. Both runs retain the same
level, coordinates, health and weapon. The live resume regression now captures
before/after pictures and checks VMAIN's increment direction, step and remapping.
Both recorded fixed values select increment-on-high, step 1, remapping 0.
The CTest suite now contains fifteen tests.

The paused path also needs the presentation HUD/weapon cache: Doom reuses native
menu scratch tiles after pausing, so restoring just native memory can leave
status counters blank and discard the retained gun overlay. Paused snapshots
now include the complete retained HUD and decoded weapon tiles, applied after
the desktop reset and private scene reconstruction for the matching level.
Running snapshots stream only the small cache header. The renderer lifecycle
test verifies this reset/load/reconfigure handoff. The live paused test
`resume-graphics-paused-complete-v2` compares all fixed status artwork/counter
pixels at native coordinates across the 4x-to-2x restart (excluding animated
face pixels), and passes with no differences. Its resumed menu picture retains
the face, counters, gun, text and selector without a level restart.

The earlier Windows dialog from `doom_renderer_state_test.exe` followed an
invocation without the required ROM argument. That triggered its argc assertion;
the corrected invocation passed. Missing arguments now print usage and return 2
without aborting into a Windows application-error dialog.

The October 11 recordings exposed three independent faults.

* Ctrl+L relaunch saved SNES hardware/interpreter state but omitted the native
  CPU state and beam driver's resume PC/WAI state. A fresh process entered the
  reset vector with restored gameplay memory, producing the BRKs shown in
  screen4.png. Doom now saves the complete native execution residue and beam
  continuation through the framework's title-state hooks. Presentation caches
  and input/cheat/music adapters reset after restoration. States from the old
  incomplete format are rejected before they alter the machine; they cannot
  safely recover the missing CPU continuation. SRAM saves are unaffected.
* Scene capture initialized sky selection to SKY1 and relied on a native sky
  draw callback to discover SKY2. If walls hid the sky from the original camera,
  mouselook could expose a sky with the wrong texture. Capture now reads the
  native level flag at GSU RAM 0x7a, bit 1, matching `_DFL7000` in the original
  `rldrawf.a`. Wall projection and geometry are unchanged.
* Native mode flags can switch before a complete pause/map raster or GSU frame
  is available. The compositor retains the last complete enhanced picture
  through this handoff, with matching dimensions and a maximum of twelve fields.
  It never extends that deadline while holding a picture, so unsupported scenes
  cannot remain frozen indefinitely.

Validation: all fourteen CTests pass. Sky flag tests cover both SKY1 and SKY2.
Presentation tests cover widescreen at scales 1–4, padded destination rows,
allocation guards, transition retention and expiry. `validate_resume.ps1`
boots E1M1, saves in gameplay, changes the resolution mod from 4x to 2x and
resumes in a new process. Player coordinates, level and gameplay readiness are
preserved without architectural BRKs. Evidence: `resume-mod-change-final`.
The paused variant additionally exposed a missing presentation snapshot after
restart: no native BSP task runs while Doom's menu is open. Configuration now
rebuilds a private BSP snapshot from restored level RAM for that case. The
authentic GSU is untouched. The paused mod-change regression verifies the menu,
enhanced segments, zero replay failures and successful return to gameplay.
Evidence: `resume-paused-final-v2`, including `resumed-menu.bmp`.
Adapter resets belong to the desktop timeline-jump hook, so native speculative
rollback applies execution state without destroying renderer history/workers.
The `transitions-trace-final` audit captures sixteen consecutive fields per
pause, resume, map-open and map-close operation at 4x widescreen. The resulting
64-picture sheet retains its world framing through the native handoffs. These
disk captures are visual evidence only, and are not audio/performance benchmarks.

Run the live restart regression with a trace-enabled build:

```powershell
./tests/validate_resume.ps1 -BinaryDirectory ./build-lag-debug -RunLabel resume-check
```
