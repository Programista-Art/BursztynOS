// One private in-flight document across all six tabs. The published tree is
// immutable to the layout task, so painting/hit testing never sees half a
// reflow.
Husarz::DocumentJob *document_job{};
void work_debug(const char *event, const Husarz::DocumentJob &job) {
#if HUSARZ_PERF
    char line[384];
    unsigned n = 0;
    Husarz::perf_word(line, n, "[HUSARZ WORK] ");
    Husarz::perf_word(line, n, event);
    auto field = [&](const char *name, uint64_t value) {
        Husarz::perf_word(line, n, " ");
        Husarz::perf_word(line, n, name);
        Husarz::perf_word(line, n, "=");
        Husarz::perf_number(line, n, value);
    };
    field("tab", job.identity.tab);
    field("nav", job.identity.navigation);
    field("doc", job.identity.document);
    field("revision", job.identity.revision);
    field("viewport", job.identity.viewport);
    field("width", job.width);
    field("slices", job.driver.slices);
    field("cursor", job.driver.cursor);
    field("nodes", job.result ? job.result->node_count : 0);
    Husarz::perf_word(line, n, " stage=");
    Husarz::perf_word(line, n, Husarz::stage_name(job.driver.stage));
    line[n++] = '\n';
    line[n] = 0;
    wypisz(line);
#else
    (void)event;
    (void)job;
#endif
}
uint32_t work_cursor{};
void browser_request_work(Zakladka &z) {
    ensure_tab_identity(z);
    z.work_pending = true;
}
bool browser_has_work() {
    if (document_job)
        return true;
    for (int i = 0; i < liczba_zakladek; ++i)
        if (zakladki[i].work_pending || (zakladki[i].javascript && zakladki[i].javascript->task.handle))
            return true;
    return false;
}
bool browser_active_busy() {
    if (!liczba_zakladek)
        return false;
    if (g_husarz.widok_strony && g_husarz.widok_strony->paint_pending())
        return true;
    auto &z = zakladki[aktywna_zakladka];
    if (z.javascript && !z.javascript->cancelled) {
        if (z.javascript->task.handle || z.javascript->script_index < z.dokument->script_count)
            return true;
        for (auto &timer : z.javascript->timers)
            if (timer.active)
                return true;
    }
    if (z.work_pending || (document_job && document_job->identity.tab == z.identity) ||
        (z.pending_css && z.pending_css->count))
        return true;
    if (!browser_resources)
        return false;
    auto &state = *browser_resources;
    if (state.busy && state.active.tab == z.identity && state.active.navigation == z.navigation)
        return true;
    for (unsigned i = 0; i < state.queue.count; ++i)
        if (state.queue.tasks[i].tab == z.identity && state.queue.tasks[i].navigation == z.navigation)
            return true;
    return state.launching;
}
void browser_cancel_work(uint32_t identity) {
    if (g_husarz.widok_strony)
        g_husarz.widok_strony->cancel_paint();
    (void)identity;
    if (document_job) {
        delete document_job;
        document_job = nullptr;
    }
}
Husarz::LayoutIdentity browser_identity(const Zakladka &z) {
    auto *d = z.dokument;
    return {z.identity,
            z.navigation,
            d ? d->generation : 0,
            d ? d->revision : 0,
            d ? d->style_revision : 0,
            browser_viewport.generation,
            d ? d->paint_revision : 0,
            uint16_t(d ? d->focused_control : 0),
            uint16_t(d ? d->open_select : 0)};
}
Husarz::Work build_browser_document(Husarz::DocumentJob &job, Zakladka &z) {
    const char *source =
        z.tresc && *z.tresc ? z.tresc : "Wpisz adres strony lub zapytanie i wybierz Przejdź.";
    webfonts_current = z.fonts;
    co_await job.build(z.dokument, source, Husarz::length(source, HTML_POJEMNOSC), z.to_jest_html);
    auto &d = *job.result;
    // BSF 2.2: a web font of this tab was registered since the last layout:
    // recompute styles (font families re-resolve) and reflow this document.
    if (z.fonts_changed)
        d.styles_dirty = true;
    if (d.style_viewport_height != browser_viewport.height && d.has_viewport_units)
        d.styles_dirty = true;
    d.style_viewport_height = browser_viewport.height;
#if HUSARZ_PERF
    if (job.parse && z.navigation_started)
        d.perf.resource_completions = 1;
#endif
    if (!job.parse && z.dokument) {
        auto &old = *z.dokument;
        // Pin existing images before yielding into layout; neither document
        // borrows the other's lifetime.
        for (unsigned i = 0; i < old.image_count; ++i) {
            co_await Husarz::WorkPoint{1};
            auto image = old.images[i];
            if (image.owner)
                ++static_cast<Husarz::ResourceEntry *>(image.owner)->refs;
            d.images[d.image_count++] = image;
        }
        d.image_bytes = old.image_bytes;
    }
    if (z.pending_css)
        for (unsigned i = 0; i < job.css_count; ++i) {
            auto &item = z.pending_css->items[i];
            auto &e = *item.entry;
            unsigned before_rules = d.rule_count, before_decls = d.declaration_count;
            co_await Husarz::parse_css_work(
                d, reinterpret_cast<const char *>(e.raw), e.size, uint32_t(item.node) * 4096,
                d.store(e.source_url[0] ? e.source_url : e.url,
                        Husarz::length(e.source_url[0] ? e.source_url : e.url, sizeof(e.url))));
            resource_field("parsed", e.url);
            resource_number("rules", d.rule_count - before_rules);
            resource_number("declarations", d.declaration_count - before_decls);
            resource_field("parse-result", d.truncated ? "LIMIT" : "OK");
        }
    uint16_t local =
        !job.parse && !job.css_count && z.dokument ? Husarz::local_control_update(*z.dokument, job.width) : 0;
    if (local)
        co_await Husarz::update_control_layout(d, *z.dokument, local, browser_metrics);
#if HUSARZ_PERF
    else if (!job.parse && !job.css_count && z.dokument && z.dokument->layout_width != job.width) {
        Husarz::PerfScope resize_scope(d.perf, Husarz::Stage::Resize);
        co_await Husarz::reflow_work(d, job.width, browser_metrics);
    }
#endif
    else
        co_await Husarz::reflow_work(d, job.width, browser_metrics);
#if HUSARZ_DEBUG_RESOURCES
    resource_number("document-nodes", d.node_count);
    resource_number("document-attributes", d.attr_count);
    resource_number("CSS-total-rules", d.rule_count);
    resource_number("CSS-total-declarations", d.declaration_count);
    resource_number("CSS-unsupported-selectors", d.unsupported_selectors);
    resource_number("limit-flags", d.limits);
    resource_number("arena-bytes", d.arena_used);
    resource_number("generated-boxes", d.generated_count);
    if (job.css_count || job.parse)
        for (uint16_t i = 1; i < d.node_count; ++i) {
            co_await Husarz::WorkPoint{1};
            if (d.tag(i, "body") || d.tag(i, "footer") ||
                Husarz::token_ci(d.attr(i, "class"), "btn-primary") ||
                Husarz::equal(d.attr(i, "id"), "download-button"))
                co_await debug_computed_style(d, i);
        }
    if (job.css_count)
        for (unsigned i = 0; i < d.font_count; ++i) {
            resource_field("font-family", d.str(d.fonts[i].family));
            resource_field("font-sources", d.str(d.fonts[i].sources));
            resource_field("font-result", "native fallback; web font decoder unavailable");
        }
#endif
}
void browser_step_work() {
    if (document_job) {
        Zakladka *z = nullptr;
        for (int i = 0; i < liczba_zakladek; ++i)
            if (zakladki[i].identity == document_job->identity.tab) {
                z = &zakladki[i];
                break;
            }
        if (!z || !(browser_identity(*z) == document_job->identity) ||
            z->resource_revision != document_job->resource_revision) {
            work_debug("superseded", *document_job);
            delete document_job;
            document_job = nullptr;
            if (z)
                z->work_pending = true;
        }
    }
    if (!document_job) {
        int selected = -1;
        // Prefer the visible tab, but give another pending tab every eighth turn.
        if ((++work_cursor & 7U) && liczba_zakladek && zakladki[aktywna_zakladka].work_pending)
            selected = aktywna_zakladka;
        if (selected < 0)
            for (int n = 0; n < liczba_zakladek; ++n) {
                int i = int((work_cursor + unsigned(n)) % unsigned(liczba_zakladek));
                if (zakladki[i].work_pending) {
                    selected = i;
                    break;
                }
            }
        if (selected >= 0 && zakladki[selected].javascript && zakladki[selected].javascript->task.handle) {
            selected = -1;
            for (int n = 0; n < liczba_zakladek; ++n) {
                auto &other = zakladki[n];
                if (other.work_pending && (!other.javascript || !other.javascript->task.handle)) {
                    selected = n;
                    break;
                }
            }
        }
        if (selected < 0)
            return;
        auto &z = zakladki[selected];
        auto *job = new Husarz::DocumentJob;
        if (!job)
            return;
        job->result = new Husarz::Document;
        if (!job->result) {
            delete job;
            return;
        }
        job->identity = browser_identity(z);
        job->resource_revision = z.resource_revision;
        job->width = browser_viewport.width;
        job->parse = z.dokument_brudny || !z.dokument;
        job->css_count = z.pending_css ? z.pending_css->count : 0;
#if HUSARZ_PERF
        job->driver.navigation = z.navigation;
        job->driver.document = job->result->generation;
#endif
        job->driver.allow_clock = false;
        job->driver.clock = Husarz::native_work_clock.valid ? Husarz::native_work_now : nullptr;
        job->task = build_browser_document(*job, z);
        if (!job->task.handle) {
            delete job;
            z.work_pending = false;
            ustaw_status("Brak pamięci na przetwarzanie strony.");
            return;
        }
        document_job = job;
        work_debug("start", *job);
    }
    auto &job = *document_job;
    bool done = job.task.step(job.driver);
    if (!(job.driver.slices % 32))
        work_debug("progress", job);

    if (!done)
        return;
    work_debug(job.driver.failed ? "allocation-failed" : "ready", job);
    for (int i = 0; i < liczba_zakladek; ++i) {
        auto &z = zakladki[i];
        if (!(browser_identity(z) == job.identity) || z.resource_revision != job.resource_revision)
            continue;
        z.work_pending = false;
        if (job.driver.failed) {
            ustaw_status("Brak pamięci na przetwarzanie strony.");
            break;
        }
        bool parsed = job.parse;
        if (parsed) {
            delete z.javascript;
            z.javascript = nullptr;
        }
        if (g_husarz.widok_strony)
            g_husarz.widok_strony->cancel_paint();
        delete z.dokument;
        z.dokument = job.result;
        job.result = nullptr;
        z.dokument_brudny = false;
        if (z.fonts && z.fonts->navigation == z.navigation) {
            z.fonts_changed = false;
            z.fonts->laid_out = true;
        }
        if (z.pending_css) {
            delete z.pending_css;
            z.pending_css = nullptr;
        }
        if (z.javascript)
            z.javascript->bind(*z.dokument, z.identity, z.navigation, z.url);
        [[maybe_unused]] auto &d = *z.dokument;
        HUSARZ_COUNT(d, slices, job.driver.slices);
#if HUSARZ_PERF
        if (parsed && z.navigation_started)
            d.perf.duration[unsigned(Husarz::Stage::FirstLayout)] =
                gui_czas_monotoniczny_ns() - z.navigation_started;
        if (resource_cache) {
            d.perf.cache_hits = resource_cache->hits;
            d.perf.cache_misses = resource_cache->misses;
        }
#endif
#if HUSARZ_PERF
        if (job.driver.maximum_ns > d.perf.max_slice_ns)
            d.perf.max_slice_ns = job.driver.maximum_ns;
        if (job.driver.maximum > d.perf.max_slice_work)
            d.perf.max_slice_work = job.driver.maximum;
#endif
        if (i == aktywna_zakladka) {
            browser_viewport.pending = false;
            ++browser_viewport.performed;
        }
        if (parsed) {
            kopiuj_limit(z.published_url, sizeof(z.published_url), z.url);
            queue_document_resources(z);
            if (i == aktywna_zakladka)
                zresetuj_przewijanie();
        } else if (job.css_count && !queued_css(z))
            queue_images(z);
        if (d.truncated)
            ustaw_status(d.limits && !(d.limits & (Husarz::LimitNodes | Husarz::LimitArena))
                             ? "Osiągnięto limit CSS; część stylów pominięta."
                             : "Osiągnięto limit dokumentu; wyświetlono część strony.");
        async_refresh(z);
        break;
    }
    delete document_job;
    document_job = nullptr;
}

void browser_document_painted() {
#if HUSARZ_PERF
    if (!liczba_zakladek)
        return;
    auto &z = zakladki[aktywna_zakladka];
    if (!z.dokument || !z.navigation_started || z.navigation_measured || z.work_pending ||
        (z.pending_css && z.pending_css->count))
        return;
    if (browser_resources) {
        auto &r = *browser_resources;
        if (r.busy && r.active.tab == z.identity && r.active.navigation == z.navigation)
            return;
        for (unsigned i = 0; i < r.queue.count; ++i)
            if (r.queue.tasks[i].tab == z.identity && r.queue.tasks[i].navigation == z.navigation)
                return;
    }
    z.dokument->perf.duration[unsigned(Husarz::Stage::Navigation)] =
        gui_czas_monotoniczny_ns() - z.navigation_started;
    z.navigation_measured = true;
#endif
}
