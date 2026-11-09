#include "bursztyn_ui_host_fixture.h"
#include <cassert>
#include <algorithm>
using namespace BursztynUI;

static unsigned checks;
#define CHECK(c) do {++checks;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)

#include "slider_pixel_cases.h"
int main() {
    slider_pixel_cases();
    printf("Slider creation / 100 repeats / 50-60-50 / themes: %u checks PASS\n",checks);
}
