// Bursztyn OS - Ozone PlatformEventSource: naglowek.
//
// PlatformEventSource to zrodlo zdarzen wejsciowych dla Chromium. W naszym
// backendzie czyta je z BWS 37/38 (kolejka zdarzen per PID), ktora jest
// zasilana przez sterowniki wejscia (PS/2, virtio-input, USB HID, tablet).

#ifndef UI_OZONE_PLATFORM_BURSZTYN_PLATFORM_EVENT_SOURCE_BURSZTYN_H_
#define UI_OZONE_PLATFORM_BURSZTYN_PLATFORM_EVENT_SOURCE_BURSZTYN_H_

#include <memory>

#include "base/message_loop/message_pump_bursztyn.h"
#include "ui/events/ozone/evdev/device_event_dispatcher_evdev.h"
#include "ui/events/platform/platform_event_source.h"

namespace ui {

class PlatformEventSourceBursztyn : public PlatformEventSource {
 public:
  PlatformEventSourceBursztyn();
  ~PlatformEventSourceBursztyn() override;

  // PlatformEventSource:
  void AddPlatformEventDispatcher(PlatformEventDispatcher* dispatcher) override;
  void RemovePlatformEventDispatcher(PlatformEventDispatcher* dispatcher) override;

  // Uruchamia pompe komunikatow oparta na BWS 38 (czekaj na zdarzenie).
  void StartMessagePump();

 private:
  void OnZdarzenieZPetli(const BwsZdarzenie& zdarzenie);

  std::unique_ptr<base::MessagePumpBursztyn> message_pump_;

  PlatformEventSourceBursztyn(const PlatformEventSourceBursztyn&) = delete;
  PlatformEventSourceBursztyn& operator=(const PlatformEventSourceBursztyn&) = delete;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_BURSZTYN_PLATFORM_EVENT_SOURCE_BURSZTYN_H_
