#pragma once
// Shared token matching inside the existing bounded Multiboot command-line walk.
inline bool opcja_startowa_obecna(const char* tekst,const char* token) {
    if(!tekst||!token||!*token)return false;
    for(unsigned i=0;tekst[i];) {
        while(tekst[i]==' '||tekst[i]=='\t'||tekst[i]=='\n')++i;
        unsigned j=0;
        while(token[j]&&tekst[i+j]==token[j])++j;
        if(!token[j]&&(!tekst[i+j]||tekst[i+j]==' '||tekst[i+j]=='\t'||tekst[i+j]=='\n'))return true;
        while(tekst[i]&&tekst[i]!=' '&&tekst[i]!='\t'&&tekst[i]!='\n')++i;
    }
    return false;
}
