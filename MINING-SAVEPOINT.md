# Working natural-mining savepoint

Confirmed by the user on 9 October 2026: natural Titanium(Ore) highlighted in mining mode, displayed composition, responded to the mining laser, and extraction worked with the manual **Enable nearby biome spawning** experiment.

This initial commit preserves the working manual implementation before automatic startup and DEV cleanup. The corresponding compiled DLL has SHA-256 `e3af246261c01359d0d05df85526da1bbf18eead2e40dce607d3f33fd5556dd6`. The latest trace records 10 native spawn requests, all accepted, with 10 spawning-mode instances. The user report confirms gameplay beyond request acceptance.

Build from `source_code` following `source_code/docs/build.md`; the tested machine uses VS 2022 / v143 and builds the two .vcxproj files directly (see `source_code/docs/dev-mining.md`). No automatic startup changes are included in this checkpoint.

Research, decompilations, original runtime logs, compiled binaries and personal runtime state stay local and are ignored. Existing local `analysis/` and `compiled/` folders are not removed. The compact mechanism and build documentation remain versioned with the code; license and upstream attribution are preserved.

The annotated tag `mining-working-manual` identifies this checkpoint. To inspect/rebuild it without resetting your current checkout, use `git worktree add ../sc-offline-mining-manual mining-working-manual` and build there. Only source/build documentation and intended static assets are tracked. Streaming/depletion after leaving and returning, and persistence across restarts, remain unverified.
