# Trading OMS/RMS Demo

A C++20 demo application showcasing the core concepts and workflows of an **Order Management System (OMS)** and **Risk Management System (RMS)** in trading software.

## Purpose

This project is intended as a demonstration and learning codebase for how OMS and RMS components fit together in a trading environment. It provides a reproducible C++ foundation that will evolve incrementally through user stories.

## Project Structure

```text
.
├── .github/workflows/    # CI pipeline definitions
├── bench/                # Benchmarks
├── docs/
│   └── adr/              # Architecture Decision Records
├── include/              # Public headers
├── src/                  # Application/library source code
├── tests/                # Tests
├── CMakeLists.txt        # CMake build configuration
└── README.md
```

## Requirements

- CMake >= 3.20
- A C++20-compatible compiler:
  - GCC
  - Clang / AppleClang

## Build

Configure and build from the repository root:

```bash
cmake -S . -B build
cmake --build build
```

Run the demo executable:

```bash
./build/trading_oms_rms_demo
```

## Test

After building, run tests with CTest:

```bash
ctest --test-dir build --output-on-failure
```

## Continuous Integration

GitHub Actions CI (`.github/workflows/ci.yml`) runs on every push and pull request:

- **Build & Test:** GCC and Clang, in both `Debug` and `Release` configurations.
- **Sanitizers:** a separate job runs tests with **AddressSanitizer (ASan)** and **UndefinedBehaviorSanitizer (UBSan)**.

## Development Notes

- The build is configured for C++20 (`CMAKE_CXX_STANDARD 20`, no compiler extensions).
- Compiler warnings are treated as errors for GCC/Clang/AppleClang: `-Wall -Wextra -Werror`.
- The project is being developed incrementally, story by story.
