#include "peerjam/logic/audio/portaudio_runtime.hpp"

#if defined(PEERJAM_HAS_PORTAUDIO)
#include <portaudio.h>
#endif

#include <mutex>

namespace peerjam {
namespace {

#if defined(PEERJAM_HAS_PORTAUDIO)
std::mutex g_pa_mutex;
int g_pa_refcount = 0;
#endif

}  // namespace

bool retain_portaudio() {
#if !defined(PEERJAM_HAS_PORTAUDIO)
    return false;
#else
    std::lock_guard lock(g_pa_mutex);
    if (g_pa_refcount == 0) {
        if (Pa_Initialize() != paNoError) {
            return false;
        }
    }
    ++g_pa_refcount;
    return true;
#endif
}

void release_portaudio() {
#if defined(PEERJAM_HAS_PORTAUDIO)
    std::lock_guard lock(g_pa_mutex);
    if (g_pa_refcount <= 0) {
        return;
    }
    --g_pa_refcount;
    if (g_pa_refcount == 0) {
        Pa_Terminate();
    }
#endif
}

}  // namespace peerjam
