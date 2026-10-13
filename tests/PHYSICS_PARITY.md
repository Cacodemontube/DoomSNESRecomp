# Physics parity audit (in progress)

The target is a function-by-function audit against the original SNES source,
including player movement, line/object collision, projectiles, gravity, sector
membership and moving floor/ceiling interactions. Passing the current CTests
does not establish this target.

## Reproducible inventory

Run `powershell -NoProfile -ExecutionPolicy Bypass -File tests/inventory_physics_source.ps1`.
The output is `build-evidence/physics/source-inventory.json`: 29 original modules,
their SHA256 hashes, and internal labels with source line numbers. Every entry
starts unverified. Branch labels are not assumed to be independent functions.
Historical `old` and `a` alternatives need explicit variant analysis before use.
Enemy and moving-sector modules are included because their movement calls and
height changes can affect collisions. This inventory is a starting point, not
proof that all transitive dependencies have been found.

## Findings verified on 2026-10-13

- `rlplayer2.a` `_RLP2000` compares floor height with player Z and bypasses
  horizontal movement while falling. The adapter's `JumpAirMovement` redirects
  that check when optional jumping is active, so this option deliberately changes
  original physics. Its disabled path does not request a redirect.
- `rlmobjects3.a` gravity reads `FPSRatio`, scales the constant 16 using GSU
  fixed-point multiplication, skips floating objects and clamps Z to the floor.
  `JumpGravity` substitutes an independently integrated host jump while enabled;
  its constant 1225 and host-field time are not the original gravity algorithm.
- Crouching modifies native eye height and collision clearance through four
  hooks. Mouse turning modifies the player angle at `_RLP2100`. These paths need
  separate verification from untouched original controller simulation; enabled
  extensions cannot be described as exact original physics.
- The Release build uses generated 65816 code from
  `build-evidence/review-002/generated`, rather than inaccessible `src/gen`.
  GSU execution and all installed host hooks still require instruction/state
  comparison; use of original ROM bytes alone is insufficient proof.

## Build and rollback evidence

`cmake --build build-review-release -j 4` succeeded (already up to date).
`ctest --test-dir build-review-release --output-on-failure` passed all 15 tests.
These are baseline checks, not differential physics tests.

Before building, the current executable, assets, mods, config, keybinds, ROM
selection and review notes were copied to
`build-review-release/rollback-physics-20261013-131135`.
The backed-up executable SHA256 is
`BA3DF3C36B4A18BDE119E587D742418AD2F4BF7D9949F64000328B8766A5B027`.
Existing saves and earlier rollback directories remain in place.

## Remaining requirements

Resolve retail addresses and conditional assembly for each source routine;
audit GSU instruction semantics and host hook interactions; execute differential
fixtures for movement, walls, corners, steps, drops, object collision, projectiles
and changing sector heights; compare relevant registers, flags and RAM against
an independent original execution reference. Fix each proven mismatch and rerun
the full affected scope before claiming parity. The goal remains incomplete.

## Retail gravity arithmetic verification

`tests/test_doom_gravity.c` executes the unmodified retail ROM bytes at
`0x92bd..0x92d2` corresponding to the constant/scaling sequence and
`_RLMO2200`, `_RLMO2220`, `_RLMO2300` in `rlmobjects3.a`. An exact opcode
signature guards this address mapping. Only the fixture's return destination
is replaced with STOP; the tested instructions are untouched. The fixture
supplies the preceding routine's Z, floor, FPSRatio and saved RAM address.

The independent oracle multiplies signed FPSRatio by 16, extracts product
bits 8..23, subtracts with 16-bit wrap, and clamps using signed comparison.
All 65,536 ratio bit patterns are checked at Z=1000/floor=0. Another 1,300
cases cross 13 ratios with ten starting heights and ten floor heights,
including signed limits. All 66,836 pass. Each case checks the computed R0,
gravity decrement R6, native height store, and every other RAM byte for
unexpected writes. This tests the branch delay slot and buffered SBK store.

The whole Release suite now contains 16 tests, all passing. Build with
`C:\msys64\mingw64\bin` on PATH so compiler helper DLLs and the new test's
runtime can load. The gameplay executable SHA256 remains identical to the
verified rollback copy; this audit added test coverage without finding a
gravity mismatch requiring a gameplay change.

Coverage limits: this is a source arithmetic oracle, not an independent
hardware emulator. It does not verify the preceding object/sector/flag reads,
floating-object branch, the FPSRatio producer, timing, all final flags or
registers, or interaction with the optional host hooks. These remain open.

## Retail player falling gate and rotation

`tests/test_doom_player_gate.c` guards and executes the original `_RLP2000`
bytes at `0x81ac..0x81c0`. The retail conditional-assembly variant loads an
object pointer from `0x18c`, floor at object+34 and Z at object+14. Hooks at
the two destinations record which branch was reached and redirect to a
fixture STOP; they do not replace the comparison or its delay slot.
196,708 cases pass: every Z bit pattern against floors 0, 32767 and -32768,
plus 100 height boundary combinations. Two object locations are exercised.
The independent signed-comparison oracle checks the destination, loaded
object pointer, arithmetic registers R0/R1 and that all RAM remains unchanged.

The same test enters `_RLP2100` at `0x81c1` with no strafe bits and executes
`_RLP3000` at `0x8228` through the native angle store at `0x8245`, stopping
before `_RLP4000` at `0x8246`. The oracle multiplies signed MouseX by signed
FPSRatio, extracts bits 8..23, adds modulo 65536 and clears the low angle bit.
Zero MouseX takes the original skip branch and preserves even an odd angle.
131,584 cases pass: every MouseX bit pattern at ratio 256, every ratio bit
pattern at MouseX=64, plus 512 boundary combinations. The entire RAM image
is compared against the expected single angle update.

The Release build and all 17 CTests pass. No gameplay mismatch was found in
these slices. Coverage still excludes dead-player handling, strafe and forward
velocity computation, collision dispatch, timing, host-hook interactions and
all-state comparison against independent hardware execution. Full parity is
still unverified.

## Retail forward and strafe velocity generation

`tests/test_doom_player_velocity.c` executes `_RLP2100` through `_RLP4000`,
stopping at `_RLP5000` (`0x8296`) before use/operate handling. The fixture
uses original ROM instructions and reads the original `sincos.a` constants
independently of the GSU's ROM bus. All 1,280 words covering overlapping sine
and cosine tables match retail bank 0x4a addresses 0xe4d1 and 0xe6d1.

The oracle scales signed input by signed FPSRatio, extracts product bits
8..23, indexes by angle >> 6, multiplies by signed table entries and extracts
the high 16 bits. Velocity additions wrap to 16 bits. Left strafe takes
precedence when both strafe bits are set; strafe changes velocity without
rotating the stored angle. Native forward input adds to that velocity.

525,312 cases pass: all 65,536 angles in four input modes, all input bit
patterns for forward and right strafe with fractional ratio 257, all ratios
for forward and combined left-strafe/forward movement, and 1,024 boundary
combinations. Full RAM comparisons permit only the two expected velocity
word updates. They also check zero-input skips, ROM buffering and the strafe
branch's SBK delay-slot write. The new CTest requires the original source
directory, configured by `DOOM_REFERENCE_SOURCE_DIR`; CMake reports when
that source-dependent test cannot be registered.

All 18 Release CTests pass. The gameplay executable still matches the
rollback SHA256. No production physics change was needed for these slices.
This completes the tested arithmetic paths, not the entire player function:
input production, state flags, hardware timing, dead/use paths and downstream
line/object collisions remain unverified. The next source stages are
`rlmove.a` block-map line gathering, `rlmove2.a` line/sector candidates,
`rlmove3.a` object collision, and separate player/enemy/projectile resolution
in `rlmove4.a`, `rlmove5.a`, `rlmove6.a`.

