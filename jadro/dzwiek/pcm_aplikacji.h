#pragma once
#include "mikser_pcm.h"
#include "jadro/bws/pcm_api.h"
namespace PcmAplikacji {
int64_t open(Pcm::Owner owner, const BwsPcmFormat& format);
int64_t write(Pcm::Owner owner, uint64_t handle, const int16_t* samples, uint32_t frames, uint64_t copy_ns = 0);
int64_t query(Pcm::Owner owner, uint64_t handle, BwsPcmState& state);
int64_t statistics(Pcm::Owner owner, uint64_t handle, BwsPcmStats& out);
int64_t gain(Pcm::Owner owner, uint64_t handle, uint64_t volume, uint64_t mute);
int64_t close(Pcm::Owner owner, uint64_t handle);
int64_t finish(Pcm::Owner owner, uint64_t handle);
}
extern "C" bool pcm_posprzataj_proces(uint32_t pid, uint64_t generation);

extern "C" bool pcm_diag_stream(uint32_t pid, uint64_t generation, BwsPcmStats& out);

extern "C" bool pcm_diag_starvation(uint32_t index, Pcm::Starvation& out);
