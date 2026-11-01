#pragma once

// Called once guest RAM has been mapped, before any guest/render workers run.
// ARM32 Android chooses its host base at runtime, so resource views cannot be
// captured during shared-library static initialization.
void RefreshNativeResourceMemory();