## Attempted coordinate integration before block-map lookup

`tests/test_doom_move_coordinates.c` guards and runs `rlmove.a`'s attempted
coordinate calculation at retail GSU `0xa000..0xa01c`, before endpoint
block-map calculations. The independent oracle adds signed 8.8 velocity
shifted by eight bits to a 16.16 coordinate modulo 2^32. It checks both
fraction words, both new integer coordinates, sign-extended integer delta
registers, unchanged original integer coordinates, and the native object
pointer store at RAM `0x11c`. Full RAM comparison rejects any other write.

524,288 cases pass: each velocity bit pattern with eight fractional boundary
values, on both axes with complementary integer and velocity values. Carry,
negative fractional movement and wrapped integer boundaries are covered.
This verifies the arithmetic slice and buffered object-pointer store, not
the preceding coordinate loads or the following block-map endpoint/list
logic. No gameplay mismatch was found in this slice.
The current Release suite has 19 passing tests. The Release gameplay
executable still matches the verified rollback SHA256 recorded above.

## Block-map endpoint bounds and traversal setup

The coordinate fixture now continues through `0xa01d..0xa0b3`, covering
`rlmove.a`'s radius expansion, map-origin adjustment, endpoint clamping,
iteration counts and row stride. An observer at `0xa095` checks all four
endpoint block indices before the native code overwrites the destination
indices with iteration counts. The final observer stops before ROM block-map
address lookup. Both observers retain the original tested instructions.

131,584 source-oracle cases pass, in addition to the preceding 524,288
coordinate cases: every coordinate bit pattern with two origin/direction/map
configurations, plus 512 boundary combinations for a one-cell map. The fixture
checks the native signed-bit clamp after wrapped origin subtraction, division
by 128, radius+1 expansion, inclusive iteration counts, X step and row stride.
It also checks both attempted integer coordinates, saved radius, row width and
the entire RAM image, allowing only object-pointer/return-address stores.

The original stride MULT takes signed low-byte operands, including when a
synthetic map produces more than 127 X iterations. The oracle preserves that
instruction behavior. This is not evidence that such maps occur in the game.
Diameter cases currently cover 0, 32 and 56; dimensions cover one-cell,
16x24 and 255x255 maps. Block offset lookup, line-list deduplication and
collision response remain unverified, as do invalid zero-sized maps,
other diameter values, cycle timing and full register/flag parity.

The 19-test Release suite passes; after strengthening the endpoint observer,
the affected test was rebuilt and passed again. No production mismatch was
found in this stage. The rollback backup remains preserved.

## Block-map line candidate gathering and deduplication

`tests/test_doom_move_candidates.c` runs retail `0xa0b4..0xa11c`, starting
with the endpoint/traversal registers produced by the preceding stage. It
observes entry to `_RLMoveXY2` at `0xa132`, stopping before line/sector
classification. ROM code stays original; fixture block-map data resides in
GSU bank 0x4f. The reference model traverses rows/cells in the selected
directions and appends each unseen line in first-encounter order.

12,800 cases pass: widths 1..5, heights 1..4, all four direction combinations,
five RLLineCount markers including 0 and 65535, four premarked-line patterns,
and eight list patterns. Lists contain zero to four entries, intra-list
duplicates, cross-cell duplicates and 0xffff terminators. The fixture checks
the entire RAM image against expected line tags and 22-byte-spaced candidate
entries, including final sentinel and final list cursor. Relative block-map
offsets are based at the pointer's high-byte address after the original INC,
which the fixture explicitly models. Empty/all-premarked cases produce only
the sentinel. No candidate-gathering mismatch was found.

This validates candidate construction on synthetic data. It does not prove
retail level decoding, line IDs above 15, candidate-buffer capacity handling,
cycle timing, every architectural register/flag or downstream classification
and response. These remain part of the active full parity audit.
All 20 Release CTests pass. The gameplay executable SHA256 still matches
`rollback-physics-20261013-131135/DoomSNESRecomp.exe`.

## Sector candidates and line classification (`rlmove2.a`)

`tests/test_doom_move_sectors.c` runs `_RLMoveXY2` at `0xa132` through entry
to `_RLMoveXY3` at `0xa289`. Empty-line fixtures exercise current-sector
initialization, global counter increment/wrap, sector tagging and the
0xff80 sector-list sentinel. Direct calls to `_RLMoveXYAddMSector` at
`0xa258` exercise both append and already-tagged paths. All 256 sector IDs
and five counter boundary values are checked: 3,840 cases pass. SectorData
is 14 bytes per the original `rle.i`; floor/tag offsets are 2/10.

Another 37,632 cases run the whole classification stage with one synthetic
line: 256 sampled angles, seven flag combinations, three current/near/far
sector arrangements and seven signed height combinations. Fixtures check
vertex decoding, line flags, perpendicular sine/cosine components, trigger
count, near/far sector membership, highest floor, lowest floor, lowest ceiling,
candidate structure contents and both list sentinels by comparing all RAM.
Solid lines contribute only their near sector and leave transparent height
fields untouched. Already-included current sectors are not appended again.
All cases pass without a production mismatch.

The line oracle uses the retail table words independently of GSU GETB reads;
their match against all original assembly constants is separately verified
by `doom_player_velocity`. Synthetic line/vertex/face data lives in bank 0x4f.
Coverage currently has one line at a time, line ID zero, fixed vertex values,
sampled angles and distinct near/far sectors. Multi-line combinations,
other face/vertex offsets, aliasing near/far sectors, all trigger masks,
timing and all final architectural state still require verification.
This stage prepares collisions; it does not test collision response.
All 21 Release CTests pass; the gameplay executable and rollback backup
retain the identical SHA256 recorded above. Full parity remains unproven.

## Object collision candidate filtering (`rlmove3.a`)

`tests/test_doom_object_filter.c` enters `_RLMoveXY3` at `0xa289` and executes
sector and object iteration, collision-mask filtering, self-exclusion,
fixed/movable coordinate reads and the distance checks through `0xa2f5`.
An observer at `_RM3XY4000` (`0xa2f6`) records each accepted object's pointer,
coordinates and deltas, then resumes the original linked-list iterator at
`0xa2a8`. Geometry construction is explicitly excluded from this fixture.
The native end-of-sector path writes the sentinel and jumps through MVCode
to a fixture STOP. Tested code remains unmodified retail instructions.

278,528 source-oracle cases pass: every X delta and every Y delta for both
fixed and movable objects, plus 64 distance-boundary pairs across all 256
collision-mask values. Fixtures contain the moving object itself, an object
with zero flags, and the candidate, exercising self/flag rejection before
candidate processing. Fixed objects read coordinates from synthetic bank
0x4f level data; movable objects read their RAM coordinates.

The source gate accepts wrapped signed deltas from -256 through 255 on both
axes; -257 and +256 are rejected. The oracle verifies accepted count, pointer,
coordinates, delta registers and the entire RAM image. Coordinate wrap at
signed/unsigned limits is included. No filtering mismatch was found.

Coverage excludes multiple candidates/sectors in one fixture, malformed
linked lists, alternate flag combinations on objects, geometry/angle
construction, cycle timing and all final registers/flags. Accepted-object
geometry and downstream collision response remain unverified.
All 22 Release CTests pass. The Release gameplay executable still matches
the preserved rollback copy's SHA256; no production change was needed.

