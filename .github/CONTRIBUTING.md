# Contributing to ZXC

Thank you for your interest in ZXC! As an open-source project, we welcome contributions from the community, whether they are bug reports, documentation improvements, or code fixes.

This document outlines the guidelines and conventions for contributing effectively to the project.

## 🛠 Development Environment Setup

ZXC is written in **C17**. To compile and test the project locally, you will need:
* CMake (3.10+)
* A C17-compatible compiler (GCC, Clang, or MSVC)
* Make or Ninja

### Build the Project

```bash
git clone https://github.com/hellobertrand/zxc.git
cd zxc
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j
```

### Run Tests

Before submitting a change, ensure all unit tests pass:

```bash
cd build
ctest --output-on-failure
```

We also use Valgrind in our CI to detect memory leaks. If you are on Linux, we recommend running:

```bash
valgrind --leak-check=full ./zxc_test
```

## Coding Style & Conventions

Quality and performance are paramount for ZXC.

*   **Automatic Formatting**: We use `clang-format` with the Google style. Please format your files before committing:

  ```bash
  clang-format -i src/lib/*.c include/*.h
  ```

*   **Static Analysis**: Code must be clean for Cppcheck.
*   **ASCII Only**: To ensure compatibility, source code must not contain any non-ASCII characters (checked by CI).
*   **Performance**: ZXC is a high-performance library. Any change impacting the hot path must be justified by benchmarks using `lzbench`.

## Reporting a Bug

If you find a bug, please open an issue including:

*   The version of ZXC used (`zxc --version`).
*   The operating system and architecture (e.g., Ubuntu 22.04 x86_64, macOS M2).
*   A minimal example to reproduce the problem (file or command).

## Submitting a Pull Request (PR)

1.  Fork the repository and create a branch for your feature (`git checkout -b feature/my-feature`).
2.  Add corresponding tests in `tests/test.c` if you are adding functionality.
3.  Verify that your code compiles on major platforms (Linux, macOS, Windows).
4.  Ensure the BSD 3-Clause license header is present in new files.
5.  Open a PR targeting the `main` branch.

### PR Checklist

- [ ] Code compiles without warnings.
- [ ] `ctest` passes successfully.
- [ ] Code style follows `.clang-format`.
- [ ] No major performance regressions (CI will run benchmarks).

Thank you for helping make ZXC faster and more robust!
