# Windows v0.01 validation

Both runtime executables were built in Release mode with the portable
Clang/MinGW toolchain and static SDL3/runtime libraries. The x64 executable
has PE machine type AMD64 (0x8664); x86 has I386 (0x014c).

- All 14 CTest tests passed on each architecture.
- The renderer state test, including the retail-ROM checks, passed on each.
- Each executable completed the menu route at 4x resolution, 16:9 and
  interpolated presentation, including a long native-menu pause and resume.
- Evidence: `build-lag-evidence/v001-x64-menu-final` and
  `build-lag-evidence/v001-x86-menu-final` (local, excluded from Git).
- Windows icon resources use the same multi-size ICO derived from `logo1.png`.
- Package checks verify PE architecture, runtime dependencies, licenses,
  and exclusion of ROM files and machine-specific state.

The x86 build required corrections to Windows crash-report register selection
and the timing API calling convention. Both corrections are committed in
the pinned snesrecomp framework. No gameplay or texture-placement changes
were needed for the 32-bit target.