## Object-to-object angle (`rlmove3.a`)

`tests/test_doom_object_angle.c` executes `_RM3XY4000` (`0xa2f6`) through
the angle computation, observing R0 before `_RM3XY5000` (`0xa393`) begins
the perpendicular table lookup. An independent source oracle uses absolute
delta ratios, the original reciprocal/partial-arctangent tables, and explicit
quadrant reconstruction. All 262,144 pairs in the filter's admitted domain
[-256,255] x [-256,255] pass, including coincident objects, cardinal directions,
equal-magnitude diagonals and the reciprocal precision transition at 128.
Every RAM byte remains unchanged during this slice.

Before running, the test verifies all 32,768 original InverseTable.bin bytes
against retail bank 0x34, and all 1,024 PArcTanTable words parsed directly
from sincos.a against retail bank 0x4a. Thus GSU table reads and the oracle
do not share the same memory-reading implementation. The oracle preserves
the original integer approximation, not mathematical atan2 rounding.
This source-dependent test reports at configure time if its original table
files are unavailable and it cannot be registered.

No angle mismatch was found. Geometry vertices, object diameter/flags,
collision response, hardware cycle timing and full register/flag equivalence
remain unverified. Exact angle parity on this complete admitted delta domain
does not establish full physics parity.
All 23 Release CTests pass. The complete verbose test evidence is preserved
in `build-evidence/physics/release-ctest-log.txt`. The gameplay executable
and rollback backup still have identical SHA256 hashes.

## Collision-plane vertices, diameter and flags (`rlmove3.a`)

The object-angle fixture now also executes `_RM3XY5000` at `0xa393` through
the constructed object's collision-plane entry, observing return to the
iterator at `0xa2a8`. The oracle computes perpendicular (-cos, sin), native
signed multiplication by object diameter with high-word extraction, shifted
center and both vertices using wrapping 16-bit arithmetic. It compares all
RAM, including object pointer, perpendicular vectors, vertices, flags,
sector-object-list pointer and item-trigger byte. Untouched entry fields
must retain their initial bytes. Final candidate cursor must advance by 22.

196,608 cases cover every angle with three flag/coordinate configurations,
cycling through all 70 object types. `tests/verify_object_diameters.ps1`
independently resolves original odd* constants from rlobjects.i and the
object records in objdata.a: all 70 diameter bytes match retail ROM. Evidence
is saved in `build-evidence/physics/object-diameters.json`. Sine/cosine
constants are already source-verified by the velocity oracle. No geometry
mismatch was found. Type/angle combinations are sampled, not exhaustively
crossed; multiple collision planes, all flag combinations, cycle timing,
all-state reference execution and response routines remain open.

During this extension, the filtering fixture's resume address was corrected
from 0xa2a7 to the true iterator entry 0xa2a8. The earlier address executed
an extra INC R8. Its RAM assertion missed this because the subsequent GSU
word store pairs bytes via XOR; the fixture now checks R8 explicitly. All
278,528 filtering cases pass again. This was a test-fixture correction,
not a production physics change.
All 23 Release CTests pass with the geometry extension and corrected filter.
The saved test log is updated, and executable/rollback SHA256 hashes still
match. Full parity remains unproven; collision response is the next stage.

## Player step and ceiling clearance (`rlmove4.a`)

`tests/test_doom_player_clearance.c` guards every byte and executes
`_RM4XY2700` at `0xa512` through the MVBlock store, observing `0xa535` before
movement adjustment. It supplies the original starting player Z and line's
highest floor/lowest ceiling. The source oracle checks the signed wrapped
step delta against 25, ceiling against wrapped Z+56, and signed wrapped
floor-to-ceiling clearance against 56. It expects zero for a blocking line
and `(highFloor*2+1) mod 65536` for a passable line, preserving the original
WITH-prefix/branch-delay-slot behavior that encodes this status.

393,945 cases pass: every Z bit pattern crossed with six boundary scenarios
(24/25-unit steps, 55/56-unit standing clearance, passage-height boundary and
a downward step), plus 729 signed boundary triples. Entire RAM comparisons
allow only the native status-word update; R0 is also checked independently.
No clearance mismatch was found. The test does not exercise the optional
crouch hook, line-side determination, line flags before this entry, movement
intersection/deflection after it, timing or all final architectural state.
Those paths remain part of the active collision-response audit.
All 24 Release CTests pass. The preserved full test log includes the new
clearance cases, and Release/rollback executable hashes still match.

## Player line-side dot product (`rlmove4.a`)

`tests/test_doom_line_side.c` guards all tested bytes and executes
`_RM4XY4000` at `0xa5f0` through its native return. The source oracle multiplies
signed X/Y deltas by signed perpendicular components, adds both products
modulo 2^32, and extracts the high word. The original retains that word in
R2 and increments negative high words in R1, even when the product has an
exact integral value. This rule is preserved rather than substituting a
mathematical sign test or generic rounded dot product.

395,617 cases pass: all coordinate bit patterns against six axis/diagonal
vectors, plus 2,401 boundary combinations of coordinates and perpendicular
components. The fixture checks both side registers, the X product's high/low
words, final multiplier register and every RAM byte. It includes low-word
carry into the high sum, wrapped signed sum, negative rounding and the WITH
prefix in the conditional branch delay slot. No side-helper mismatch was
found. Line/segment intersection decisions, radius adjustments, movement
percentage, deflection, timing and full final architectural state remain
unverified; this arithmetic helper alone does not prove collision response.
All 25 Release CTests pass, with the verbose log preserved at the existing
evidence path. Release and rollback executable SHA256 hashes still match.

## Collision-clipped coordinate update (`rlmove4.a`)

`tests/test_doom_clipped_move.c` guards all 67 retail bytes and executes
`_RM4XY8200` at `0xa607` through both coordinate writes, stopping at `0xa64a`
before the full-movement/deflection branch. The independent source oracle
extracts signed product bits 15..30 for FMULT/ROL, subtracts applied velocity
from the remainder, and adds signed 8.8 applied movement to 16.16 coordinates
modulo 2^32. Native rounding at percentage 0x7fff is preserved; the test
does not substitute exact whole-velocity movement for this fraction.

327,680 cases pass: all percentage bit patterns with two positive/negative
velocity pairs, and all velocity bit patterns at percentages 0, 0x4000 and
0x7fff. Coordinates include fractional carry and signed/unsigned wrap
boundaries. Assertions check both remaining velocity registers, integer
coordinate registers, object pointer, percentage register and the entire
RAM image, allowing only the four native coordinate-word updates. No update
mismatch was found. Computing the collision percentage, the subsequent
sliding projection/rescan, height/sector updates, cycle timing and full
architectural reference comparison remain unverified.
All 26 Release CTests pass; the updated full test log is preserved. The
gameplay executable and rollback backup still have identical SHA256 hashes.

## Sliding projection and rescan dispatch (`rlmove4.a`)

`tests/test_doom_slide.c` executes projection from `0xa64f` through the
remaining-movement dispatch. The source oracle preserves two separately
quantized signed products, subtracts their 16-bit magnitudes modulo 65536,
then projects along (PUY, -PUX) using native FMULT/ROL extraction. It expects
both projected velocity words in native object RAM and MVVX/MVVY, and checks
the corresponding registers and every other RAM byte.

786,648 cases pass: every velocity bit pattern paired with its complement,
six axis/diagonal vectors, both ordinary rescan counter values, plus 216
signed counter/velocity boundary combinations. Counter decrement and signed
dispatch are modeled exactly, including wrap. Observers stop at `_RM4XY1000`
(`0xa47a`) for a rescan, or `0xa69e` on the exhausted path before its zero
velocity stores. The preceding `0xa69d` instruction is also executed as the
rescan jump's delay slot, so observing it cannot distinguish destinations.
The fixture was adjusted accordingly before accepting results.

