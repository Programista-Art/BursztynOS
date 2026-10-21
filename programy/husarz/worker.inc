// Included inside the native browser translation unit. The worker build retains
// only this entry point and the existing bounded HTTP/image helpers.
[[maybe_unused]] bool worker_send(uint64_t endpoint,const Husarz::WorkerMessage &message,uint64_t shared=0) {
    BwsIpcWyslij descriptor{BWS_NOWOCZESNE_ABI_WERSJA,shared?BWS_IPC_ZAWIERA_SHM:0U,shared,
        reinterpret_cast<uint64_t>(&message),sizeof(message),0};
    return ipc_send(endpoint,&descriptor);
}
[[maybe_unused]] bool decimal64(const char *&text,uint64_t &value) {
    value=0; unsigned count=0;
    while (*text>='0' && *text<='9') {
        unsigned digit=unsigned(*text++-'0');
        if (++count>20 || value>(UINT64_MAX-digit)/10) return false;
        value=value*10+digit;
    }
    return count>0;
}
[[maybe_unused]] void append_decimal(char *out,size_t &position,uint64_t value) {
    char digits[24]; unsigned n=0;
    do { digits[n++]=char('0'+value%10); value/=10; } while (value);
    while (n) out[position++]=digits[--n];
    out[position]=0;
}
[[maybe_unused,noreturn]] void browser_worker_main() {
    char argument[512]{}; uint64_t parent=0,nonce=0;
    const char *text=argument;
    bool valid=pobierz_argument_startowy(argument,sizeof(argument)) && prefiks_ci(argument,"/husarz-worker/");
    if (valid) { text=argument+15; valid=decimal64(text,parent) && *text++=='/' && decimal64(text,nonce) && !*text; }
    if (!valid || !parent || !nonce) bws_zakoncz_proces();
    uint64_t endpoint=ipc_create(); if (!endpoint) bws_zakoncz_proces();
    Husarz::WorkerMessage hello{}; hello.type=1; hello.endpoint=endpoint; hello.nonce=nonce;
    if (!worker_send(parent,hello)) bws_zakoncz_proces();
    uint64_t last=gui_czas_monotoniczny_ns();
    for (;;) {
        BwsIpcOdbierz incoming{};
        if (!ipc_receive(endpoint,&incoming)) {
            bws_zdarzenie event{};
            gui_czekaj_na_zdarzenie_z_timeoutem(&event,250);
            uint64_t now=gui_czas_monotoniczny_ns();
            if (now && last && now-last>UINT64_C(120000000000)) break;
            continue;
        }
        if (incoming.rozmiar!=sizeof(Husarz::WorkerMessage)) continue;
        Husarz::WorkerMessage message{}; memcpy(&message,incoming.dane,sizeof(message));
        if (message.magic!=Husarz::AsyncMagic || message.nonce!=nonce) { if(incoming.shm_handle)shm_close(incoming.shm_handle); continue; }
        if (message.type==4) break;
        if (message.type!=2 || !(incoming.flagi&BWS_IPC_ZAWIERA_SHM) || !incoming.shm_handle) continue;
        auto *payload=static_cast<Husarz::WorkerPayload*>(shm_map(incoming.shm_handle,BWS_SHM_READ|BWS_SHM_WRITE));
        if (!payload) { shm_close(incoming.shm_handle); continue; }
        payload->success=payload->size=payload->width=payload->height=0;
        if (payload->magic==Husarz::AsyncMagic && payload->request==message.request &&
            Husarz::length(payload->url,sizeof(payload->url))<sizeof(payload->url)) {
            if (payload->kind==uint32_t(Husarz::RequestKind::Html)) {
                auto *parsed=new ParsedUrl{};
                uint8_t ip[4]{};
                if (parsed && parsuj_url(payload->url,parsed) && (adres_ipv4_literal(parsed->domena,ip)||bws_siec_dns(parsed->domena,ip))) {
                    uint32_t bytes=bws_siec_pobierz_bajty(parsed->https,ip,parsed->domena,parsed->sciezka,siec_bufor,sizeof(siec_bufor));
                    if (bytes && bytes<=sizeof(payload->raw)) {
                        for (uint32_t i=0;i<bytes;++i) payload->raw[i]=uint8_t(siec_bufor[i]);
                        payload->size=bytes; payload->success=1; payload->trusted=bws_tls_certyfikat_zaufany();
                    }
                }
                delete parsed;
            } else if (payload->kind==uint32_t(Husarz::RequestKind::Css) || payload->kind==uint32_t(Husarz::RequestKind::Image)) {
                auto type=payload->kind==uint32_t(Husarz::RequestKind::Css)?Husarz::ResourceType::Css:Husarz::ResourceType::Image;
                auto *entry=fetch_browser_resource(payload->url,type);
                if (entry && entry->size<=sizeof(payload->raw)) {
                    for (size_t i=0;i<entry->size;++i) payload->raw[i]=entry->raw[i];
                    kopiuj_limit(payload->content_type,sizeof(payload->content_type),entry->content_type);
                    kopiuj_limit(payload->etag,sizeof(payload->etag),entry->etag);
                    kopiuj_limit(payload->modified,sizeof(payload->modified),entry->modified);
                    payload->size=uint32_t(entry->size); payload->success=1; payload->no_cache=entry->transient; payload->cache_seconds=entry->cache_seconds;
                    if (type==Husarz::ResourceType::Image) {
                        if (decode_browser_image(*resource_cache,*entry) && uint64_t(entry->width)*entry->height<=Husarz::AsyncImagePixels) {
                            payload->width=entry->width; payload->height=entry->height;
                            for (size_t i=0;i<size_t(entry->width)*entry->height;++i) payload->pixels[i]=entry->pixels[i];
                        } else payload->success=0;
                    }
                }
                delete resource_cache; resource_cache=nullptr;
            }
        }
        message.type=3;
        bool sent=worker_send(parent,message);
        shm_unmap(payload); shm_close(incoming.shm_handle);
        if (!sent) break;
        last=gui_czas_monotoniczny_ns();
    }
    ipc_close(endpoint); bws_zakoncz_proces();
}
