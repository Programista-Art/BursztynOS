#pragma once
#include "diagnostyka_systemu.h"
// Kernel-only initialization scope. A failed return retains an explicit failure;
// successful preparation can publish INACTIVE until the activation barrier.
struct DiagPciAttempt {
 DiagDevice record;uint64_t key;
 DiagPciAttempt(uint32_t category,const char* name,const char* driver,
     uint8_t bus,uint8_t slot,uint8_t function,uint16_t vendor,uint16_t device)
  :record(diag_device(category,name,driver,DIAG_INIT_FAILED,true,"Inicjalizacja sterownika nie powiodła się; sprawdź log uruchamiania")),
   key((uint32_t(bus)<<16)|(uint32_t(slot)<<8)|function){
  record.parent=diag_pci_parent(bus,slot,function);
  record.transport=DIAG_TRANSPORT_PCI;record.bus=bus;record.pci_device=slot;
  record.function=function;record.pci_valid=1;record.vendor=vendor;record.device=device;
 }
 void ready(uint32_t status=DIAG_OK){record.status=status;const char* msg=status==DIAG_OK?
  "Sterownik gotowy":"Sterownik przygotowany; oczekiwanie na aktywację";
  size_t i=0;for(;msg[i]&&i+1<sizeof(record.message);++i)record.message[i]=msg[i];record.message[i]=0;}
 ~DiagPciAttempt(){diag_publish(key,record);}
};
