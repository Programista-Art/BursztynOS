struct BrowserResources {
    Husarz::ResourceQueue queue{}; Husarz::ResourceTask active{};
    uint64_t endpoint{}, worker{}, worker_pid{}, nonce{}, shared{}, started{}, last_launch{};
    unsigned launch_attempts{};
    Husarz::WorkerPayload *payload{};
    Husarz::Work publication{};Husarz::WorkDriver publication_driver{};
    uint32_t next_tab{}; bool busy{}, launching{};
};
BrowserResources *browser_resources{};
void resource_debug(const char *event,const Husarz::ResourceTask *task=nullptr) {
#if HUSARZ_DEBUG_RESOURCES
    char line[384]="[HUSARZ] "; size_t used=9;
    for (size_t i=0;event[i] && used<100;++i) line[used++]=event[i];
    if (task) {
        const char *label=" nav="; for (unsigned i=0;label[i];++i) line[used++]=label[i];
        append_decimal(line,used,task->navigation);
        label=" doc="; for (unsigned i=0;label[i];++i) line[used++]=label[i];
        append_decimal(line,used,task->document); line[used++]=' ';
        for (size_t i=0;task->url[i] && used<250;++i) line[used++]=task->url[i];
    }
    if (resource_cache) {
        const char *label=" hits="; for(unsigned i=0;label[i];++i) line[used++]=label[i]; append_decimal(line,used,resource_cache->hits);
        label=" misses="; for(unsigned i=0;label[i];++i) line[used++]=label[i]; append_decimal(line,used,resource_cache->misses);
        label=" images="; for(unsigned i=0;label[i];++i) line[used++]=label[i]; append_decimal(line,used,resource_cache->images_decoded);
        label=" fail="; for(unsigned i=0;label[i];++i) line[used++]=label[i]; append_decimal(line,used,resource_cache->image_failures);
    }
    line[used++]='\n'; line[used]=0; wypisz(line);
#else
    (void)event; (void)task;
#endif
}
void queue_document_resources(Zakladka &);
void async_dispatch();
[[maybe_unused]] void launch_browser_worker(BrowserResources &state) {
#ifndef HUSARZ_HOST_TEST
    char argument[96]="/husarz-worker/"; size_t position=15;
    append_decimal(argument,position,state.endpoint); argument[position++]='/'; append_decimal(argument,position,state.nonce);
    // BWS10 can return success after delivering an argument to an older,
    // single-instance worker. Only our nonce-matched hello proves ownership.
    (void)uruchom_program_z_argumentem_uzytkownika("/programy/husarz-worker.cebula/husarz-worker.bur",argument);
    state.last_launch=gui_czas_monotoniczny_ns();++state.launch_attempts;
#else
    (void)state;
#endif
}
bool storage_task(const Husarz::ResourceTask &task){return task.kind==Husarz::RequestKind::StorageRead||task.kind==Husarz::RequestKind::StorageWrite;}

