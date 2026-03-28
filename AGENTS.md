# AGENTS.md — Developer Guidelines for imflame

This document provides essential information for AI agents and developers working on the imflame flame graph viewer project.

## Project Documentation Index

| File | Purpose |
|------|---------|
| `SPEC.md` | **Specification**. Defines data models, UI layout, rendering rules, interaction behavior, performance constraints, and acceptance criteria. The single source of truth for all development work. |
| `PLAN.md` | **Development Plan**. Breaks down SPEC into 7 phases with 28 tasks, including technology selection, phase dependencies, SPEC chapter references, and expected deliverables. |
| `PROGRESS.md` | **Progress Tracking**. Records task completion status (🔲/🔶/✅/❌), must be updated during development. Contains change log at the bottom. |
| `README.md` | **Project Documentation**. Includes features, technology stack, build instructions, usage, data format, and project structure. |
| `CMakePresets.json` | **CMake Presets Configuration**. Provides VSCode CMake plugin compatible presets for one-click configuration and build. |

## Work Conventions

1. Before starting a task, read the corresponding section in `SPEC.md` to confirm requirements.
2. After completing a task, update the status and notes in `PROGRESS.md`.
3. When encountering requirement ambiguities, refer to `SPEC.md` as the authority.

## Project Overview
- **Language**: C++17
- **GUI Framework**: Dear ImGui (docking branch)
- **Charting**: ImPlot
- **Graphics Backend**: GLFW + OpenGL3
- **JSON Parsing**: nlohmann/json
- **Build System**: CMake 3.20+

## Build Commands

### Basic Build Process
```bash
# Create build directory
mkdir build && cd build

# Configure (first-time builds will download dependencies automatically)
cmake ..

# Compile release version
cmake --build . --config Release
```

### CMake Presets (Windows/Visual Studio)
```bash
# Configure with preset
cmake --preset vs2022-x64

# Build release with preset
cmake --build --preset vs22-x64-release
```

### Debug Build
```bash
# Configure debug
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build debug version
cmake --build . --config Debug
```

### Clean Rebuild
```bash
# Remove build directory and rebuild
rm -rf build && mkdir build && cd build && cmake .. && cmake --build . --config Release
```

### No Built-in Tests
The project currently has no unit tests or integration tests. Manual visual testing of UI interactions is required after changes.

## Code Style Guidelines

### C++ Standard & Features
- Target C++17 standard only
- Use modern C++ features: smart pointers, range-based loops, auto, lambdas
- Prefer RAII (Resource Acquisition Is Initialization) patterns
- Use const wherever possible for safer code

### Includes Organization
- Standard library headers first (alphabetically)
- Third-party library headers second
- Local project headers last
- Group with blank lines between groups
```cpp
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

#include "data_loader.h"
#include "flame_data.h"
#include "timeline_view.h"
```

### Naming Conventions
- Classes and Structs: `PascalCase`
- Functions: `camelCase`
- Variables: `camelCase`
- Constants: `kCamelCase` or `SCREAMING_SNAKE_CASE`
- Private members: `trailingUnderscore_` (e.g., `_member`)
- Section/paragraph markers: `§X` in comments indicating architectural sections

### Memory Management
- Use smart pointers (`std::unique_ptr`, `std::shared_ptr`) over raw pointers
- Prefer `std::make_unique<T>()` and `std::make_shared<T>()` for creation
- Avoid manual `new`/`delete`
- For resource cleanup, ensure destructors handle cleanup (RAII principles)

### Data Structures
- For arrays with variable sizes, prefer `std::vector<T>` over C-style arrays
- Use `std::unique_ptr<T[]>` for owning dynamic arrays
- Use `std::string` for text storage
- Use `std::mutex` for thread synchronization (project has background thread usage for loading)

### Error Handling
- Throw exceptions with `std::runtime_error` for runtime errors
- Catch exceptions by `const std::exception&` when handling
- Log errors to stderr using `fprintf` when returning error codes
- Always clean up allocated resources before returning early due to error

### Code Documentation

#### Comment Sections
- Use section markings: `// §X.Y — Topic Description`
- Example: `// §2.3 — 前值保持查询`
- These section markers link to the architectural documentation

#### Headers
- Every file should include descriptive comments about its purpose
- Document non-obvious algorithms or business logic
- Use Doxygen-style comments (`///`) for public interfaces

#### Inline Comments
- Explain non-obvious design decisions
- When performance is critical, explain the reasoning
- Describe the business logic behind algorithmic choices

### Performance Considerations
- Use global string pools to reduce memory duplication
- Sort child nodes alphabetically for consistent rendering
- Implement efficient lookups with binary search (bisection)
- Use progress callbacks during lengthy operations

### Multithreading and Concurrency
- Use `std::async` for background tasks like loading
- Use `std::future<T>` and `std::future_status` for async completion checking
- Store atomic flags for thread-safe shared state
- Use `std::thread` for custom background operations

### UI Design Patterns
- Follow the separation between data models (`flame_data.h/cpp`) and views (`timeline_view.h/cpp`, `flame_view.h/cpp`)
- Use Dear ImGui's immediate mode paradigm consistently
- Maintain consistent spacing, padding, and color schemes
- Implement responsive layouts that adjust to window size changes

### File Structure
- Header files: `.h`, Implementation files: `.cpp`
- Match file names to primary class/function name (e.g., `flame_data.h` contains `FlameNode` structure)
- Include guards: `#pragma once` (avoid macro-based guards)
- Match class/function to file name convention

### Architecture-Specific Patterns
- Data model (`flame_data.h/cp`): Sample queries using binary search
- View implementations use ImGui drawing primitives
- Separate concerns between data loading, processing, and visualization
- Background loading with progress callbacks

## Important Architectural Notes

### File Organization
- `main.cpp`: Application entry point, manages loading screen and main loop
- `flame_data.h/cpp`: Core data structures and query algorithms (bisection search)
- `data_loader.h/cpp`: Input/JSON parsing with SAX parser for efficiency
- `timeline_view.h/cpp`: Time series visualization (curves and cursor)
- `flame_view.h/cpp`: Flame graph visualization (rectangles, tooltips)

### Dependencies
- All external dependencies managed through CMake's FetchContent
- Dear ImGui with manual build setup (as it lacks native CMakeLists)
- nlohmann/json for SAX parsing and DOM manipulation
- OpenGL and GLFW for graphics backend

## Git Workflow
- Develop in topic branches off main
- Use descriptive commit messages in present tense: "Fix crash when loading malformed JSON" 
- Add issue numbers in brackets if applicable: `[Issue #123]`
- No pre-commit hooks or formalized test suite
- Merge after reviewing code changes for style consistency