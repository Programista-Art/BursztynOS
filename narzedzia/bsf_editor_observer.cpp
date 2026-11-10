#include <cstdio>
#include <cstddef>
#define BSF_NATIVE 1
#define private public
#include "../bursztyn_ui/bursztyn_ui.h"
#undef private
#define _start notepad_observer_entry
#define __dso_handle notepad_observer_dso
#include "../notatnik.cpp"
#undef _start
#undef __dso_handle
int main(){
 printf("{\"memo\":%zu,\"cursor\":%zu,\"anchor\":%zu,\"length\":%zu,\"buffer\":%zu,\"boundaries\":%zu}\n",offsetof(NotatnikApp,memo),offsetof(BursztynUI::PoleWielowierszowe,kursor_),offsetof(BursztynUI::PoleWielowierszowe,kotwica_),offsetof(BursztynUI::PoleWielowierszowe,dlugosc_),offsetof(BursztynUI::PoleWielowierszowe,bufor_),offsetof(BursztynUI::PoleWielowierszowe,granice_));
}
