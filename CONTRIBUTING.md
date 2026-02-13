# Contributing to WallaBMC

Thank you for your interest in contributing to WallaBMC. This document provides guidelines for contributing to the project.

## Code of Conduct

This project adheres to the [Contributor Covenant Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code.

## Getting Started

### Prerequisites

- A working [Zephyr RTOS development environment](https://docs.zephyrproject.org/latest/getting_started/index.html)
- West manifest and workspace set up as described in [README.rst](README.rst#building)

### Building

From the project root (inside your west workspace):

```sh
cd wallabmc
west build -b $BOARD app
```

Supported boards include `hifive_premier_p550_mcu` and `nucleo_f767zi`.

## How to Contribute

### Reporting Bugs

Open an issue with a clear description of the problem, steps to reproduce, your board/target, and any relevant log output or behavior.

### Suggesting Changes

For feature requests or larger changes, open an issue first to discuss the idea. This helps avoid duplicate work and ensures the change fits the project direction.

### Submitting Changes

1. **Fork and clone** the repository (or create a branch if you have write access).

2. **Make your changes** in a topic branch. Keep commits focused and messages clear.

3. **License headers**: New source files must include SPDX license identifiers. The project uses **Apache-2.0**. For example, at the top of a C file:

   ```c
   /*
    * Copyright (c) 20XX Your Name or Organization
    *
    * SPDX-License-Identifier: Apache-2.0
    */
   ```

   Pull requests are checked for correct SPDX headers; see [.github/workflows/spdx.yml](.github/workflows/spdx.yml).

4. **Build and test**: Ensure the project builds for at least one supported board and that existing behavior is preserved.

5. **Open a pull request** against the default branch. Describe what changed and why. Reference any related issues.

6. **Address review feedback** promptly. Maintainers may request changes before merging.

## Project Structure

- **`src/`** – Main application and BMC logic (C)
- **`boards/`** – Board definitions, overlays, and configs
- **`scripts/`** – Build and CI helpers
- **`static_web_resources/`** – Web UI and Redfish-related static assets

## License

By contributing, you agree that your contributions will be licensed under the same license as the project: **Apache License 2.0**.