No projection/dispatch mismatch was found. This fixture does not execute the
subsequent rescan or exhausted-path reset/height update. Collision percentage,
end-to-end line intersections, multiple contacts, cycle timing and all-state
reference execution remain unverified. Only six perpendicular vectors are
tested, so complete table-vector combination coverage is also still open.
All 27 Release CTests pass; the complete test log is updated. The gameplay
executable still matches the preserved rollback backup's SHA256.

## Exhausted-movement cleanup and floor/ceiling writes (`rlmove4.a`)

The slide fixture now additionally follows projection with counter zero
through `_RM4XY9500` and `_RM4XY9600`, stopping at `0xa6b6` before sector
membership updates. All 25 cleanup bytes at `0xa69d..0xa6b5` are guarded.
393,216 cases pass: all velocity bit patterns and six axis/diagonal vectors,
with the same bit patterns independently supplied as floor/ceiling values.
The full RAM oracle expects projected MVVX/MVVY, zero native object VX/VY,
and the supplied MVIZ2/MVIC2 written to object offsets 34/36. The odd-address
GSU word store used to clear VX is verified through its paired bytes. Final
object pointer and projected velocity registers are also checked.

No cleanup mismatch was found. This establishes these stores after exhausted
projection, not sector unlink/relink, BSP sector selection, the actual rescan,
collision percentages or end-to-end response. The height pairs include
synthetic signed limits and need not describe a valid physical sector.
Timing and full architectural reference comparison remain unverified.
All 27 Release CTests pass with the cleanup extension; the complete test log
is refreshed. Release and rollback executable hashes still match.

## Sector object unlink/relink (`rlobjects2.a`)

`tests/test_doom_sector_links.c` executes `_RLRemSectorObject` at `0x8d7f`
and `_RLAddSectorObject` at `0x8d3d`, with original code and native returns.
The independent linked-list model checks removal from the head, interior,
tail and only-node positions, successor Prev updates, insertion into empty
and populated destinations, and predecessor/successor/head consistency.
Every RAM byte is compared after each routine. Removed objects retain their
own old link fields until insertion, as the source specifies; the oracle
does not add clearing stores absent from the original.

55,296 migrations pass: all 256 sector IDs, source lists of lengths 1..8,
every source position and five destination lengths (46,080 cases), plus
same-sector removal/reinsertion at the head (9,216 cases). All object addresses
and sector IDs remain explicit in the fixtures. No list-update mismatch was
found. The fixture supplies the resulting sector ID between calls; it does
not substitute this for proof of native BSP selection.

Fixed-object removal, malformed lists/diagnostic conditional variants,
larger lists and alternative address layouts, native BSP selection,
end-to-end collisions, cycle timing and all final architectural state remain
unverified. These list operations alone do not establish full physics parity.
All 28 Release CTests pass. The full test log is updated; Release and rollback
executable SHA256 hashes still match.

## BSP area, sector and sector-data selection (`rlobjects2.a`)

`tests/test_doom_bsp_sector.c` executes `_RLGetXYArea` at `0x8ce3`,
`_RLGetXYSector` at `0x8cc9` and `_RLGetXYSectorData` at `0x8cb3` with native
returns. Synthetic three-level trees reside in GSU bank 0x4f. Node records
are 28 bytes (rle.i/ripdoom.i), with child offsets 16 and 26. An independent
tree oracle computes wrapped signed coordinate differences, multiplies them
by partition deltas, subtracts modulo 2^32, and chooses the left child when
the wrapped cross-product sign is nonnegative. It follows encoded children
until an area leaf and models the original area-base addition/sector lookup.

393,510 cases pass: every X bit pattern with complementary Y, three lookup
entry points, two trees with shifted origins/vectors, plus 294 coordinate
boundary combinations. Checks include returned area pointer or sector ID,
sector-data pointer, unchanged input coordinate registers and every RAM
byte. Axis, signed-limit and diagonal partition vectors and multi-level
branches are exercised. No traversal/lookup mismatch was found.

The initial fixture incorrectly used a 34-byte node and right-child offset
32; source layout inspection corrected it to 28/26 before accepting results.
This was a fixture error, not a production mismatch. Coverage still excludes
actual decoded retail level trees, arbitrary topology/depth, every coordinate
pair/vector combination, malformed trees, hardware cycle timing and all
architectural state. Sector lookup in these fixtures alone does not establish
end-to-end collision parity.
All 29 Release CTests pass. The full evidence log is refreshed, and Release
and rollback executable SHA256 hashes still match.

## Player response integration with native return

`tests/test_doom_player_pipeline.c` enters `_RLMoveXY4` at `0xa42f` and
executes through original BSP lookup, sector removal/insertion, trigger-count
exit and caller return. No routine is redirected or substituted. A read-only
observer counts actual entries to the native rescan. The fixture supplies
candidate records (earlier candidate-building stages remain separate), a
one-node BSP and an existing destination-sector object.

512 no-contact cases cross eight velocity values on each axis with eight
coordinate/fraction boundary configurations. Another 35 solid-wall cases
cross seven inward velocities with five tangential velocities against a
vertical wall. The source oracle expects blocked normal motion, separately
quantized tangential projection, actual second scan, final coordinate/velocity
words, floor/ceiling writes, sector ID and fully consistent object-list links.
All cases pass. The complete native return and R0/R12 outputs are checked.

RAM is compared outside movement's temporary work-variable range
0x11a..0x153; meaningful movement percentage is asserted separately. This is
explicitly weaker than all-state differential parity and does not conceal
that gap. The oracle preserves the oriented collision vector: rlmove4 negates
stored PUX before caching it, which changes intermediate integer rounding.
The initial fixture oracle used the stored orientation and was corrected
against that source instruction before accepting sliding results.

These are integrated response cases, not the full physics chain: arbitrary
wall orientation, multiple contacts/corners, two-sided steps, objects/items,
enemy/projectile response, actual level data, temporary-register/flag parity
and hardware timing remain unverified. Full parity is still unproven.
All 30 Release CTests pass. The integrated-case output is preserved in the
full test log; Release and rollback executable hashes still match.

## Integrated two-sided passages and corners

The native player-pipeline fixture now adds 280 two-sided passage cases:
eight high floors (-24 through 26), seven ceilings (55 through 128), and
five tangential velocities. The source oracle permits steps up to 24 units,
requires both standing and floor-to-ceiling height of 56, and models passable
floor/ceiling updates or blocked projection/rescan accordingly. Native Z stays
unchanged here; the mover updates the floor/ceiling bounds for later gravity.
All 280 cases pass through BSP selection, relinking and native return.

Another 98 cases check two perpendicular solid walls, seven velocities on
each axis and both candidate orders. Both walls span the player's position.
The oracle expects no coordinate displacement, zero object velocities after
the exhausted second scan, preserved floor/ceiling bounds, correct sector
links and final movement percentage zero. All pass with exactly two scans.
The initial corner fixture reused a vertical segment outside the player's
Y range; its endpoints were corrected before accepting corner results.

Together with the earlier 547 cases, the fixture has 925 integrated response
cases. As before, temporary movement work variables are excluded from the
whole-RAM comparison, with meaningful outputs checked separately. This is
not full-state differential parity. Arbitrary geometry, object contacts,
enemy/projectile response, real level data, cycle timing and all-state
reference execution remain open; the complete goal is still unproven.
All 30 Release CTests pass with passage/corner integration. The saved full
log is updated, and executable/rollback SHA256 hashes still match.

