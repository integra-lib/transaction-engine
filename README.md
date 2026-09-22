# transaction-engine

Transaction protocol over an unreliable link: acknowledged send, retries with exponential backoff, dedup on receive.

Part of [integra-lib](https://github.com/integra-lib) — architecture-independent C++20
components shared between firmware projects. One translation unit,
no exceptions, no RTTI.

## Use it

```bash
git submodule add git@github.com:integra-lib/transaction-engine.git external/integra/transaction-engine
```

```cmake
add_subdirectory(external/integra/transaction-engine)
target_link_libraries(app PRIVATE Integra::transaction_engine)
```

```cpp
#include <integra/transaction_engine.hpp>
```

Each component carries its own include directory, so this header stays unreachable
until the component is linked: a forgotten dependency is a compile error rather than
a build that happens to work.

## Dependencies

Three other components, added **next to** this one rather than inside it, so that a
consumer never ends up with two copies of the same component:

* `Integra::crc` >= 0.1.0, < 0.2.0
* `Integra::bit_ops` >= 0.1.0, < 0.2.0
* `Integra::dedup_cache` >= 0.1.0, < 0.2.0

```bash
for c in crc bit-ops dedup-cache; do
  git submodule add git@github.com:integra-lib/$c.git external/integra/$c
done
```

```cmake
add_subdirectory(external/integra/crc)
add_subdirectory(external/integra/bit-ops)
add_subdirectory(external/integra/dedup-cache)
add_subdirectory(external/integra/transaction-engine)   # after its dependencies
```

Missing, or outside the range, they stop the CMake configure with a message naming
the component and the version found.

## Before using it in firmware

This is the only component with a translation unit of its own, so it is a static
library rather than a header-only one. Under Zephyr a static library must be given
the SDK's compile flags — otherwise it is built with a different ABI than the
application that links it. Verify a build on the real toolchain before shipping it
in firmware; the header-only components have no such concern, because they are
compiled as part of the application.

## Versioning

Every component is released on its own, tagged `vX.Y.Z`. Pre-1.0, a minor release may
break the API, which is why dependants accept a single minor.

```bash
git -C external/integra/transaction-engine fetch --tags
git -C external/integra/transaction-engine checkout v0.2.0
git add external/integra/transaction-engine && git commit -m "build: bump transaction-engine to v0.2.0"
```

## In a consumer's CI

The component is an ordinary submodule, so the build needs it checked out. On GitLab
that means `GIT_SUBMODULE_STRATEGY: normal` (or `recursive`) on every job that builds —
not only on the ones that run unit tests.

## Develop it

```bash
git submodule update --init          # ci-shared, needed by pre-commit
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
```

Tests are built only when this repository is the top-level project, so a consumer
never builds them and never fetches GoogleTest.

The style configs are symlinks into the `ci-shared` submodule, and the pipeline comes
from the same place. On GitHub this repository carries a self-contained build-and-test
workflow instead: a workflow token cannot read another private repository, so neither
a shared workflow nor the submodule is reachable there. The shared setup is what
GitLab will use.