Zakladka *task_tab(const Husarz::ResourceTask &task) {
    for (int i=0;i<liczba_zakladek;++i) {
        auto &z=zakladki[i];
        if (Husarz::ResourceQueue::valid(task,z.identity,z.navigation,z.dokument?z.dokument->generation:0)) return &z;
    }
    return nullptr;
}
void async_release_shared();
void cancel_async_tab(Zakladka &z) {
    if(browser_resources && browser_resources->publication.handle && browser_resources->active.tab==z.identity)async_release_shared();
    z.navigation_started=0;z.navigation_measured=false;
    browser_cancel_work(z.identity);z.work_pending=false;z.dokument_brudny=false;delete z.pending_css;z.pending_css=nullptr;
    delete z.javascript;z.javascript=nullptr;
    ++z.navigation; if (!z.navigation) ++z.navigation;
    if (browser_resources) browser_resources->queue.cancel(z.identity);
}
[[maybe_unused]] void async_release_shared() {
    if (!browser_resources) return;
    auto &state=*browser_resources;
    state.publication.cancel();state.publication_driver={};
#ifndef HUSARZ_HOST_TEST
    if (state.payload) shm_unmap(state.payload);
    if (state.shared) shm_close(state.shared);
#endif
    state.payload=nullptr; state.shared=0; state.busy=false;
}
void async_shutdown() {
    for(auto *s:Husarz::browser_storage.local)if(s){if(s->loading){s->loaded=true;s->failed=true;}s->loading=s->saving=false;}
    for(int i=0;i<liczba_zakladek;++i)if(auto *r=zakladki[i].javascript)for(unsigned f=0;f<r->fetch_count;++f)if(r->fetches[f].active&&!r->fetches[f].ready)r->supply_fetch(f,nullptr);
#ifndef HUSARZ_HOST_TEST
    if (browser_resources) {
        auto &state=*browser_resources;
        if (state.worker) { Husarz::WorkerMessage stop{}; stop.type=4; stop.nonce=state.nonce; worker_send(state.worker,stop); }
        if (state.endpoint) ipc_close(state.endpoint);
        async_release_shared(); delete browser_resources; browser_resources=nullptr;
    }
#endif
}
[[maybe_unused]] void begin_async_navigation(Zakladka &z,bool history) {
#ifndef HUSARZ_HOST_TEST
    if (!browser_resources) browser_resources=new BrowserResources;
    if (!browser_resources) { ustaw_status("Brak pamięci na zadania sieciowe."); return; }
    if (!z.dokument) przygotuj_dokument_zakladki(z,false);
    ensure_tab_identity(z);
    cancel_async_tab(z);z.navigation_started=gui_czas_monotoniczny_ns();z.navigation_measured=false;
    auto *task=new Husarz::ResourceTask{}; if (!task) return;
    task->tab=z.identity; task->navigation=z.navigation; task->document=z.dokument?z.dokument->generation:0; task->kind=Husarz::RequestKind::Html; task->history=history;
    if (kopiuj_limit(task->url,sizeof(task->url),z.url)) browser_resources->queue.push(*task);
    delete task;
    ustaw_status("Ładowanie strony..."); async_dispatch();
#else
    (void)z; (void)history;
#endif
}
void async_refresh(Zakladka &z) {
    if (liczba_zakladek>0 && &z==&zakladki[aktywna_zakladka]) {
        if (g_husarz.txt_adres && (!g_husarz.okno || g_husarz.okno->fokus()!=g_husarz.txt_adres)) g_husarz.txt_adres->ustaw_tekst(z.url);
        if (g_husarz.widok_strony) g_husarz.widok_strony->przygotuj();
        g_husarz.aktualizuj_pasek_kart(); g_husarz.aktualizuj_pasek_stanu();
    }
}
void queue_images(Zakladka &z) {
    if (!browser_resources || !z.dokument) return;
    auto &d=*z.dokument;
    if (d.styles_dirty || z.work_pending || (z.pending_css && z.pending_css->count)) return;
    queue_fonts(z);
    struct Scratch { ParsedUrl base{},css{}; char source[URL_POJEMNOSC]{}; Husarz::ResourceTask task{}; };
    auto *scratch=new Scratch; if (!scratch) return;
    if (!document_url_base(z.url,z.dokument,scratch->base)) { delete scratch; return; }
    unsigned queued=0;
    for (uint16_t i=1;i<d.node_count && queued<Husarz::MaxDocumentImages;++i) {
        if (d.nodes[i].is_text) continue;
        for (unsigned bg=0;bg<2 && queued<Husarz::MaxDocumentImages;++bg) {
            const char *src=bg?d.str(d.styles[i].background_image):d.tag(i,"img")?d.attr(i,"src"):"";
            if (!*src) continue;
            if (bg) {
                Husarz::Span value{src,Husarz::length(src,Husarz::MaxAttrLength)};
                if (value.size<5) continue;
                value=Husarz::trimmed(src+4,value.size-5);
                if (value.size>=2 && (value.data[0]=='\'' || value.data[0]=='"') && value.data[value.size-1]==value.data[0]) { ++value.data; value.size-=2; }
                if (!value.size || value.size>=sizeof(scratch->source)) continue;
                for (size_t j=0;j<value.size;++j) scratch->source[j]=value.data[j];
                scratch->source[value.size]=0; src=scratch->source;
            }
            const ParsedUrl *base=&scratch->base;
            if (bg && d.styles[i].background_base && parsuj_url(d.str(d.styles[i].background_base),&scratch->css)) base=&scratch->css;
            auto &task=scratch->task; task={}; task.tab=z.identity; task.navigation=z.navigation; task.document=d.generation;
            task.node=i;task.node_identity=d.nodes[i].identity; task.kind=Husarz::RequestKind::Image; task.background=bg!=0;
            if (ustaw_url_z_location(task.url,sizeof(task.url),src,*base)) {
                if (!browser_resources->queue.push(task)) {d.limit(Husarz::LimitStylesheets);resource_field("error","RESOURCE_QUEUE_LIMIT");}
                ++queued;
            }
        }
    }
    delete scratch;
}
bool queued_css(const Zakladka &z) {
    if (!browser_resources || !z.dokument) return false;
    auto &q=browser_resources->queue;
    for (unsigned i=0;i<q.count;++i) if (q.tasks[i].kind==Husarz::RequestKind::Css &&
        q.tasks[i].tab==z.identity && q.tasks[i].navigation==z.navigation && q.tasks[i].document==z.dokument->generation) return true;
    return false;
}
void queue_document_scripts(Zakladka &);
void queue_document_resources(Zakladka &z) {
    if (!browser_resources || !z.dokument || !z.to_jest_html) return;
    struct Scratch { ParsedUrl base{}; Husarz::ResourceTask task{}; };
    auto *scratch=new Scratch; if (!scratch) return;
    unsigned count=0;
    if (document_url_base(z.url,z.dokument,scratch->base)) for (uint16_t i=1;i<z.dokument->node_count;++i) {
        auto &d=*z.dokument;
        if (!d.tag(i,"link") || !Husarz::token_ci(d.attr(i,"rel"),"stylesheet")) continue;
        auto &task=scratch->task; task={}; task.tab=z.identity; task.navigation=z.navigation; task.document=d.generation;
        task.node=i;task.node_identity=z.dokument->nodes[i].identity; task.kind=Husarz::RequestKind::Css;
        resource_field("original",d.attr(i,"href"));resource_field("base",z.url);
        if(count==Husarz::MaxStylesheets){d.limit(Husarz::LimitStylesheets);resource_field("error","STYLESHEET_COUNT_LIMIT");continue;}
        if (ustaw_url_z_location(task.url,sizeof(task.url),d.attr(i,"href"),scratch->base)) {
            resource_address(task.url,Husarz::ResourceType::Css);resource_field("same-origin",Husarz::Origin::parse(task.url).same(Husarz::Origin::parse(z.url))?"yes":"no");
            if (!browser_resources->queue.push(task)) {d.limit(Husarz::LimitStylesheets);resource_field("error","RESOURCE_QUEUE_LIMIT");}
            ++count;
        }
    }
    delete scratch;
    queue_document_scripts(z);
    if (!count) queue_images(z);
}
void finish_resource_task(const Husarz::ResourceTask &task,Husarz::ResourceEntry *entry) {
    auto *z=task_tab(task); if (!z || !z->dokument) return;
    auto &d=*z->dokument;
    if(task.kind==Husarz::RequestKind::Fetch){if(z->javascript)z->javascript->supply_fetch(task.script,entry);return;}
    if(task.kind==Husarz::RequestKind::Script){if(z->javascript)z->javascript->supply(task.script,entry);return;}
    uint16_t target=task.node;if(task.node_identity){target=0;for(uint16_t i=1;i<d.node_count;++i)if(d.nodes[i].identity==task.node_identity){target=i;break;}if(!target)return;}
    HUSARZ_SCOPE(d, Publication);
#if HUSARZ_PERF
    if(resource_cache){d.perf.cache_hits=resource_cache->hits;d.perf.cache_misses=resource_cache->misses;}
#endif
    if (task.kind==Husarz::RequestKind::Css) {
        if(entry){
            if(!z->pending_css)z->pending_css=new PendingCss;
            if(z->pending_css && z->pending_css->count<Husarz::MaxStylesheets){++entry->refs;z->pending_css->items[z->pending_css->count++]={entry,target};++z->resource_revision;browser_request_work(*z);}
        }
        if(!entry && !queued_css(*z))queue_images(*z);
    } else if (entry && resource_cache) {
        if (resource_cache->bind(d,target,*entry,task.background)) {
            ++z->resource_revision;
            const auto &style=d.styles[target];
            bool fixed=task.background || ((style.width.unit!=Husarz::Unit::Auto || d.has_attr(target,"width")) && (style.height.unit!=Husarz::Unit::Auto || d.has_attr(target,"height")));
            if(fixed && (task.background || Husarz::publish_fixed_image_paint(d,target,browser_metrics,browser_glyphs)))d.invalidate(target,1);
            else {d.invalidate(target,4);browser_request_work(*z);}
        }
    }
    async_refresh(*z);
}
[[maybe_unused]] Husarz::Work apply_worker_result_work() {
    HUSARZ_PROGRESS(Publication,0,1);
    auto &state=*browser_resources; const auto &task=state.active;
    if(storage_task(task)){
        if(!state.payload||state.payload->magic!=Husarz::AsyncMagic||state.payload->request!=task.request||state.payload->kind!=uint32_t(task.kind)||state.payload->size>sizeof(state.payload->raw))co_return;
        if(task.script>=Husarz::StorageOrigins){co_return;}auto *s=Husarz::browser_storage.local[task.script];if(!s){co_return;}auto &p=*state.payload;
        if(task.kind==Husarz::RequestKind::StorageRead){
            s->loading=false;co_await Husarz::WorkPoint{256};
            if(p.success&&p.size<=Husarz::StorageDiskCapacity&&Husarz::storage_decode(*s,p.raw,p.size,s->origin))s->loaded=true;else {s->loaded=true;s->failed=true;}
        }else {s->saving=false;if(p.success)s->saved=task.storage_revision;else s->failed=true;}co_return;
    }
    auto *z=task_tab(task);
    if (!z) { ++state.queue.discarded; resource_debug("discard",&task); co_return; }
    auto &payload=*state.payload;
    if(z->dokument){HUSARZ_COUNT(*z->dokument,resource_completions,1);}
    if (payload.magic!=Husarz::AsyncMagic || payload.request!=task.request || payload.kind!=uint32_t(task.kind) ||
        payload.size>(task.kind==Husarz::RequestKind::Font?Husarz::AsyncFontCapacity:sizeof(payload.raw))) {
        ++state.queue.discarded; co_return;
    }
    ++state.queue.completed;
    resource_debug("complete",&task);
    if (task.kind==Husarz::RequestKind::Font) { webfont_apply(task,payload); co_return; }
    if (task.kind==Husarz::RequestKind::Html) {
        if (!payload.success) ustaw_tresc(*z,"Nie udało się pobrać strony.",false);
        else {
            struct Scratch { ParsedUrl parsed{}; char redirect[URL_POJEMNOSC]{}; };
            auto *scratch=new Scratch; if (!scratch) co_return;
            bool redirect=false;
            if (parsuj_url(task.url,&scratch->parsed)) consume_http_response(*z,&redirect,scratch->redirect,scratch->parsed,
                reinterpret_cast<const char*>(payload.raw),payload.size,payload.trusted!=0);
            if (redirect && task.redirects<MAX_PRZEKIEROWAN) {
                auto *next=new Husarz::ResourceTask(task);
                if (next) { ++next->redirects; kopiuj_limit(next->url,sizeof(next->url),scratch->redirect);
                    kopiuj_limit(z->url,sizeof(z->url),scratch->redirect); state.queue.push(*next); delete next; }
                delete scratch; co_return;
            }
            if (redirect) ustaw_tresc(*z,"Przekroczono limit przekierowań.",false);
            delete scratch;
        }
        przygotuj_dokument_zakladki(*z,false);
        if (task.history) dopisz_do_historii(z->url);
        async_refresh(*z);
    } else {
        auto *cache=cache_resources(); Husarz::ResourceEntry *entry=nullptr;
        // A canceled pixel copy must not leave an undecoded cache entry that
        // shadows a later successful entry for the same URL.
        struct PendingImage {
            Husarz::ResourceEntry *entry{};
            ~PendingImage(){if(entry && entry->cache && !entry->refs && !entry->pixels)entry->cache->remove(*entry);}
        } pending_image;
        if (payload.success && cache && (payload.size||task.kind==Husarz::RequestKind::Fetch) && payload.size<=Husarz::MaxResourceBytes) {
            auto type=Husarz::resource_type(task.kind);
            entry=cache->insert(task.url,type,payload.raw,payload.size,gui_czas_monotoniczny_ns());
            if(entry) {
                kopiuj_limit(entry->source_url,sizeof(entry->source_url),payload.url);
                kopiuj_limit(entry->content_type,sizeof(entry->content_type),payload.content_type);
                kopiuj_limit(entry->etag,sizeof(entry->etag),payload.etag);
                kopiuj_limit(entry->modified,sizeof(entry->modified),payload.modified);
                entry->status=uint16_t(payload.status);entry->cache_seconds=payload.cache_seconds>3600?3600:payload.cache_seconds;
                entry->expires=entry->created?entry->created+uint64_t(entry->cache_seconds)*1000000000:0;
            }
            if (entry && task.kind==Husarz::RequestKind::Image) {
                pending_image.entry=entry;
                uint32_t w=payload.width,h=payload.height;
                if (!w || !h || w>Husarz::MaxImageSide || h>Husarz::MaxImageSide || uint64_t(w)*h>Husarz::AsyncImagePixels) {
                    ++cache->image_failures; entry=nullptr;
                } else {
                    size_t bytes=size_t(w)*h*4; ++entry->refs; bool room=cache->room(0,bytes,false); --entry->refs;
                    if (room) {
                        struct PixelOwner{uint32_t *value{};~PixelOwner(){if(value)gui_free(value);}} owned;
                        auto *pixels=owned.value=static_cast<uint32_t*>(gui_malloc(bytes));
                        if (pixels) {
                            for(size_t i=0;i<size_t(w)*h;++i){if(!(i&63))co_await Husarz::WorkPoint{1};pixels[i]=payload.pixels[i];}
                            entry->pixels=pixels;owned.value=nullptr; entry->width=w; entry->height=h; entry->decoded=bytes;
                            cache->decoded_bytes+=bytes; ++cache->images_decoded;
                        }
                    }
                }
            }
        } else if (cache && task.kind==Husarz::RequestKind::Image) ++cache->image_failures;
        if (entry && payload.no_cache) { entry->transient=true; entry->url[0]=0; }
        finish_resource_task(task,entry);
        if (entry && entry->transient && !entry->refs && cache) cache->remove(*entry);
    }
}
[[maybe_unused]] void apply_worker_result(){Husarz::finish_work(apply_worker_result_work());}
void async_receive() {
#ifndef HUSARZ_HOST_TEST
    if (!browser_resources || !browser_resources->endpoint) return;
    auto &state=*browser_resources;
    BwsIpcOdbierz incoming{};
    for (unsigned step=0;step<8 && ipc_receive(state.endpoint,&incoming);++step) {
        if (incoming.shm_handle) { shm_close(incoming.shm_handle); continue; }
        if (incoming.rozmiar!=sizeof(Husarz::WorkerMessage)) continue;
        Husarz::WorkerMessage message{}; memcpy(&message,incoming.dane,sizeof(message));
        if (message.magic!=Husarz::AsyncMagic || message.nonce!=state.nonce) continue;
        if (message.type==1 && state.launching) {
            state.worker=message.endpoint; state.worker_pid=incoming.nadawca_pid; state.launching=false;
        } else if (message.type==3 && state.busy && incoming.nadawca_pid==state.worker_pid && message.request==state.active.request) {
            if(!state.publication.handle){state.publication_driver={};state.publication_driver.allow_clock=false;state.publication_driver.clock=Husarz::native_work_clock.valid?Husarz::native_work_now:nullptr;state.publication=apply_worker_result_work();}
        }
    }
    async_dispatch();
#endif
}
[[maybe_unused]] void async_dispatch() {
#ifndef HUSARZ_HOST_TEST
    if (!browser_resources) return;
    auto &state=*browser_resources;
    if (state.busy || state.launching || !state.queue.count) return;
    if (!state.worker) {
        if (!state.endpoint) state.endpoint=ipc_create();
        if (!state.endpoint) return;
        state.nonce=gui_czas_monotoniczny_ns()^state.endpoint^UINT64_C(0xb59cfae71);
        if (!state.nonce) state.nonce=1;
        state.launching=true;state.launch_attempts=0;
        state.started=gui_czas_monotoniczny_ns();
        launch_browser_worker(state);
        return;
    }
    // Only one worker request can use the native global network session at once.
    for (unsigned step=0;step<4 && state.queue.pop(state.active);++step) {
        if (!storage_task(state.active)&&!task_tab(state.active)) { ++state.queue.discarded; continue; }
        struct Url { char text[2048]{}; }; auto *url=new Url;
        if (!url) return;
        bool normalized=storage_task(state.active)?kopiuj_limit(url->text,sizeof(url->text),state.active.url):Husarz::normalize_resource_url(state.active.url,url->text,sizeof(url->text));
        if (normalized) kopiuj_limit(state.active.url,sizeof(state.active.url),url->text);
        delete url;
        if (!normalized) continue;
        if (state.active.kind!=Husarz::RequestKind::Html&&state.active.kind!=Husarz::RequestKind::Font&&!storage_task(state.active)) {
            auto *cache=cache_resources();
            auto type=Husarz::resource_type(state.active.kind);
            auto *entry=cache?cache->find(state.active.url,type,gui_czas_monotoniczny_ns()):nullptr;
            if (entry && (type!=Husarz::ResourceType::Image || entry->pixels)) { resource_debug("cache-hit",&state.active); finish_resource_task(state.active,entry); continue; }
        }
        state.shared=shm_create(sizeof(Husarz::WorkerPayload),BWS_SHM_READ|BWS_SHM_WRITE);
        state.payload=state.shared?static_cast<Husarz::WorkerPayload*>(shm_map(state.shared,BWS_SHM_READ|BWS_SHM_WRITE)):nullptr;
        if (!state.payload) { async_release_shared(); return; }
        state.payload->magic=Husarz::AsyncMagic; state.payload->request=state.active.request; state.payload->kind=uint32_t(state.active.kind);
        kopiuj_limit(state.payload->url,sizeof(state.payload->url),state.active.url);
        if(state.active.kind==Husarz::RequestKind::StorageWrite){auto *s=state.active.script<Husarz::StorageOrigins?Husarz::browser_storage.local[state.active.script]:nullptr;state.payload->size=s?Husarz::storage_encode(*s,state.payload->raw,sizeof(state.payload->raw)):0;state.active.storage_revision=s?s->revision:0;}
        Husarz::WorkerMessage request{}; request.type=2; request.request=state.active.request; request.nonce=state.nonce;
        if (!worker_send(state.worker,request,state.shared)) { async_release_shared(); state.worker=0; state.queue.push(state.active); return; }
        state.busy=true; state.started=gui_czas_monotoniczny_ns(); resource_debug("request",&state.active); return;
    }
#endif
}
void async_tick() {
#ifndef HUSARZ_HOST_TEST
    if (browser_resources) {
        auto &state=*browser_resources; uint64_t now=gui_czas_monotoniczny_ns();
        if ((state.busy || state.launching) && now && state.started && now-state.started>UINT64_C(45000000000)) {
            state.queue.count=0; async_shutdown(); ustaw_status("Przekroczono czas oczekiwania na sieć."); g_husarz.aktualizuj_pasek_stanu(); return;
        }
        if(state.launching && state.launch_attempts<90 && now>=state.last_launch && now-state.last_launch>=UINT64_C(500000000))
            launch_browser_worker(state);
    }
    async_receive();
    if(browser_resources && browser_resources->publication.handle){auto &state=*browser_resources;
        if(!storage_task(state.active)&&!task_tab(state.active)){async_release_shared();}
        else if(state.publication.step(state.publication_driver)){
#if HUSARZ_PERF
            if(auto *tab=task_tab(state.active))if(tab->dokument)tab->dokument->perf.duration[unsigned(Husarz::Stage::Publication)]+=state.publication_driver.cpu;
#endif
            async_release_shared();
        }
    }
    async_dispatch();
#endif
}
bool WidokStrony::czy_wymaga_animacji() const {
    for(int i=0;i<liczba_zakladek;++i)if(zakladki[i].javascript&&!zakladki[i].javascript->cancelled)return true;
    return paint_pending() || browser_has_work() || (browser_resources && (browser_resources->busy || browser_resources->launching || browser_resources->queue.count));
}
void WidokStrony::krok_animacji(uint64_t now) { Husarz::native_work_clock.sample(now,Husarz::work_cycles());async_tick();browser_script_step(now);if(!browser_resources || !browser_resources->publication.handle)browser_step_work();advance_paint();if(g_husarz.btn_zatrzymaj)g_husarz.btn_zatrzymaj->ustaw_wlaczona(browser_active_busy());if(g_husarz.app)g_husarz.app->ustaw_odstep_animacji((browser_has_work() || paint_pending() || (browser_resources && browser_resources->publication.handle))?1:8); }