## Enemy clearance and complete response (`rlmove5.a`)

`tests/test_doom_enemy_clearance.c` guards every tested byte and executes
`_RM5XY2600` at `0xa78d` through its decision. The independent source oracle
preserves wrapped signed step/height comparisons and the enemy-specific
cliff rejection: high-floor minus low-floor >=25 blocks movement. Height is
56 in this original path, not a substituted per-monster height. Observers
distinguish continuation (`0xa721`) from blocking (`0xa7b5`), after the shared
prefix instruction used in the continuation jump delay slot.

395,617 cases pass: every Z bit pattern with six step/ceiling/cliff boundary
scenarios, plus 2,401 signed height quadruples. All RAM must stay unchanged.
This establishes the clearance decisions, not all surrounding line filters.

The integration fixture additionally enters `_RLMoveXY5` at `0xa6d0` and
executes its native return in 827 cases: 512 free movement cases, 280 passage
cases, and 35 solid-wall cases. Successful movement checks coordinates,
unchanged velocity/height fields, native BSP selection and sector relinking.
Blocked enemies retain coordinates, velocities and original sector links;
movement percentage is zero and no player rescan is entered. The expected
outcomes follow the source's all-or-nothing enemy response, not player sliding.
The integration fixture excludes the same documented temporary RAM range as
its player cases. Passage integration currently sets low floor equal to high
floor; cliff comparisons are covered by the separate decision fixture.

The normal Release build succeeds. The affected enemy-clearance and extended
integration tests pass, including all existing player integration cases.
Evidence is preserved in `build-evidence/physics/enemy-response-ctest-log.txt`;
the earlier full 30-test log remains separately preserved. The newly expanded
whole suite has not been rerun in this step. Gameplay/rollback executable
hashes still match; no production mismatch was found. Projectile response,
object contacts, actual level data, broader geometry, timing and complete
architectural reference execution remain unverified.

## Projectile clearance and response (`rlmove6.a`)

`tests/test_doom_projectile_clearance.c` guards all tested instructions and
executes `_RM6XY2600` at `0xa999`, observing continuation at `0xa931` or
blocking at `0xa9b7`. Its independent oracle requires floor < projectile Z
(equality blocks), ceiling >= wrapped Z+16 and signed wrapped passage height
>=16. All 393,559 cases pass: every Z bit pattern in six height scenarios,
plus 343 signed boundary triples. Every RAM byte stays unchanged.

The response integration fixture now also enters `_RLMoveXY6` at `0xa8c9`
through native return. 727 cases cover free movement, solid walls and
two-sided passages with Z=32 and zero vertical velocity. Successful motion
checks coordinates, BSP lookup and list relinking. Blocking preserves
coordinates, velocities and original links, with no player rescan and zero
movement percentage. The original projectile height of 16 is preserved.

Another 131,072 integrated cases exercise every signed vertical-velocity bit
pattern with native FPSRatio=256 and fractional Z of 0 or 65535. The oracle
extracts scaled product bits 7..22 (MVVZ), then models FMULT/ROL movement and
16.16 Z addition. Final integer Z, fractional Z and all non-temporary RAM are
checked together with the normal native return/relink outcomes. These are
no-contact fixtures; some velocity patterns describe synthetic unbounded
flight, not a valid retail gameplay situation. No mismatch was found.

Release builds succeed. Both new clearance and extended integration tests
pass; the final extended pipeline run also rechecks all player/enemy
integration cases. Evidence is in
`build-evidence/physics/projectile-pipeline-ctest-log.txt`. Earlier full-suite
and enemy evidence remains preserved. The whole expanded suite was not rerun
in this step. Release and rollback executable hashes still match.

Nonzero vertical velocity at contacts (including the separately predicted
integer collision Z), other FPSRatio values/fractions, projectile damage,
object targets, actual level data, all temporary state and hardware timing
remain unverified. Full physics parity remains unproven.

## Moving-Z projectile contacts and collision prediction

The integrated projectile fixture now models the separate native collision
Z prediction: starting Z plus signed product bits 15..30, with 16-bit wrap.
Clearance decisions use that predicted Z, not the final fractional coordinate.
Scaled MVVZ uses product bits 7..22. Both scratch words are now explicitly
checked in every projectile case, narrowing the prior scratch-state exclusion.

14,256 new contact cases pass: twelve signed vertical velocities, eleven
FPSRatio bit patterns (including fractional, zero and signed limits), three
initial Z fractions, six floors and six ceilings. Successful contact passage
updates 16.16 Z through original movement/BSP/relink/native return; rejected
passages preserve original coordinates and sector links. The independent
oracle preserves the distinction between collision prediction and final
movement quantization. The previous 131,072 vertical-integration cases and
all existing player/enemy/projectile integration cases also pass with the
new scratch-word assertions.

The normal Release build succeeds and the affected integration CTest passes.
Its evidence log is refreshed at the existing projectile pipeline path.
The expanded whole suite was not rerun in this step. Release/rollback
executable hashes still match; no production mismatch was found.
Exhaustive velocity-ratio combinations, other contact geometries, object
targets/damage, real gameplay captures and full state/timing parity remain
unverified. The overall goal is still incomplete.

## Native object scan/geometry/response integration

`tests/test_doom_object_contact.c` starts at `_RLMoveXY3` (`0xa289`), scans
the native sector object list, constructs the actual collision candidate,
then dispatches directly into player, enemy or projectile response and follows
the original return. No tested instruction is redirected or substituted.
Only the player rescan observer is installed, and it reads without mutating
GSU state. Both fixed level-data and movable RAM-coordinate paths are used.

420 cases pass: three mover kinds, fixed/movable targets, seven inward
velocities, five tangential velocities and two collision-mask settings.
Eligible soldier-type obstacles use the source-verified diameter 40; the
independent oracle checks the entire generated 22-byte candidate and sentinel,
including object pointer, perpendicular vectors, vertices and sector pointer.
Player contacts block normal motion and project/rescan tangent motion;
enemy/projectile contacts block without relinking. Mask-excluded targets
produce only a sentinel and unobstructed movement. Successful moves verify
native BSP selection and sector relinking while retaining the obstacle in
its original sector list.

Every RAM byte outside the documented movement temporary range is compared,
with final percentage, object pointer, return result and rescan count checked
separately. All cases pass, including candidate rejection and the linked
object's Prev update on relink. The normal Release build succeeds; the new
affected CTest passes. Evidence is in
`build-evidence/physics/object-contact-ctest-log.txt`. Release and rollback
executable hashes still match. Earlier full-suite logs remain preserved;
the expanded whole suite was not rerun in this step.

This connects existing filtering/geometry and response audits but still uses
one obstacle on one axis with synthetic sector/BSP data. Item pickup, damage,
other object types/relative angles, multiple contacts, native block-map stages,
real gameplay traces, all temporary state and timing remain unverified.
The full objective remains incomplete.

## Item pickup eligibility and contact/refusal integration

`tests/test_doom_item_gate.c` guards and executes `_RM7XY7000` at `0xabf3`
through continuation (`0xab08`) or item dispatch (`0xac02`). Its oracle
preserves the original wrapped DEC/BMI tests for item solidity and PlayerDead,
including synthetic sign limits. 196,608 cases pass: every flag word with
live/dead player values, and every PlayerDead word with ordinary item flags.
All RAM stays unchanged in this eligibility slice.

