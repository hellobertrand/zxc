# TODO

## Documentation

- [ ] **Structured Changelog**: Implement a changelog following the [Keep a Changelog](https://keepachangelog.com/) format
  - Create `CHANGELOG.md` with sections for Added, Changed, Deprecated, Removed, Fixed, and Security
  - Document changes for each version starting from v0.6.0

## Build System

- [ ] **pkg-config Support**: Generate `.pc` file for easier integration
  - Create `zxc.pc.in` template
  - Configure CMake to generate `zxc.pc` with correct paths and flags
  - Install `.pc` file to appropriate location (`${libdir}/pkgconfig`)

- [ ] **Shared Library Build**: Add CMake option to build shared libraries
  - Add `BUILD_SHARED_LIBS` CMake option
  - Generate `libzxc.so` (Linux), `libzxc.dylib` (macOS), or `zxc.dll` (Windows)
  - Ensure proper symbol visibility and versioning
  - Update installation targets for shared libraries
