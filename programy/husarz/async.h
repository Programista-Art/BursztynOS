#pragma once
#include "resource.h"
namespace Husarz {
constexpr uint32_t AsyncMagic=0x48535233;
constexpr unsigned MaxPendingRequests=48;
enum class RequestKind : uint32_t { Html, Css, Image };
struct ResourceTask {
    uint32_t request{}, tab{}, navigation{}, document{};
    uint16_t node{}; RequestKind kind{};
    bool background{}, history{}; uint8_t redirects{};
    char url[2048]{};
};
struct WorkerMessage { uint32_t magic{AsyncMagic}, type{}, request{}, reserved{}; uint64_t endpoint{}, nonce{}; };
// Less than BWS's 2 MiB per-object SHM limit. Raw bytes and pixel bytes are
// disjoint, and both producer and consumer validate every length independently.
constexpr size_t AsyncRawCapacity=256*1024;
constexpr unsigned AsyncImagePixels=384*1024;
struct WorkerPayload {
    uint32_t magic{}, request{}, kind{}, success{}, size{}, width{}, height{}, trusted{}, no_cache{}, cache_seconds{60};
    char url[2048]{};
    char content_type[96]{}, etag[128]{}, modified[128]{};
    uint8_t raw[AsyncRawCapacity]{};
    uint32_t pixels[AsyncImagePixels]{};
};
static_assert(sizeof(WorkerPayload)<2*1024*1024,"BWS shared-memory budget");
struct ResourceQueue {
    ResourceTask tasks[MaxPendingRequests]{}; unsigned count{}; uint32_t serial{};
    uint64_t discarded{}, completed{};
    bool push(ResourceTask task) {
        if (count==MaxPendingRequests) return false;
        task.request=++serial; if (!task.request) task.request=++serial;
        tasks[count++]=task; return true;
    }
    bool pop(ResourceTask &task) {
        if (!count) return false;
        task=tasks[0]; for (unsigned i=1;i<count;++i) tasks[i-1]=tasks[i]; --count; return true;
    }
    void cancel(uint32_t tab) {
        unsigned kept=0;
        for (unsigned i=0;i<count;++i) {
            if (tasks[i].tab==tab) ++discarded;
            else tasks[kept++]=tasks[i];
        }
        count=kept;
    }
    static bool valid(const ResourceTask &task,uint32_t tab,uint32_t navigation,uint32_t document) {
        return task.tab==tab && task.navigation==navigation &&
            task.document==document;
    }
};
} // namespace Husarz
