#pragma once
#include "url.h"
namespace Husarz {
enum class ResourceType : uint8_t { Html, Css, Image, Script, Fetch };
constexpr unsigned MaxCacheObjects = 32, MaxDocumentImages = 32;
constexpr size_t MaxResourceBytes = 240 * 1024, MaxCacheRaw = 2 * 1024 * 1024;
constexpr size_t MaxCacheDecoded = 16 * 1024 * 1024, MaxDocumentDecoded = 8 * 1024 * 1024;
constexpr unsigned MaxImageSide = 2048, MaxImagePixels = 384 * 1024;
struct ResourceCache;
struct ResourceEntry {
    ResourceCache *cache{}; bool transient{};
    char content_type[96]{}, etag[128]{}, modified[128]{};
    char url[2048]{}, source_url[2048]{};
    ResourceType type{}; uint16_t status{200};
    uint8_t *raw{}; size_t size{};
    uint32_t *pixels{}; unsigned width{}, height{}; size_t decoded{};
    unsigned refs{}; uint64_t used{}, created{}, expires{}; uint32_t cache_seconds{60};
};
// Slots never move. A document pins decoded storage until its destructor; LRU
// eviction only reclaims unpinned entries. No borrowed cache pointer survives a step.
struct ResourceCache {
    ResourceEntry entries[MaxCacheObjects]{};
    void *(*allocate)(size_t){}; void (*release)(void*){};
    uint64_t clock{}, hits{}, misses{}, evictions{}, image_failures{}, images_decoded{};
    size_t raw_bytes{}, decoded_bytes{};
    ResourceCache(void *(*a)(size_t),void (*f)(void*)) : allocate(a),release(f) {}
    ~ResourceCache() { for (auto &e:entries) remove(e); }
    void remove(ResourceEntry &e) {
        if (e.raw) { raw_bytes -= e.size; release(e.raw); }
        if (e.pixels) { decoded_bytes -= e.decoded; release(e.pixels); }
        e = {};
    }
    ResourceEntry *find(const char *url, ResourceType type, uint64_t now=0) {
        for (auto &e:entries) if (e.raw && e.type == type && equal(e.url,url) && (!now || !e.expires || now<e.expires)) {
            ++hits; e.used = ++clock; return &e;
        }
        ++misses; return nullptr;
    }
    bool room(size_t raw, size_t decoded, bool slot) {
        if (raw > MaxCacheRaw || decoded > MaxCacheDecoded) return false;
        for (unsigned step=0;step<=MaxCacheObjects;++step) {
            bool empty = false;
            ResourceEntry *victim = nullptr;
            for (auto &e:entries) {
                if (!e.raw) empty = true;
                else if (!e.refs && (!victim || e.used<victim->used)) victim = &e;
            }
            if (raw_bytes <= MaxCacheRaw-raw && decoded_bytes <= MaxCacheDecoded-decoded && (!slot || empty)) return true;
            if (!victim) return false;
            remove(*victim); ++evictions;
        }
        return false;
    }
    ResourceEntry *insert(const char *url,ResourceType type,const uint8_t *data,size_t size,uint64_t now=0) {
        size_t n = length(url,2048);
        if (!url || !n || !allocate || !release || !data || (!size&&type!=ResourceType::Fetch) || size > MaxResourceBytes || n == 2048 || !room(size,0,true)) return nullptr;
        for (auto &e:entries) if (!e.raw) {
            auto *copy = static_cast<uint8_t*>(allocate(size+1)); if (!copy) return nullptr;
            for (size_t i=0;i<size;++i) copy[i]=data[i];
            copy[size]=0;
            for (size_t i=0;i<=n;++i) e.url[i]=url[i];
            e.cache=this; e.raw=copy; e.size=size; e.type=type; e.used=++clock; e.created=now; e.expires=now?now+UINT64_C(60000000000):0; raw_bytes+=size; return &e;
        }
        return nullptr;
    }
    static void unpin(void *owner) {
        auto *e=static_cast<ResourceEntry*>(owner);
        if (e && e->refs) { --e->refs; if (!e->refs && e->transient && e->cache) e->cache->remove(*e); }
    }
    bool bind(Document &d, uint16_t node, ResourceEntry &e, bool background=false) {
        if (!e.pixels || d.image_count==MaxDocumentImages || e.decoded>MaxDocumentDecoded-d.image_bytes) return false;
        ++e.refs; d.images[d.image_count++]={node,e.width,e.height,e.pixels,&e,unpin,background};
        d.image_bytes+=e.decoded; return true;
    }
};
} // namespace Husarz
