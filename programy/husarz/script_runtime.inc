// Native Ring 3 browser integration. No scripting capability is added to BWS.
void queue_document_scripts(Zakladka &z){
    if(!z.dokument||!z.dokument->script_count||z.javascript)return;
    z.javascript=new Husarz::Js::BrowserRuntime;if(!z.javascript)return;
    z.javascript->bind(*z.dokument,z.identity,z.navigation,z.url);
    auto origin=Husarz::Origin::parse(z.url);z.javascript->local_storage=Husarz::browser_storage.get(origin);z.javascript->session_storage=Husarz::browser_storage.get(origin,z.identity);
    if(!browser_resources)browser_resources=new BrowserResources;
    struct Scratch{ParsedUrl base{};Husarz::ResourceTask task{};};auto *scratch=new Scratch;
    if(!scratch)return;
    bool base=document_url_base(z.url,z.dokument,scratch->base);
    for(unsigned i=0;i<z.dokument->script_count;++i){auto &script=z.dokument->scripts[i];const char *src=z.dokument->str(script.url);if(!*src)continue;
        bool svg=false;for(unsigned p=script.node;p;p=z.dokument->nodes[p].parent)if(z.dokument->tag(uint16_t(p),"svg")){svg=true;break;}if(svg){z.javascript->supply(i,nullptr);continue;}
        auto &task=scratch->task;task={};task.tab=z.identity;task.navigation=z.navigation;task.document=z.dokument->generation;task.kind=Husarz::RequestKind::Script;task.script=uint16_t(i);task.node=script.node;task.node_identity=z.dokument->nodes[script.node].identity;
        if(!browser_resources||!base||!ustaw_url_z_location(task.url,sizeof(task.url),src,scratch->base)||!browser_resources->queue.push(task))z.javascript->supply(i,nullptr);
    }
    delete scratch;
}
bool browser_script_event(Husarz::Document &d,uint16_t node,const char *type,unsigned action,uint16_t control,unsigned key,bool ctrl,bool shift,bool alt,int x,int y){
    if(browser_script_bypass)return false;
    for(int i=0;i<liczba_zakladek;++i){auto &z=zakladki[i];if(z.dokument!=&d||!z.javascript||!z.javascript->owns(z.identity,z.navigation,d.generation))continue;
        auto &runtime=*z.javascript;
        if(node<d.node_count){unsigned identity=d.nodes[node].identity;
            if(Husarz::equal(type,"input")){if(auto *c=Husarz::control(d,node))if(Husarz::selectable(d,*c))for(auto &changed:runtime.changed_nodes)if(changed==identity||!changed){changed=uint16_t(identity);break;}}
            if(Husarz::equal(type,"blur"))for(auto &changed:runtime.changed_nodes)if(changed==identity){runtime.queue(node,"change");changed=0;}
        }
        if(!action){bool listened=false;for(auto &listener:runtime.listeners)if(listener.active&&runtime.is(listener.type,type)){listened=true;break;}if(!listened)return false;}
        // Notifications may drop at the cap; cancellable native defaults stay consumed.
        bool queued=z.javascript->queue(node,type,action,control,key,ctrl,shift,alt,x,y);
        return queued||action!=0; // Fail closed for cancellable defaults at queue exhaustion.
    }return false;
}
void WidokStrony::script_default_click(uint16_t id,int x,int y,bool shift){
    if(!liczba_zakladek){return;}auto &z=zakladki[aktywna_zakladka];if(!z.dokument){return;}auto &d=*z.dokument;if(!id||id>=d.node_count||d.nodes[id].detached){return;}
    for(uint16_t p=id;p;p=d.nodes[p].parent)if(Husarz::is_control(d,p)||d.tag(p,"label")){id=p;break;}
    if(d.tag(id,"label")){const char *target=d.attr(id,"for");for(unsigned i=0;i<d.control_count;++i){uint16_t node=d.controls[i].node;if((*target&&Husarz::equal(d.attr(node,"id"),target))||(!*target&&d.nodes[node].parent==id)){id=node;break;}}}
    uint16_t old=d.focused_control;
    if(auto *c=Husarz::control(d,id)){
        if(d.has_attr(id,"disabled")){return;}d.focused_control=id;
        if(old!=id){if(old)browser_script_event(d,old,"blur");browser_script_event(d,id,"focus");}
        if(Husarz::selectable(d,*c)){Husarz::control_pointer(d,*c,browser_metrics,x,y,shift);selection_node_=id;selection_generation_=d.generation;selection_revision_=d.revision;selection_tab_=uint32_t(aktywna_zakladka);}
        else c->cursor=c->anchor=c->length;
        if(Husarz::input_type(d,id,"checkbox")||Husarz::input_type(d,id,"radio")){bool previous=c->checked;Husarz::form_toggle(d,*c);if(c->checked!=previous){browser_script_event(d,id,"input");browser_script_event(d,id,"change");}}
        else if(d.tag(id,"select"))d.open_select=id;
        else if(Husarz::input_type(d,id,"submit")||(d.tag(id,"button")&&!Husarz::input_type(d,id,"button")))submit_browser_form(d,id);
        przygotuj();return;
    }
    d.focused_control=0;if(old)browser_script_event(d,old,"blur");
    for(uint16_t p=id;p;p=d.nodes[p].parent){if(!d.tag(p,"a")||!d.has_attr(p,"href"))continue;
        struct Scratch{ParsedUrl base{};char url[URL_POJEMNOSC]{};};auto *scratch=new Scratch;if(!scratch)return;
        bool ready=document_url_base(z.url,&d,scratch->base)&&ustaw_url_z_location(scratch->url,sizeof(scratch->url),d.attr(p,"href"),scratch->base);
        if(ready){g_husarz.przejdz_do_url(scratch->url);}delete scratch;break;
    }
}
void browser_storage_tick(){
    if(!browser_resources)return;
    for(unsigned i=0;i<Husarz::StorageOrigins;++i){auto *s=Husarz::browser_storage.local[i];if(!s||s->failed||s->loading||s->saving)continue;
        if(s->loaded&&s->revision==s->saved)continue;
        auto *task=new Husarz::ResourceTask;if(!task)return;task->kind=s->loaded?Husarz::RequestKind::StorageWrite:Husarz::RequestKind::StorageRead;task->script=uint16_t(i);Husarz::origin_key(s->origin,task->url);
        if(browser_resources->queue.push(*task)){if(s->loaded)s->saving=true;else s->loading=true;}delete task;return;
    }
}
void browser_script_step(uint64_t now){
    browser_storage_tick();static unsigned cursor{};if(!liczba_zakladek)return;
    // One JS slice across the browser per animation turn, with tab round robin.
    for(int n=0;n<liczba_zakladek;++n){unsigned i=(cursor+unsigned(n))%unsigned(liczba_zakladek);auto &z=zakladki[i];auto *runtime=z.javascript;
        if(!runtime||!z.dokument||!runtime->owns(z.identity,z.navigation,z.dokument->generation))continue;
        if(document_job&&document_job->identity.tab==z.identity)continue;
        if(!runtime->task.handle&&runtime->publication_pending){
            if(z.work_pending)continue;
            if(int(i)==aktywna_zakladka&&g_husarz.widok_strony&&g_husarz.widok_strony->paint_pending())continue;
            runtime->publication_pending=false;
        }
        auto before=z.dokument->revision,paint=z.dokument->paint_revision;
        for(unsigned f=0;f<runtime->fetch_count;++f){auto &fetch=runtime->fetches[f];if(!fetch.active||fetch.queued||fetch.ready)continue;
            struct FetchScratch{ParsedUrl base{};Husarz::ResourceTask task{};};auto *scratch=new FetchScratch;
            bool valid=scratch&&document_url_base(z.url,z.dokument,scratch->base)&&ustaw_url_z_location(scratch->task.url,sizeof(scratch->task.url),fetch.url,scratch->base)&&Husarz::Origin::parse(z.url).same(Husarz::Origin::parse(scratch->task.url));
            if(valid&&browser_resources){auto &request=scratch->task;request.tab=z.identity;request.navigation=z.navigation;request.document=z.dokument->generation;request.kind=Husarz::RequestKind::Fetch;request.script=uint16_t(f);valid=browser_resources->queue.push(request);if(valid)kopiuj_limit(fetch.url,sizeof(fetch.url),request.url);}else valid=false;
            fetch.queued=true;if(!valid)runtime->supply_fetch(f,nullptr);delete scratch;
        }
        runtime->tick(now);
#if HUSARZ_DEBUG_RESOURCES
        if(runtime->event_dispatches!=runtime->reported_dispatches && runtime->debug_notes<128){++runtime->debug_notes;runtime->reported_dispatches=runtime->event_dispatches;char message[160];size_t n=0;const char *prefix="[HUSARZ JS EVENT] action=";while(*prefix)message[n++]=*prefix++;append_decimal(message,n,runtime->event.action);prefix=" target=";while(*prefix)message[n++]=*prefix++;append_decimal(message,n,runtime->event.node);prefix=" type=";while(*prefix)message[n++]=*prefix++;const char *type=runtime->text(runtime->event.type);while(*type && n<145)message[n++]=*type++;message[n++]='\n';message[n]=0;wypisz(message);}
        if(runtime->errors!=runtime->reported_errors&&runtime->debug_notes<128){++runtime->debug_notes;
            runtime->reported_errors=runtime->errors;char message[160];size_t n=0;
            auto word=[&](const char *s){while(*s)message[n++]=*s++;};
            word("[HUSARZ JS] script=");append_decimal(message,n,runtime->last_error_source);
            const char *types[]={"None","SyntaxError","TypeError","ReferenceError","RangeError","OutOfMemory","InstructionBudget","Cancelled"};
            word(" error=");word(types[unsigned(runtime->last_error)]);
            word(" line=");append_decimal(message,n,runtime->last_error_line);
            word(" column=");append_decimal(message,n,runtime->last_error_column);
            message[n++]='\n';message[n]=0;wypisz(message);
        }
#endif
        cursor=(i+1)%unsigned(liczba_zakladek);
        if(z.dokument->revision!=before)browser_request_work(z);
        if(z.dokument->paint_revision!=paint){runtime->publication_pending=true;async_refresh(z);}
        if(runtime->default_action){
#if HUSARZ_DEBUG_RESOURCES
            if(runtime->debug_notes<128){++runtime->debug_notes;char message[100]="[HUSARZ JS DEFAULT] action=";size_t n=Husarz::length(message,sizeof(message));append_decimal(message,n,runtime->default_action);const char *p=" target=";while(*p)message[n++]=*p++;append_decimal(message,n,runtime->default_node);message[n++]='\n';message[n]=0;wypisz(message);}
#endif
            unsigned action=runtime->default_action;runtime->default_action=0;unsigned target=runtime->resolve(runtime->default_node),control=runtime->resolve(runtime->default_control);auto event=runtime->default_event;
            // Defaults retain their owning tab; an inactive tab cannot hijack UI.
            if(int(i)==aktywna_zakladka && target){
                if(action==1)g_husarz.widok_strony->script_default_click(uint16_t(target),event.x,event.y,event.shift);
                else if(action==2){browser_script_bypass=true;submit_browser_form(*z.dokument,uint16_t(control?control:target));browser_script_bypass=false;}
                else if(action==4){ZdarzenieKlawiatury key;key.kod_klawisza=event.native_key;key.kod_skanu=event.native_key|KLAWISZ_FIZYCZNY_GUI;key.modyfikatory=(event.ctrl?uint32_t(BWS_MOD_CTRL):0u)|(event.shift?uint32_t(BWS_MOD_SHIFT):0u)|(event.alt?uint32_t(BWS_MOD_ALT):0u);browser_key_bypass=true;g_husarz.widok_strony->obsluz_wcisniecie_klawisza(key);browser_key_bypass=false;}
                else if(action==5&&z.dokument->focused_control==target){bool edited=false;if(Husarz::form_insert(*z.dokument,event.key,&edited)){if(auto *c=Husarz::control(*z.dokument,uint16_t(target)))Husarz::control_keep_caret(*z.dokument,*c,browser_metrics);if(edited)browser_script_event(*z.dokument,uint16_t(target),"input");g_husarz.widok_strony->przygotuj();}}
            }
            return; // A default navigation may have destroyed this runtime.
        }
        if(runtime->navigation_request[0]){
            struct Scratch{ParsedUrl base{};char url[URL_POJEMNOSC]{};};auto *scratch=new Scratch;
            if(scratch){bool valid=document_url_base(z.url,z.dokument,scratch->base)&&ustaw_url_z_location(scratch->url,sizeof(scratch->url),runtime->navigation_request,scratch->base);runtime->navigation_request[0]=0;
                if(valid&&int(i)==aktywna_zakladka){g_husarz.przejdz_do_url(scratch->url);}delete scratch;}
        }
        return;
    }
}

// Graceful close drains bounded browser-owned persistence without running JS.
// The closed window need not stay responsive; BWS38 yields to all other apps.
[[maybe_unused]] void browser_flush_storage(){
#ifndef HUSARZ_HOST_TEST
    for(int i=0;i<liczba_zakladek;++i)cancel_async_tab(zakladki[i]);
    uint64_t start=gui_czas_monotoniczny_ns();
    for(unsigned turn=0;turn<512;++turn){
        bool pending=false;for(auto *s:Husarz::browser_storage.local)if(s&&!s->failed&&s->loaded&&(s->saving||s->revision!=s->saved))pending=true;
        if(!pending){break;}browser_storage_tick();async_tick();
        uint64_t now=gui_czas_monotoniczny_ns();if(now>=start&&now-start>=2000000000ULL)break;
        bws_zdarzenie event{};gui_czekaj_na_zdarzenie_z_timeoutem(&event,1);
    }
#endif
}
