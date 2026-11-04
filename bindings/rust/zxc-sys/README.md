# zxc-compress-sys

Low-level, unsafe FFI bindings to the [ZXC](https://github.com/hellobertrand/zxc)
compression library. The C sources are vendored and built by the crate's build
script; enable the `system` feature to link against an installed `libzxc` instead.

Most users want the safe wrapper, [`zxc-compress`](https://crates.io/crates/zxc-compress)
([documentation](https://docs.rs/zxc-compress)).

[![Crates.io](https://img.shields.io/crates/v/zxc-compress-sys.svg)](https://crates.io/crates/zxc-compress-sys)
[![Documentation](https://docs.rs/zxc-compress-sys/badge.svg)](https://docs.rs/zxc-compress-sys)

## License

BSD-3-Clause - see [LICENSE](https://github.com/hellobertrand/zxc/blob/main/LICENSE) for details.
