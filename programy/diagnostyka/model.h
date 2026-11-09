#pragma once
#include "diagnostyka_systemu_format.h"
struct DiagAppModel {
 DiagSummary summary{};DiagDevice devices[DIAG_MAX_DEVICES]{};
 DiagLog logs[DIAG_MAX_LOG]{};uint32_t order[DIAG_MAX_DEVICES]{},drivers[DIAG_MAX_DEVICES]{},visible_logs[DIAG_MAX_LOG]{};
 uint32_t driver_count{},visible_count{};unsigned filter{};bool valid{};
 void rebuild(){
  driver_count=0;for(uint32_t i=0;i<summary.devices;++i){order[i]=i;if(devices[i].driver[0])drivers[driver_count++]=i;}
  // Stable preorder: bounded parent walks, preserving independent root sources.
  uint32_t placed=0;bool used[DIAG_MAX_DEVICES]{};
  for(uint32_t pass=0;pass<summary.devices&&placed<summary.devices;++pass)
   for(uint32_t i=0;i<summary.devices;++i)if(!used[i]){
    const uint32_t parent=devices[i].parent;
    if(!parent||(parent<=summary.devices&&used[parent-1])){order[placed++]=i;used[i]=true;}}
  for(uint32_t i=0;i<summary.devices;++i)if(!used[i])order[placed++]=i;
  visible_count=0;for(uint32_t i=0;i<summary.logs;++i)if(diag_log_matches(logs[i],filter))visible_logs[visible_count++]=i;
 }
 unsigned depth(uint32_t index)const{
  unsigned n=0;uint32_t p=devices[index].parent;
  while(p&&p<=summary.devices&&n<8){++n;p=devices[p-1].parent;}return n;
 }
 bool report(char* buffer,size_t capacity,size_t* length)const{
  if(!valid||!length)return false;
  DiagText t(buffer,capacity);diag_report_summary(t,summary);
  t.add("\nUrządzenia i sterowniki\n");
  for(uint32_t i=0;i<summary.devices;++i)diag_report_device(t,devices[order[i]]);
  t.add("\nLog uruchamiania\n");
  for(uint32_t i=0;i<summary.logs;++i){const auto& l=logs[i];t.scalar("#",l.sequence);t.add(" [");
   t.add(diag_category_text(l.category));t.add("] ");t.add(l.text);
   if(l.code){t.add(" ");t.add(diag_code_text(l.code));}if(l.flags&DIAG_FLAG_TRUNCATED)t.add(" [wpis skrócony]");t.add("\n");}
  *length=t.used;return !t.truncated;
 }
};