The connected object-contact fixture adds 28 item cases with fixed/movable
soldier-type dropped-ammo items, seven crossing velocities and live/dead
players. The native scan generates item geometry, movement ignores its
solidity, the real trigger routine tests strict integer side changes, and
the pickup gate/dispatch execute without redirects. Read-only observers
assert actual gate/dispatch/clip entry counts: velocity 18 ends on the plane
and does not trigger; velocities 19..24 cross it. A live player reaches the
original `_IClip` at `0xb230`; full ammo (200) rejects the pickup. Dead players
never reach item dispatch. Items, ammo and object flags remain intact, while
movement and sector links update normally. Every non-temporary RAM byte is
compared. All 28 cases and the previous 420 object cases pass.

The explicit entry counters were added before accepting the integration
results, because unchanged inventory alone cannot prove that pickup handling
was reached. Full-ammo rejection and the dead-player gate are established;
successful inventory gain, pickup removal, messages/sound, other item types,
broader geometry and all state/timing parity remain unverified.

The Release build succeeds; new gate and extended object-contact tests pass.
The final contact log is preserved at
`build-evidence/physics/item-contact-ctest-log.txt`; prior evidence remains
separate. The expanded whole suite was not rerun in this step. Release and
rollback executable hashes still match. The full goal remains incomplete.

## Fixed-item pickup unlink and clip/ammo-box inventory effects

The sector-link fixture now enters `_RLRemSectorObject2` directly at
`0x8d8b`, as `_RM7XY8800` does after fixed-item pickup. It supplies the sector
head address in R1 rather than reading a movable object's sector byte.
139,264 cases pass: every flags/type word at an interior list position,
plus all sector IDs, lists of lengths 1..8, every removal position and eight
flag/type boundaries. Full RAM comparison verifies predecessor/head and
successor Prev updates while preserving the removed item's own links and
flags/type. The separate destruction entry `_RLRemSectorFObject` clears
active/solid/target flags; pickup does not call that entry. No extra clearing
behavior was substituted for the original.

`tests/test_doom_ammo_pickup.c` guards every byte of `_IClip` (`0xb230`),
`_IAmmoBox` (`0xb235`) and their shared update path. 262,144 cases pass:
every ammo word, backpack off/on and both increments (5/50). The independent
oracle preserves signed wrapped comparisons and addition/clamping, with
normal capacity 200 or backpack capacity 400. All RAM is compared, permitting
only the expected bullet-word store. Successful routines return natively;
rejected pickups reach the real trigger-loop destination, where an observer
stops before unrelated caller continuation. Synthetic signed ammo values
verify the native arithmetic, not plausible inventory ranges.

Release builds succeed. The affected sector-link test passes its new cases
and the prior 55,296 migrations; the new ammo CTest passes. Ammo evidence is
preserved at `build-evidence/physics/ammo-pickup-ctest-log.txt`. The whole
expanded suite was not rerun in this step. Release/rollback executable hashes
still match; no production mismatch was found. Full successful pickup
integration through messages/sound/removal, other item effects, damage,
real gameplay traces and all-state/timing parity remain unverified.

## Fixed-object destruction (`rlobjects2.a`)

The sector-link fixture now executes `_RLRemSectorFObject` at `0x8d6e`
through its shared native unlink and return. Every destruction-prefix byte
is guarded. Its independent RAM oracle clears exactly mask 0x00a8
(active/solid/target flags), preserves the type byte and all other flags,
updates the predecessor/head and successor Prev, and leaves the destroyed
object's own link fields unchanged. R12 starts at a different pointer;
the return must contain the target pointer transferred from R13.

139,264 cases pass: all flag/type words at an interior list position,
plus all 256 sector IDs, list lengths 1..8, every removal position and
eight flag/type boundaries. Full RAM comparison permits only the specified
flag word and list-link updates. The affected CTest also rechecks all
55,296 migrations and 139,264 pickup-unlink cases, preserving their distinct
flag behavior. No destruction mismatch was found.

The normal Release build succeeds. The affected test passes; its complete
log is preserved in `build-evidence/physics/sector-lifecycle-ctest-log.txt`.
Release and rollback executable hashes still match. The whole expanded
suite was not rerun in this step. Damage callers, movable-object destruction,
processing-list maintenance, larger/address-varied lists, real gameplay,
all registers/flags and hardware timing remain unverified. Full parity is
still unproven.

## Movable-object freeing, corpse retention and item conversion

`tests/test_doom_movable_lifecycle.c` executes three original `rlmobjects2.a`
entries through native return: `_RLMAEnemyDie2`/projectile removal at `0x90fd`,
retained-corpse `_RLMAEnemyDie` at `0x9129`, and soldier/sergeant item conversion
at `0x90f3`. Opcode guards map each entry. The independent lifecycle oracle
checks the native sector-list unlink and free Next marker only for removal;
retained corpses/items keep their sector membership. All paths decrement
RLNumPMObjs, clear flag mask 0x28 and mark the advanced processing cursor's
previous slot 0xffff. Removal also decrements RLNumMObjs; item conversion adds
flag 0x10 before common death handling. Type and remaining flag bits persist.

334,923 cases pass: all flag/type words in all three paths, all 256 sector IDs,
list lengths 1..8, every list position and five flag/type boundaries, plus
75 counter-wrap boundary cases. Every RAM byte is compared. Object pointer,
processing cursor and native return result are asserted. No lifecycle
mismatch was found. The fixture supplies valid list links and an already
advanced processing cursor, rather than replacing the animation iterator.

The Release build succeeds and the affected CTest passes. Its complete log
is in `build-evidence/physics/movable-lifecycle-ctest-log.txt`. Release and
rollback executable hashes still match. The expanded whole suite was not
rerun in this step. Animation-command callers, radius-damage/attack queues,
subsequent processing-list scans, corpse/item rendering, other address
layouts, full architectural state and timing remain unverified. Full parity
remains unproven.

## Processing-list iterator and native animation/lifecycle commands

`tests/test_doom_animation_iterator.c` enters `_RLMAObjects` at `0x9028`.
11,264 selection cases verify encoded object pointers, freed-slot skipping,
held (negative-byte) counts, wrapped timer decisions, advance selection and
native attack/radius-list terminators. They cross all counter bytes, eleven
frame-step values and four hole patterns. A selection observer redirects
before command execution in this first phase only; full RAM is compared.

10,240 further cases execute the original image-command and animation-jump
instructions on a synthetic duration/image stream (3, 5 and 127 ticks,
plus a jump back to its start). The observer is read-only in this phase.
An independent timeline oracle checks image byte, next animation pointer,
remaining count and every RAM byte, including exact-duration boundaries and
multiple image advances. Ten frame-step values and four hole patterns are
crossed with every initial counter byte.

240 connected cases remove the selection observer entirely. Original call
commands invoke freeing, corpse retention or soldier-item conversion, return
to the native animation loop, save an image/count and continue across the
processing list. Fixtures cross one to four objects, four hole patterns,
three lifecycle entries and five flag/type words. Read-only lifecycle entry
counters verify the actual calls. Full RAM comparison checks processing slots,
sector links, object counts, flags/types, image/pointer/count stores and list
terminators. Freed objects disappear from the list; corpses/items retain
their sector links. All cases pass without a production mismatch.

The Release build succeeds and the affected CTest passes all three phases.
Its complete log is preserved at
`build-evidence/physics/animation-iterator-ctest-log.txt`. Release and rollback
hashes still match. The expanded whole suite was not rerun in this step.
Other animation commands/streams, attack/radius queues, enemy targeting after
the iterator, real gameplay, full register/flag state and timing remain
unverified. The full objective is still incomplete.

