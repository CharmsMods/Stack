# Render resource regression checks

Run from the repository root with a C++17 compiler and CMake. This independent
project builds the production section store and viewport policy headers without
fetching the application's GUI, GPU or model dependencies.

```sh
cmake -S tools/tests/render_resources -B _workspace/render-resource-checks -DCMAKE_BUILD_TYPE=Release
cmake --build _workspace/render-resource-checks --config Release --parallel 2
ctest --test-dir _workspace/render-resource-checks -C Release --output-on-failure
```

The eight separately reported cases cover indexed/visible-first coverage,
RAM and temporary-file bit preservation (including signed zero, negative/HDR
values, infinities and NaN payload bits), shared upload leases, capacity-based
RAM accounting, concurrent publication, expired timing evidence, and the
explanation of the actually dispatched quality candidate. Failed loads leave
previous caller pixels unchanged. These checks do not render or modify a user
photo or project. Publication exercises a real writer and concurrent metadata
reader, but has no machine-dependent latency assertion.

Windows exercises the normal Win32 delete-on-close cache. POSIX hosts exercise
the same store/index/accounting code through a `tmpfile` transport. This does not
establish Linux application support or substitute for Windows validation.

For focused sanitizer runs on supported Clang/GCC hosts, configure a separate
tree with `-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`.
ThreadSanitizer is a separate build (`-fsanitize=thread`); run the `publication`
case. Do not combine ThreadSanitizer with AddressSanitizer.

## Limits

These are CPU/cache contract checks, not benchmarks. They do not compile the
full application, exercise a real GL upload/fence, verify preview/export parity,
or establish mouse feel, display cadence, HDR appearance, device-loss behavior
or performance on particular adapters. Use the normal Windows build and focused
viewport validation on real supported GPUs before treating those as qualified.
See `RESEARCH/rendering-resource-audit.md` for the inspected paths, event
contracts, remaining risks, and hardware validation plan.
