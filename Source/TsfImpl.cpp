// Compiles the TinySoundFont implementation in one TU.
#define TSF_IMPLEMENTATION
#include "ThirdParty/tsf.h"

// Accessors for preset bank/program numbers (struct is private to the impl).
int pastelTsfPresetBank(const tsf* f, int i)   { return (f && i >= 0 && i < f->presetNum) ? f->presets[i].bank : -1; }
int pastelTsfPresetNumber(const tsf* f, int i) { return (f && i >= 0 && i < f->presetNum) ? f->presets[i].preset : -1; }
