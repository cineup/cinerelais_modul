# CLAUDE.md — AI Assistant Guide for cinerelais_modul

## Project Overview

**Repository**: `cineup/cinerelais_modul`
**Status**: Newly initialized (no source code yet)
**Domain**: Cinema relay module — part of the CineUp ecosystem

This project is a relay module for cinema automation, belonging to the `cineup` organization. The name suggests German-language origins ("Relais" = relay, "Modul" = module). The module likely controls relay-based switching for cinema infrastructure (e.g., lighting, curtains, projector control, audio routing).

## Repository Structure

```
cinerelais_modul/
├── CLAUDE.md              # This file — AI assistant guide
└── .git/                  # Git repository metadata
```

> **Note**: This repository is in its initial state. Update this section as the project structure evolves.

## Development Setup

### Prerequisites

_To be determined as the project takes shape. Likely candidates based on the domain:_

- Embedded toolchain (e.g., PlatformIO, Arduino IDE, or arm-none-eabi-gcc)
- Hardware relay module for testing
- Serial/UART debugging tools

### Building

_No build system configured yet. Update this section when build tooling is added._

### Testing

_No test infrastructure configured yet. Update this section when tests are added._

## Conventions

### Language

- Project naming uses German terminology (Relais, Modul)
- Code comments and documentation language: _to be established_

### Git Workflow

- **Branch naming**: Feature branches use descriptive names
- **Commits**: Use clear, descriptive commit messages in imperative mood
- **Main branch**: Not yet established — first commit will define it

### Code Style

_To be established. Update this section with linting rules, formatters, and style guides as they are adopted._

## Architecture

_To be documented as the project develops. Expected components for a cinema relay module:_

- **Hardware abstraction layer** — GPIO and relay driver interfaces
- **Communication protocol** — Serial, I2C, CAN, or network-based control interface
- **State management** — Relay state tracking and safety interlocks
- **Configuration** — Persistent settings for relay mapping and behavior

## Key Files

| File | Purpose |
|------|---------|
| `CLAUDE.md` | AI assistant context and project guide |

> Update this table as key files are added to the project.

## AI Assistant Guidelines

1. **Read before modifying** — Always read existing files before proposing changes
2. **Minimal changes** — Make only the changes requested; avoid unnecessary refactoring
3. **Preserve conventions** — Follow established patterns in the codebase
4. **Hardware awareness** — This is an embedded/hardware project; consider memory constraints, real-time requirements, and hardware safety
5. **Safety first** — Relay modules interact with physical equipment; never bypass safety checks or interlocks
6. **Update this file** — When significant structural changes are made, update CLAUDE.md to reflect the current state

## Maintenance Log

| Date | Change |
|------|--------|
| 2026-02-01 | Initial CLAUDE.md created for empty repository |
