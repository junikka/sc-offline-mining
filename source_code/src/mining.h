#pragma once
#include "common.h"

struct MiningSnapshot {
    bool buildVerified = false, hookReady = false, active = false, faulted = false;
    uint64_t cells = 0, promoted = 0, faults = 0;
};

// Installed during offline startup; no menu action is required.
void ResolveMiningApi();
bool MiningBuildVerified();
void GetMiningSnapshot(MiningSnapshot& out);
void ProcessMining(DWORD now, bool trace);
