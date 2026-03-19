# Contributing to TronGrid

Thank you for your interest in contributing to TronGrid! This document provides guidelines and information for contributors.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [How to Contribute](#how-to-contribute)
    - [Reporting Bugs](#reporting-bugs)
    - [Suggesting Features](#suggesting-features)
    - [Pull Requests](#pull-requests)
- [Development Setup](#development-setup)
- [Coding Standards](#coding-standards)
- [Commit Messages](#commit-messages)
- [Documentation](#documentation)

---

## Code of Conduct

This project adheres to the Contributor Covenant Code of Conduct.
By participating, you are expected to uphold this code. Please see [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) for details.

---

## How to Contribute

### Reporting Bugs

Before submitting a bug report:

1. Check the [existing issues](https://github.com/MatejGomboc/tron-grid/issues) to avoid duplicates
2. Ensure you're using the latest version
3. Collect relevant information:
    - Operating system and version
    - GPU and driver version
    - Vulkan SDK version
    - Compiler and CMake preset used
    - Steps to reproduce
    - Expected vs actual behaviour

When submitting:

- Use the bug report template
- Provide a clear, descriptive title
- Include minimal reproduction steps
- Include Vulkan validation layer output if relevant

### Suggesting Features

We welcome feature suggestions! Before submitting:

1. Check [existing issues](https://github.com/MatejGomboc/tron-grid/issues) and [discussions](https://github.com/MatejGomboc/tron-grid/discussions) for similar ideas
2. Consider how the feature fits the project's [phased roadmap](docs/VISION.md#phased-roadmap)
3. Think about backwards compatibility

When submitting:

- Use the feature request template
- Explain the problem you're trying to solve
- Describe your proposed solution
- Consider alternatives you've thought about

### Pull Requests

#### Before You Start

1. Open an issue first to discuss significant changes
2. Fork the repository
3. Create a feature branch from `main`
4. Make your changes following our [coding standards](#coding-standards)

#### PR Requirements

- [ ] Code compiles without warnings on all relevant presets
- [ ] Code is formatted (`.clang-format`)
- [ ] No Vulkan validation layer errors
- [ ] Documentation is updated if needed
- [ ] CHANGELOG.md is updated for user-facing changes
- [ ] Commit messages follow [conventional commits](#commit-messages)

#### PR Process

1. Submit your PR against the `main` branch
2. Fill out the PR template completely
3. Wait for CI to pass
4. Address any review feedback
5. Once approved, a maintainer will merge

---

## Development Setup

### Prerequisites

- C++20 compiler (MSVC 19.30+, GCC 12+, or Clang 15+)
- CMake 3.16+
- Ninja build system
- Vulkan SDK 1.4.335.0+
- XCB development headers (Linux only)

### Setup

```bash
# Clone the repository
git clone https://github.com/MatejGomboc/tron-grid.git
cd tron-grid

# Windows (from VS Developer Command Prompt or with MSVC in PATH)
cmake --preset windows-msvc
cmake --build build/windows-msvc --config Debug

# Linux
sudo apt-get install libxcb1-dev
cmake --preset linux-x11-gcc
cmake --build build/linux-x11-gcc --config Debug
```

---

## Coding Standards

### C++ Style

- Follow `.clang-format` formatting (run clang-format before committing)
- Use C++20 features where appropriate
- Allman braces for functions and namespaces, attached for classes/structs/enums
- 4-space indentation, 170-column limit
- See [STYLE.md](STYLE.md) for the full style guide

### Naming Conventions

See [STYLE.md](STYLE.md) § Naming Conventions for the full table. Key rules:
camelCase functions, PascalCase types, `m_` member prefix, SCREAMING_SNAKE_CASE constants.

### Documentation

- Add comments for non-obvious logic
- Keep comments up to date with code changes
- Use British spelling in all documentation and comments

### British Spelling

Use British spelling in all documentation and user-facing text:

| American | British |
|----------|---------|
| color | colour |
| behavior | behaviour |
| organization | organisation |
| center | centre |
| license (noun) | licence |
| analyze | analyse |
| initialize | initialise |
| optimize | optimise |
| meter | metre |
| synchronize | synchronise |

**Note:** Code identifiers may use American spelling where it matches library/API conventions (e.g., Vulkan API names).

---

## Commit Messages

We use [Conventional Commits](https://www.conventionalcommits.org/). Format:

```text
<type>(<scope>): <description>

[optional body]

[optional footer(s)]
```

### Types

| Type | Description |
|------|-------------|
| `feat` | New feature |
| `fix` | Bug fix |
| `docs` | Documentation only |
| `style` | Formatting, no code change |
| `refactor` | Code change that neither fixes a bug nor adds a feature |
| `perf` | Performance improvement |
| `test` | Adding or updating tests |
| `chore` | Maintenance tasks |
| `ci` | CI/CD changes |

### Examples

```text
feat(vulkan): add swapchain creation

fix(window): correct X11 event handling on multi-monitor setups

docs: update README with build instructions

chore: update Vulkan SDK to 1.4.335.0
```

### Rules

- Use imperative mood ("Add feature" not "Added feature")
- Don't capitalise the first letter of the description
- No period at the end of the subject line
- Keep the subject line under 72 characters
- Reference issues in the footer: `Fixes #123`

---

## Documentation

### Types of Documentation

| Location | Purpose |
|----------|---------|
| `README.md` | User-facing overview and quick start |
| `CONTRIBUTING.md` | This file — contributor guidelines |
| `SECURITY.md` | Security policy and vulnerability reporting |
| `STYLE.md` | Code style conventions |
| `CHANGELOG.md` | User-facing change history |
| `docs/ARCHITECTURE.md` | Technical architecture |
| `docs/VISION.md` | Project vision and roadmap (single source of truth) |
| `docs/AI_INTERFACE.md` | AI brain plugin interface specification (future) |

### Updating Documentation

- Update `README.md` for user-facing changes
- Update `CHANGELOG.md` for all notable changes
- Update code comments when changing public APIs
- Keep examples up to date and working

---

## Questions?

- Open a [Discussion](https://github.com/MatejGomboc/tron-grid/discussions) for questions
- Check existing issues and discussions first
- Be patient — maintainers are volunteers

Thank you for contributing!