### Native enemy attack queues

The retail `_RLMAQEnemyAttack` (0x9092) and `_RLMAQEnemyDAttack`
(0x90a5) now have independent source-oracle coverage in
`test_doom_animation_queues.c`: 262,144 cases cross every target-distance
word, both routines and available/full queue cursors. Every RAM byte,
queue cursor and animation pointer is checked. The distance branch uses
signed comparison against 64, including overflow-sensitive values.
Full queues consume their animation operands without writing an entry;
available queues append the object and selected attack pointer.
Retail instruction bytes are checked before execution. Synthetic attack
words are enqueued, not executed. These cases do not establish attack
execution, radius damage, arbitrary queue positions, full register/flag
state, timing or gameplay parity.

Release rebuild and the affected CTest pass. Evidence is saved in
`build-evidence/physics/animation-queues-ctest-log.txt`. No production
mismatch was found in these routines. The full objective remains incomplete.

### Radius queue and connected explosion removal

983,040 additional cases execute `_RLMAMissileDie`/`_RLMABarrelDie`/
`_RLMABFGDie` at their shared retail entry 0x90ca through native object
removal and return. Every damage word is crossed with all 14 available
queue positions and the full cursor. Two different bijections of the
sweep exercise every X and Y coordinate word. An independent oracle
checks every RAM byte, radius cursor, restored animation pointer and
removal return value. Queued entries contain damage/X/Y in source order;
a full queue appends nothing, and both paths free/unlink the object,
clear its active flags, decrement both counts and clear its processing slot.
Retail entry bytes are verified. Fixtures use a one-object sector and
synthetic MObjData in the inherited ROM bank. Multi-object removal was
covered separately; this test does not execute the radius-damage consumer
or prove all coordinate combinations, flags, registers, timing or gameplay.

Release rebuild and the combined queue CTest pass (262,144 attack cases
plus 983,040 radius/removal cases). The queue log was refreshed at
`build-evidence/physics/animation-queues-ctest-log.txt`. No production
mismatch was found. Full parity is still unproven and the goal stays active.

### Attack queue coverage expansion

The attack sweep now crosses both routines and all 65,536 distance words
with every valid queue cursor (20 entry positions plus full), totaling
2,752,512 cases. Distinct near/far pointer words vary with a bijection of
the distance sweep; the simple-attack path enqueues every possible pointer
word at every available position. Distance-dependent selection exercises
both operands with varying low/high bytes, but does not exhaust the full
Cartesian product of distances and pointer words. Every RAM byte and both
queue/stream cursors remain checked. The combined CTest passes these cases
and the 983,040 radius/removal cases after a successful Release rebuild.
Its preserved log is refreshed. Earlier queue case counts describe earlier
coverage; the current attack count supersedes them. No production change
was required. Actual attack execution and radius-damage application remain
next audit gaps; full physics parity is not yet established.

### Barrel/rocket radius range rejection

`test_doom_radius_range.c` executes retail 0x9d8e through the native
range branches, observing accepted continuation 0x9da7 or rejected target
continuation 0x9de0 before redirecting to a controlled STOP. 524,288 cases
cross every wrapped coordinate difference, each axis and damage high bytes
0, 1, 63 and 127. Original source uses one's complement for negative
differences (the increment is commented out), so negative and positive
boundaries differ by one. The independent oracle preserves this behavior:
wrapped negative difference is complemented; admission requires both axis
magnitudes to be strictly below 40 plus the damage high byte. Every RAM
byte remains unchanged. Retail instruction bytes are verified.

The Release build and affected CTest pass; evidence is preserved in
`build-evidence/physics/radius-range-ctest-log.txt`. This isolated branch
check does not cover BFG routing, joint X/Y combinations, other high bytes,
line-of-sight, target traversal or damage application. No production
mismatch was found. Full parity remains incomplete.

### Joint radius boundary coverage

The radius-range test adds 73,728 joint X/Y cases: all 128 non-BFG high
bytes, twelve signed/wrapped differences per axis, four translated target
origins and four low damage bytes. Boundaries include radius-1/radius/
radius+1, the asymmetric negative edges and signed extrema. Target origins
0, 0x7fff, 0x8000 and 0xffff exercise subtraction across coordinate wrap.
The two-axis oracle admits a target only when both one's-complement
magnitudes lie below the source radius. Every RAM byte is checked and
branch observations stop before target damage or traversal. The current
combined count is 598,016 passing cases, superseding the earlier count.
Release rebuild and affected CTest pass; the range evidence log is refreshed.
This narrows the range-check gaps but does not establish BFG selection,
line-of-sight, target enumeration or actual damage parity. Goal remains active.

### Radius damage kind and BFG player exemption

524,288 routing cases enter retail 0x9d76 with the saved radius pointer
and cross every damage word with eight target types (0, 1, 2, 63, 127,
128, 129, 255). Observers distinguish direct damage, next-target rejection
and the barrel/rocket range entry; they stop before executing those consumers.
Every RAM byte is checked. Original routing increments the high damage
byte before testing its bit 7: bytes 127 through 254 select BFG, while
0 through 126 and 255 enter range checking. BFG exempts type zero only;
the loaded unsigned type byte is decremented as a 16-bit register value.
An initial oracle incorrectly treated the resulting type value as signed
8-bit; the observed type-129 failure identified that fixture error, which
was corrected against the source's 16-bit DEC/BMI semantics. No runtime
change was made. The Release rebuild and combined CTest now pass both
598,016 range cases and 524,288 routing cases; the evidence log is refreshed.

Correction to earlier coverage wording: the direct range test's high bytes
0..127 are routine inputs, not all non-BFG values. High byte 127 normally
routes to BFG; 255 normally routes to range. Existing direct-range tests
remain valid isolated evidence but do not establish the complete connected
routing/range behavior. Target enumeration, line-of-sight and actual damage
application remain unverified. Full goal remains active.

### Wrapped radius kind 255

The direct range sweep now includes high damage byte 255: all 65,536
differences on each axis, plus 576 joint boundary/translated-origin cases.
This value normally reaches rocket/barrel range checking because the kind
classifier increments the byte before testing its bit 7. These additional
131,648 cases pass; the current range total is 729,664, alongside 524,288
routing cases. All routed range high bytes (0..126 and 255) now have joint
boundary coverage; byte 127 remains an extra direct routine input.
Release rebuild and affected CTest pass and the saved range log is refreshed.
Release executable and original rollback SHA256 still match
BA3DF3C36B4A18BDE119E587D742418AD2F4BF7D9949F64000328B8766A5B027.
This isolated range evidence still excludes connected target enumeration,
line-of-sight and actual damage application; full parity remains incomplete.

### Expanded whole-suite verification

The current Release configuration was tested with
`ctest --test-dir build-review-release --output-on-failure -j 4`.
All 39 registered tests passed in 58.50 seconds, including all added
movement/collision, lifecycle, queue and radius branch tests. The full
LastTest log is preserved at `build-evidence/physics/release-ctest-log.txt`.
Earlier whole-suite counts describe prior configurations. This current
result verifies the registered tests only: optional renderer-state tests
are not registered without their ROM cache setting, and the source audit
still has explicit unverified functions and integrated-state gaps.
No claim of full physics parity follows from this suite result.

### Connected routing/range discrepancy under investigation

