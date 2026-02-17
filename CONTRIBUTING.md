# Contributing to WallaBMC

Thank you for your interest in contributing to WallaBMC! This document provides guidelines for contributing to the project.

## Ways to Contribute

We welcome contributions in various forms:

- **Bug Reports**: Report bugs via GitHub Issues
- **Bug Fixes**: Submit fixes via Pull Requests
- **New Features**: Propose and implement new functionality
- **Documentation**: Improve documentation and examples
- **Testing**: Add test cases and improve test coverage

## Getting Started

### Reporting Bugs

If you find a bug, please report it via [GitHub Issues](https://github.com/tenstorrent/wallabmc/issues) with the following information:

- A clear, descriptive title
- Steps to reproduce the issue
- Expected behavior
- Actual behavior
- Your environment (OS, hardware, Zephyr version, etc.)
- Any relevant logs or error messages

### Suggesting Enhancements

Feature requests and enhancement suggestions are welcome! Please open a GitHub Issue with:

- A clear description of the proposed feature
- Use cases and benefits
- Any implementation considerations you're aware of

## Pull Request Process

### Before Submitting

1. **Check existing issues and PRs**: Make sure someone hasn't already worked on the same thing
2. **Discuss major changes**: For significant changes, open an issue first to discuss your approach
3. **Follow coding standards**: Match the existing code style and conventions

### Submitting a Pull Request

1. **Fork the repository** and create a branch from `main`
2. **Make your changes** following the coding standards
3. **Test your changes** thoroughly on relevant hardware platforms
4. **Update documentation** if you've changed functionality
5. **Commit your changes** with clear, descriptive commit messages
6. **Push to your fork** and submit a pull request to the `main` branch

### Pull Request Guidelines

- **One feature per PR**: Keep pull requests focused on a single feature or bug fix
- **Descriptive titles**: Use clear, concise titles that describe the change
- **Detailed descriptions**: Explain what changes you made and why
- **Reference issues**: Link to any related GitHub Issues
- **Keep commits clean**: Use meaningful commit messages; squash commits if necessary

### Code Review Process

- Pull requests are typically reviewed weekly (or more frequently for urgent fixes)
- Maintainers may request changes or ask questions
- Once approved, a maintainer will merge your PR
- Please be responsive to feedback and questions

## Coding Standards

### Code Style

- Follow the existing code style in the repository
- Use consistent indentation (tabs as configured in the project)
- Keep functions focused and reasonably sized
- Add comments for complex logic

### C Programming Guidelines

- Follow the Zephyr Project coding conventions where applicable
- Use meaningful variable and function names
- Avoid magic numbers; use named constants or enums
- Check return values and handle errors appropriately
- Memory management: ensure proper allocation and deallocation

### Commit Messages

Write clear, descriptive commit messages:

```
Short summary (50 chars or less)

More detailed explanation if needed. Wrap at 72 characters.
Explain the problem this commit solves and how. Include any
relevant context or design decisions.
Fixes #123
```

## Testing

Before submitting a pull request:

1. **Build the project**: Ensure it builds without errors for target platforms
2. **Test on hardware**: If possible, test your changes on actual hardware
3. **Check for regressions**: Verify existing functionality still works
4. **Document testing**: Describe the testing you performed in your PR

Supported test platforms:

- SiFive HiFive Premier P550 MCU
- Nucleo F767ZI board
- qemu ( see [run_qemu_ci.py](scripts/run_qemu_ci.py) )

## Documentation

When adding new features or changing existing functionality:

- Update relevant documentation files (README.rst, etc.)
- Add inline code comments for complex logic
- Include usage examples where appropriate
- Update configuration options in documentation

## Licensing

By contributing to WallaBMC, you agree that your contributions will be licensed under the Apache License 2.0, the same license as the project. You retain copyright to your contributions.

### Developer Certificate of Origin

By submitting a contribution, you certify that:

- You created the contribution and have the right to submit it under the Apache 2.0 License, or
- The contribution is based on previous work covered by a compatible open source license, or
- The contribution was provided to you by someone who certifies (a) or (b) and you're submitting it unmodified

## Code of Conduct

This project adheres to the Contributor Covenant Code of Conduct. By participating, you are expected to uphold this code. Please report unacceptable behavior to ospo@tenstorrent.com.
See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) for details.

## Questions?

If you have questions about contributing:
- Open a GitHub Issue with your question
- Check existing issues and discussions
- Contact the maintainers via GitHub

## Recognition

Contributors will be recognized in the project. We appreciate your efforts to improve WallaBMC!

---

Thank you for contributing to WallaBMC! 🎉