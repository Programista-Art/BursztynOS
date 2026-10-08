#pragma once
static void explorer_launcher_cases() {
    files={{"/",true},{"/uzytkownicy",true},{DesktopEntries::Directory,true},
        {std::string(DesktopEntries::Directory)+"/normal.txt",false},
        {std::string(DesktopEntries::Directory)+"/Notatnik",false}};
    for(auto& launcher:DesktopEntries::Launchers)files[launcher.path]=false;
    AktowkaModel model;CHECK(model.initialize());CHECK(model.navigate(DesktopEntries::Directory)!=Change::ERROR);
    CHECK(model.count()==DesktopEntries::Count+3);auto before=files;
    int launchers=0;for(int i=0;i<model.count();++i)launchers+=model.entry(i)->type==EntryType::LAUNCHER;
    CHECK(launchers==DesktopEntries::Count);
    for(int id=0;id<DesktopEntries::Count;++id) {
        int index=-1;for(int i=0;i<model.count();++i)if(model.entry(i)->type==EntryType::LAUNCHER && model.entry(i)->launcher==id)index=i;
        CHECK(index>=0);auto* entry=model.entry(index);CHECK(!entry->directory);
        CHECK(!strcmp(entry->name,DesktopEntries::Launchers[id].name));CHECK(!strcmp(entry_type_name(entry->type),"Skrót aplikacji"));
        bool sc=false;CHECK(model.select_visible(model.visible_position(index),5,&sc));
        CHECK(model.details().valid);CHECK(!strcmp(model.details().full_path,DesktopEntries::Launchers[id].path));
        CHECK(!entry->size_available);CHECK(!model.details().size_available);
        int count=launches;CHECK(model.activate()==Activation::STATUS);CHECK(launches==count+1);CHECK(launched_app==DesktopEntries::Launchers[id].path);
        char path[512];CHECK(!model.selected_path(path,sizeof(path)));CHECK(!model.entry_path(index,path,sizeof(path)));
        CHECK(!model.rename_selected("renamed"));CHECK(!model.delete_selected());CHECK(!model.move_selected("/uzytkownicy"));
        CHECK(!model.put_selected_on_clipboard(BWS_SCHOWEK_COPY));CHECK(!model.put_selected_on_clipboard(BWS_SCHOWEK_CUT));CHECK(files==before);
        AktowkaWindow window;AktowkaView view(model,window);
        CHECK(view.open_context(100,100,false));
        CHECK(!view.open_modal(EditMode::CONFIRM_DELETE));CHECK(!view.open_modal(EditMode::RENAME));CHECK(!view.open_modal(EditMode::MOVE));
        view.close_context();
        test_running_title=DesktopEntries::Launchers[id].title;test_activated_window=0;count=launches;
        CHECK(model.activate()==Activation::STATUS);CHECK(launches==count);CHECK(test_activated_window==42);test_running_title=nullptr;
        // Listing updates preserve identity, including a same-name real file.
        files[std::string(DesktopEntries::Directory)+"/later.txt"]=false;
        model.refresh();CHECK(model.entry(model.selected())->type==EntryType::LAUNCHER);
        CHECK(model.entry(model.selected())->launcher==id);files.erase(std::string(DesktopEntries::Directory)+"/later.txt");model.refresh();
    }
    CHECK(files==before);CHECK(model.navigate("/uzytkownicy")!=Change::ERROR);
    for(int i=0;i<model.count();++i)CHECK(model.entry(i)->type!=EntryType::LAUNCHER);
    // More files than the WM's model or visual capacity remain accessible here.
    for(int i=0;i<150;++i){char name[32];snprintf(name,sizeof(name),"overflow-%03d",i);files[std::string(DesktopEntries::Directory)+"/"+name]=false;}
    CHECK(model.navigate(DesktopEntries::Directory)!=Change::ERROR);
    CHECK(model.count()==150+DesktopEntries::Count+3);
    for(int i=0;i<150;++i){char name[32];snprintf(name,sizeof(name),"overflow-%03d",i);select_name(model,name);CHECK(model.entry(model.selected())->type!=EntryType::LAUNCHER);}
    CHECK(model.create_folder("overflow-folder"));CHECK(files.count(std::string(DesktopEntries::Directory)+"/overflow-folder"));
}
