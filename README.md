# transaction-engine

Transaction protocol over an unreliable link: acknowledged send, retries with backoff, dedup on receive.

Part of [integra-lib](https://gitlab.integrasources.com/internal-projects/integra-lib).
Header-only C++20 with one translation unit, no exceptions, no RTTI.

## Use it

```bash
git submodule add ../transaction-engine.git external/integra/transaction-engine
```

```cmake
add_subdirectory(external/integra/transaction-engine)
target_link_libraries(app PRIVATE Integra::transaction_engine)
```

```cpp
#include <integra/transaction_engine.hpp>
```

## Dependencies

This component needs three others, added next to it rather than inside it:

* `Integra::crc` >= 0.1.0, < 0.2.0
* `Integra::bit_ops` >= 0.1.0, < 0.2.0
* `Integra::dedup_cache` >= 0.1.0, < 0.2.0

Missing or out of range, they stop the CMake configure with a message naming the version found.

## Develop it

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
```

Tests are built only when this repository is the top-level project, so a consumer
never builds them. Style and pipeline come from the `ci-shared` submodule; run
`git submodule update --init` before `pre-commit`.
