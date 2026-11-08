# Local XboxXBERecomp changes

These patches apply to the `sdk` submodule. They are not upstream yet; apply them after cloning:

```
git -C sdk apply ../sdk-patches/0001-xdk5120-layout-guest-affinity-f3-depth.patch
```

- `xdk/5120/layout.toml`: the XDK 5120 data layouts, every value proven from this game (see `xdk5120/`).
- `runtime/kernel/kernel.h`, `threads.cpp`: `kernel::g_guest_affinity`, an opt-in host CPU mask for the threads that
  run guest code. Titles whose own locks assume the Xbox's single CPU race once their threads run in parallel.
- `runtime/app/main.cpp`: F3 (DumpThreads) scans 512 guest stack words instead of 32. Debugging aid.
