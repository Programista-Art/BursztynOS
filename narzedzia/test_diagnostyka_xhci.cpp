#define BURSZTYN_DIAG_STORE_TEST 1
#include "narzedzia/test_xhci_multi.cpp"
int main(){
 diag_test_reset();independent_controllers();
 diag_test_reset();composite_device();
 diag_test_reset();interface_fairness();
 diag_test_reset();fixture();clock_step=1000000;
 cnr_stuck=true;put(registers,0x84,STS_CNR|STS_HCH);
 CHECK(!xhci_inicjalizuj_wszystkie());
 DiagDevice d{};CHECK(diag_get_device(0,&d));
 CHECK(d.category==DIAG_XHCI&&d.status==DIAG_TIMEOUT);
 CHECK(d.code==DIAG_CODE_XHCI_INIT&&d.pci_valid);
 CHECK(d.hardware_status&STS_CNR);clean();
 printf("Production xHCI diagnostics integration: %u checks PASS\n",checks);
}