A new 73,728-case connected fixture removes the intermediate range-entry
observer and starts at 0x9d76. It currently fails for damage 0x0000,
player type zero, wrapped X delta zero and Y delta 40. The isolated source
arithmetic oracle expects range rejection; native execution accepts, with
R1=0x0083 (131), not 40. Target object address is 0x5b00. Retail BPL at
0x9d81 targets 0x9d8f, skipping FROM R4 at 0x9d8e, while its delay slot
is FROM R13 at 0x9d83. Thus the observed radius is consistent with the
high byte of the target pointer plus 40. This requires explicit validation
of original branch-delay/prefix semantics before classifying it as a runtime
bug or adjusting the instruction-level source oracle. No production change
has been made and parity is not claimed for this connected path.

The updated Release build succeeds, but the affected CTest currently fails.
The earlier 39-test green log predates this fixture and is not proof that
the current suite passes. The failure log is preserved as
`build-evidence/physics/radius-connected-failure-log.txt`. Set the process
environment `RADIUS_CONNECTED_ONLY=1` to skip the prior exhaustive phases
and reproduce the connected failure quickly; default CTest runs all phases.
The full goal remains active and this discrepancy is the next investigation.

### Connected discrepancy resolved by source instruction semantics

The failing expectation omitted the original BPL delay slot. The retail
branch at 0x9d81 has displacement 0x0c and targets 0x9d8f, as specified
by `_RLRD2600+1` in source. Its delay slot at 0x9d83 is FROM R13; the
branch skips FROM R4 at 0x9d8e. HIB at 0x9d8f therefore reads R13,
not the damage word. For target pointer 0x5b00 the resulting radius is
0x5b + 40 = 131. This unusual original behavior must be preserved for
instruction-level parity; no production arithmetic correction is warranted
from the earlier failure. This is an inference from original source/retail
bytes and independently compared processor semantics, not hardware capture.

Independent primary implementation references inspected:
- https://raw.githubusercontent.com/ares-emulator/ares/master/ares/component/processor/gsu/instructions.cpp
  Branch consumes displacement and adjusts R15 without resetting prefixes;
  FROM selects source register; HIB reads that source and resets prefixes.
- https://raw.githubusercontent.com/ares-emulator/ares/master/ares/sfc/coprocessor/superfx/memory.cpp
  peekpipe/pipe retain the prefetched delay-slot opcode.
- https://raw.githubusercontent.com/ares-emulator/ares/master/ares/sfc/coprocessor/superfx/superfx.cpp
  main executes the pipeline opcode and conditionally advances R15.

The connected oracle now derives its radius from the target pointer and
checks R1. A read-only observer at 0x9d8f also asserts SREG=13/DREG=0,
and is reached once for range paths and never for BFG paths. All 73,728
connected cases pass in the fast reproducer. These cross all high damage
bytes, joint boundary deltas, type zero/one and varying low damage bytes.
The prior failed log is retained as investigation evidence. Other target
addresses, full processor state, line-of-sight and actual damage remain
unverified; full goal stays active.

The full affected CTest now passes all three phases: 729,664 direct range,
524,288 routing and 73,728 connected cases (47.20 seconds). The current
passing log is saved at `build-evidence/physics/radius-range-ctest-log.txt`.
Release rebuild succeeds. The entire suite has not been rerun after this
oracle change; the previously saved 39-test log predates the connected phase.

### Connected radius checks across target addresses

The connected phase now crosses all 256 high damage bytes and the joint
X/Y boundary matrix with five target addresses: 0x5a8e plus 38 times
positions 0, 1, 31, 63 and 99. These exercise multiple address high bytes,
including the last position in the source's 100-object movable allocation.
Each fixture sets its own type byte and independently derives radius from
the target pointer, asserts the native R1 value and checks the surviving
source prefix on the routed range path. All 368,640 connected cases pass.
Together with 729,664 isolated range and 524,288 routing cases, the affected
CTest passes after a successful Release rebuild (49.38 seconds).
The passing range log is refreshed. Release and rollback hashes still match
BA3DF3C36B4A18BDE119E587D742418AD2F4BF7D9949F64000328B8766A5B027.
Target enumeration, line-of-sight, damage application and complete runtime
state remain unverified; the full objective stays active. The whole suite
has not been rerun after this fixture expansion.

### Radius target flags and object stride

`test_doom_radius_filter.c` executes retail 0x9def through the original
flag filter, observing either loop continuation 0x9de6 or accepted-target
continuation 0x9e07 before redirecting to a controlled STOP. All 65,536
flag/type byte combinations pass an independent oracle: admission requires
both active (0x80) and target (0x20); cursor advances by 38 for movable
(0x40), otherwise 8. Type does not influence this byte-only filter. Every
RAM byte, cursor and preserved target pointer are checked; retail code
bytes are verified. Observations occur after branch delay slots and before
the accepted path saves its cursor or begins coordinate/line-of-sight work.

Release rebuild and affected CTest pass. Evidence is preserved in
`build-evidence/physics/radius-filter-ctest-log.txt`. This isolated filter
does not prove list bounds, sequential enumeration, coordinate loads,
line-of-sight or damage application. The configuration now has 40 tests;
the prior 39-test whole-suite log predates this addition. Full parity is
still incomplete and the goal remains active.

### Connected radius target iteration

11,520 cases remove the intermediate loop observer and execute native
0x9de6 compare/stride/filter loops through an accepted target or the native
end jump (observed at 0x9d56). Fixtures cross lengths zero through eight,
every accepted position plus no accepted target, and 256 fixed/movable
stride patterns. Rejected objects cycle inactive, active-only and target-only
flags. An independent list oracle checks selected target pointer, advanced
cursor and every RAM byte; accepted entries require active+target.
Empty lists and exhaustion stop at the original end guard. The fixture
supplies a synthetic end pointer in R6, so this proves loop mechanics for
those lists, not the full production allocation's enumeration or saved-pointer
initialization. No intermediate redirects occur. Observers stop before
coordinate/line-of-sight processing or the next radius entry.

Release rebuild and affected CTest pass 65,536 flag/type cases plus 11,520
connected traversal cases. The preserved filter log is refreshed. Original
code bytes for the filter are verified; the initial guard/end instruction
bytes are executed from the retail ROM but not separately byte-asserted.
No production mismatch found. Coordinate loads, full production traversal,
line-of-sight and damage application remain open; full goal stays active.

### Accepted movable radius target coordinates

65,536 cases execute the flag filter, save the advanced enemy cursor,
load movable target X/Y and take the native branch into line-of-sight setup.
All X words are swept; Y follows a distinct bijection covering every word.
The observer stops at 0x9e2f after the branch delay's Y load. Retail accepted
path bytes are checked, and the independent oracle checks R7/R8, R10/R13
and every RAM byte, including saved ENEnemy at 0x178. All cases pass after
a successful Release rebuild, alongside previous filter/traversal phases.
The affected test log is refreshed. No production mismatch found. Fixed
ROM coordinates and actual line-of-sight/damage remain unverified.

### Accepted fixed radius target ROM coordinates

131,072 additional cases take the accepted fixed-object branch from the
native flag filter through ROM-bank setup and X/Y loads, stopping before
line-of-sight setup at 0x9e2f. Both banks 0x4f and 0x5f are crossed with
all X words; a different bijection sweeps all Y words. The other bank has
complemented decoy coordinates, making incorrect bank selection observable.
Every RAM byte, saved cursor, target pointer, X/Y registers, ROM bank and
final animation/data pointer 0x9003 are checked. Retail fixed-path bytes
are verified. Both branches preserve native little-endian load order.
All four filter/traversal/coordinate phases pass after the Release rebuild;
the preserved log is refreshed. These fixtures use synthetic LevelObject
ROM data and pointer 0x9000; other pointers/banks, full production target
lists, line-of-sight and damage application remain open. No production
mismatch found and full parity remains incomplete.
