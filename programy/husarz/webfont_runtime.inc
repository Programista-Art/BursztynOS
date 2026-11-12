// BSF 2.2 — Husarz web fonts (CSS @font-face), browser process side.
// Included by przegladarka.cpp after async_runtime.inc.
//
// Ownership: every tab owns at most one BrowserFonts set, tied to its
// navigation. Faces are registered in the BSF browser scope of that set
// (BSF::register_scoped_face) and released when the tab navigates, is
// cleared or closed. The system font catalog and /czcionki are never
// touched; a page can only reach faces of its own scope because the scoped
// family names are produced here, never from page CSS.
#if defined(BSF_NATIVE) && !defined(HUSARZ_WORKER_ONLY)
#define HUSARZ_WEBFONTS 1
#else
#define HUSARZ_WEBFONTS 0
#endif
enum WebFontState : uint8_t { WebFontIdle, WebFontQueued, WebFontLoaded, WebFontFailed, WebFontLocal, WebFontIgnored };
struct BrowserFontFace {
    uint8_t state{}, source{};
    uint32_t id{};
    char family[96]{}, local_family[96]{};
    Husarz::FontFaceDescriptor desc{};
};
struct BrowserFonts {
    uint64_t scope{};
    uint32_t navigation{}, document{};
    unsigned count{}, loaded{}, failed{}, requests{};
    bool laid_out{}; // first layout published (font-display: optional)
    BrowserFontFace faces[Husarz::MaxFontFaces]{};
};
uint64_t webfont_serial{};
// Native verification counters (read by narzedzia/bsf2_native_qemu.py).
struct WebFontCounters { uint64_t requests, loaded, failed, cors_denied, local, skipped_format, released; };
WebFontCounters webfont_counters{};
// Fonts of the tab whose document job is running (style computation).
BrowserFonts *webfonts_current{};
void webfonts_debug(const char *event, const char *detail) {
    resource_field(event, detail);
}
void webfonts_release(Zakladka &z) {
    if (!z.fonts) return;
#if HUSARZ_WEBFONTS
    BSF::release_scope(z.fonts->scope);
#endif
    if (webfonts_current == z.fonts) webfonts_current = nullptr;
    ++webfont_counters.released;
    delete z.fonts;
    z.fonts = nullptr;
    z.fonts_changed = false;
}
uint64_t webfont_new_scope(const Zakladka &z) {
    // Unpredictable within the process and never reused.
    uint64_t v = gui_czas_monotoniczny_ns() ^ (uint64_t(z.identity) << 40) ^ (++webfont_serial * UINT64_C(0x9e3779b97f4a7c15));
    v ^= v >> 29; v *= UINT64_C(0xbf58476d1ce4e5b9); v ^= v >> 32;
    return v ? v : 1;
}
// Copies the CSS family descriptor value (first family, unquoted).
bool webfont_family(const char *value, char *out, size_t cap) {
    size_t n = Husarz::length(value, Husarz::MaxAttrLength), i = 0;
    while (i < n && Husarz::font_space(value[i])) ++i;
    size_t end = n;
    while (end > i && Husarz::font_space(value[end - 1])) --end;
    if (end - i >= 2 && (value[i] == '"' || value[i] == '\'') && value[end - 1] == value[i]) { ++i; --end; }
    if (end <= i || end - i >= cap) return false;
    for (size_t k = i; k < end; ++k) { if (uint8_t(value[k]) < 32) return false; out[k - i] = value[k]; }
    out[end - i] = 0;
    return true;
}
bool webfont_used(const Husarz::Document &d, const char *family) {
    size_t fn = Husarz::length(family, 96);
    for (uint16_t i = 1; i < d.node_count; ++i) {
        const char *list = d.str(d.styles[i].family);
        size_t n = Husarz::length(list, Husarz::MaxAttrLength);
        for (size_t at = 0; at < n;) {
            while (at < n && (Husarz::font_space(list[at]) || list[at] == ',' || list[at] == '"' || list[at] == '\'')) ++at;
            size_t b = at;
            while (at < n && list[at] != ',') ++at;
            size_t e = at;
            while (e > b && (Husarz::font_space(list[e - 1]) || list[e - 1] == '"' || list[e - 1] == '\'')) --e;
            if (e - b == fn && Husarz::font_ci(list + b, e - b, family)) return true;
        }
    }
    return false;
}
void webfont_queue(Zakladka &z, unsigned index);
// Syncs the tab's font set with its published document and queues loads.
void queue_fonts(Zakladka &z) {
    if (!browser_resources || !z.dokument || !z.to_jest_html) return;
    auto &d = *z.dokument;
    if (z.fonts && z.fonts->navigation != z.navigation) webfonts_release(z);
    if (!d.font_count) return;
    if (!z.fonts) {
        z.fonts = new BrowserFonts;
        if (!z.fonts) return;
        z.fonts->scope = webfont_new_scope(z);
        z.fonts->navigation = z.navigation;
    }
    auto &set = *z.fonts;
    set.document = d.generation;
    for (unsigned i = set.count; i < d.font_count && i < Husarz::MaxFontFaces; ++i) {
        auto &f = set.faces[i];
        f = BrowserFontFace{};
        const auto &rule = d.fonts[i];
        if (!rule.family || !rule.sources || !webfont_family(d.str(rule.family), f.family, sizeof(f.family))) {
            f.state = WebFontFailed;
            continue;
        }
        auto value = [&](uint32_t v, const char *fallback) { return v ? d.str(v) : fallback; };
        const char *w = value(rule.weight, "400"), *st = value(rule.style, "normal"),
                   *sh = value(rule.stretch, "normal"), *ds = value(rule.display, "auto");
        Husarz::parse_font_weight(w, Husarz::length(w, 128), f.desc.weight_min, f.desc.weight_max);
        f.desc.italic = Husarz::parse_font_italic(st, Husarz::length(st, 128));
        f.desc.stretch = Husarz::parse_font_stretch(sh, Husarz::length(sh, 128));
        f.desc.display = Husarz::parse_font_display(ds, Husarz::length(ds, 128));
        if (rule.unicode_range) {
            const char *r = d.str(rule.unicode_range);
            f.desc.range_count = uint8_t(Husarz::parse_unicode_ranges(r, Husarz::length(r, Husarz::MaxAttrLength), f.desc.ranges, Husarz::MaxUnicodeRanges));
            if (!f.desc.range_count) { f.state = WebFontFailed; continue; } // invalid descriptor: rule ignored
        }
    }
    if (d.font_count > set.count) set.count = d.font_count < Husarz::MaxFontFaces ? d.font_count : Husarz::MaxFontFaces;
    for (unsigned i = 0; i < set.count; ++i)
        if (set.faces[i].state == WebFontIdle && webfont_used(d, set.faces[i].family)) webfont_queue(z, i);
}
void webfont_failed(Zakladka &z, unsigned index) {
    auto &f = z.fonts->faces[index];
    f.state = WebFontFailed;
    ++z.fonts->failed;
    ++webfont_counters.failed;
    webfonts_debug("font-result", "failed; local fallback retained");
}
// Tries sources from f.source onwards (in CSS order).
void webfont_queue(Zakladka &z, unsigned index) {
    if (!z.fonts || !z.dokument || index >= z.fonts->count) return;
    auto &d = *z.dokument;
    auto &f = z.fonts->faces[index];
    const auto &rule = d.fonts[index];
    struct Scratch { Husarz::FontSource sources[Husarz::MaxFontSources]{}; ParsedUrl base{}; Husarz::ResourceTask task{}; };
    auto *s = new Scratch;
    if (!s) return;
    const char *src = d.str(rule.sources);
    unsigned n = Husarz::parse_font_sources(src, Husarz::length(src, Husarz::MaxAttrLength), s->sources);
    bool base = (rule.base_url && parsuj_url(d.str(rule.base_url), &s->base)) || document_url_base(z.url, z.dokument, s->base);
    for (; f.source < n; ++f.source) {
        const auto &source = s->sources[f.source];
        if (source.local) {
#if HUSARZ_WEBFONTS
            uint32_t id = BSF::resolve_family(source.value);
            if (id && id != 0xffffffffu && BSF::family_name(id) && Husarz::font_ci(BSF::family_name(id), Husarz::length(BSF::family_name(id), 96), source.value)) {
                kopiuj_limit(f.local_family, sizeof(f.local_family), BSF::family_name(id));
                f.state = WebFontLocal; z.fonts_changed = true; ++f.source; ++webfont_counters.local;
                webfonts_debug("font-local", source.value);
                delete s; return;
            }
#endif
            continue;
        }
        if (source.format == Husarz::FontFormat::Unsupported) { ++webfont_counters.skipped_format; webfonts_debug("font-skip-format", source.value); continue; }
        auto &task = s->task; task = {};
        task.tab = z.identity; task.navigation = z.navigation; task.document = d.generation;
        task.kind = Husarz::RequestKind::Font; task.node = uint16_t(index);
        if (!base || !ustaw_url_z_location(task.url, sizeof(task.url), source.value, s->base)) continue;
        if (!browser_resources->queue.push(task)) { d.limit(Husarz::LimitStylesheets); break; }
        f.state = WebFontQueued; ++f.source; ++z.fonts->requests; ++webfont_counters.requests;
        webfonts_debug("font-request", task.url);
        delete s; async_dispatch(); return;
    }
    delete s;
    webfont_failed(z, index);
}
// Worker result for a Font task (payload validated by the caller).
void webfont_apply(const Husarz::ResourceTask &task, const Husarz::WorkerPayload &payload) {
    auto *z = task_tab(task);
    if (!z || !z->fonts || z->fonts->navigation != task.navigation || task.node >= z->fonts->count) return;
    auto &f = z->fonts->faces[task.node];
    if (f.state != WebFontQueued) return;
    bool ok = payload.success && payload.size >= 12 && payload.size <= Husarz::AsyncFontCapacity;
    if (ok && !Husarz::font_cors_allowed(z->url, payload.url[0] ? payload.url : task.url, payload.allow_origin)) {
        webfonts_debug("font-error", "CORS_DENIED");
        ++webfont_counters.cors_denied;
        ok = false;
    }
#if HUSARZ_WEBFONTS
    if (ok) {
        auto *bytes = new uint8_t[payload.size];
        uint32_t id = 0;
        if (bytes) {
            const uint8_t *from = Husarz::font_area(payload);
            for (size_t i = 0; i < payload.size; ++i) bytes[i] = from[i];
            BSF::ScopedDescriptor desc;
            desc.weight_min = f.desc.weight_min; desc.weight_max = f.desc.weight_max;
            desc.stretch = f.desc.stretch; desc.italic = f.desc.italic; desc.range_count = f.desc.range_count;
            for (unsigned r = 0; r < f.desc.range_count && r < BSF::MaxScopedRanges; ++r) {
                desc.ranges[r][0] = f.desc.ranges[r][0]; desc.ranges[r][1] = f.desc.ranges[r][1];
            }
            id = BSF::register_scoped_face(z->fonts->scope, f.family, desc, bytes, payload.size);
            if (!id) delete[] bytes; // ownership passes to BSF only on success
        }
        if (id) {
            f.id = id;
            ++z->fonts->loaded;
            ++webfont_counters.loaded;
            // font-display: optional never swaps after the first layout.
            if (f.desc.display == Husarz::FontDisplay::Optional && z->fonts->laid_out) {
                f.state = WebFontIgnored;
                webfonts_debug("font-result", "loaded late; font-display optional keeps fallback");
                return;
            }
            f.state = WebFontLoaded;
            z->fonts_changed = true; ++z->resource_revision; browser_request_work(*z);
            webfonts_debug("font-result", "loaded");
            return;
        }
        webfonts_debug("font-error", "BSF_VALIDATION");
    }
#endif
    // Next source in CSS order, else the local fallback stays.
    f.state = WebFontIdle;
    webfont_queue(*z, task.node);
}
// Husarz::font_family_resolver: rewrites families that have a loaded face
// in the current tab's scope to their scoped (or local()) family.
uint32_t browser_family_resolver(const char *list) {
#if HUSARZ_WEBFONTS
    auto *set = webfonts_current;
    char out[1024];
    char name[96];
    bool changed = Husarz::rewrite_font_families(list, out, sizeof(out), [&](const char *family, size_t n) -> const char * {
        for (unsigned i = 0; set && i < set->count; ++i) {
            const auto &f = set->faces[i];
            if ((f.state != WebFontLoaded && f.state != WebFontLocal) || !Husarz::font_ci(family, n, f.family)) continue;
            if (f.state == WebFontLocal) return f.local_family;
            return BSF::scoped_family(set->scope, f.family, name, sizeof(name)) ? name : nullptr;
        }
        return nullptr;
    });
    return BSF::resolve_family(changed ? out : list);
#else
    (void)list;
    return 0;
#endif
}
