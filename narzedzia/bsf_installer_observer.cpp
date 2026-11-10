#include <cstdio>
#include <cstddef>
#define BSF_NATIVE 1
#define private public
#include "../bursztyn_ui/bursztyn_ui.h"
#undef private
#include "../loader.h"
#include "../instalator_format.h"
#define class struct
#define _start installer_observer_entry
#define __dso_handle installer_observer_dso
#include "../programy/instalator/instalator.cpp"
#undef _start
#undef __dso_handle
#undef class
int main(){printf("{\"page\":%zu,\"selected\":%zu,\"word\":%zu,\"enabled\":%zu,\"source_checked\":%zu}\n",offsetof(Wizard,page),offsetof(Wizard,selected),offsetof(Wizard,word),offsetof(Wizard,buttonenabled),offsetof(Wizard,sourceackchecked));}
