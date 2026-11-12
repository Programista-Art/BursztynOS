// Bursztyn OS - Ozone PlatformEventSource: implementacja.
//
// Zdarzenia BWS maja postac "typ + x + y + kod", czyli dokladnie to, czego
// potrzebuje ui::Event. Mapowanie jest 1:1 na MouseEvent / KeyEvent.
// Nie ma tu akceleracji ani powtarzania klawiszy - to robi jadro, zeby
// zachowanie bylo identyczne dla aplikacji .bur i dla Chromium.

#include "ui/ozone/platform/bursztyn/platform_event_source_bursztyn.h"

#include <memory>

#include "base/logging.h"
#include "third_party/bursztyn/posix_bws.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes_posix.h"
#include "ui/gfx/geometry/point_f.h"

namespace ui {

namespace {

// Typy zdarzen BWS - musza byc identyczne z jadro/bws/bws_zdarzenia.h.
// Weryfikuje to narzedzia/chromium/weryfikuj_backend.py.
constexpr uint32_t BWS_ZDARZENIE_MYSZ = 1;
constexpr uint32_t BWS_ZDARZENIE_KLAWISZ = 2;
constexpr uint32_t BWS_ZDARZENIE_OKNO = 3;

// Kody przyciskow myszy w BWS (bit 0 = lewy, bit 1 = prawy, bit 2 = srodkowy).
constexpr uint32_t BWS_PRZYCISK_LEWY = 1U << 0;
constexpr uint32_t BWS_PRZYCISK_PRAWY = 1U << 1;
constexpr uint32_t BWS_PRZYCISK_SRODKOWY = 1U << 2;

}  // namespace

PlatformEventSourceBursztyn::PlatformEventSourceBursztyn() = default;
PlatformEventSourceBursztyn::~PlatformEventSourceBursztyn() = default;

void PlatformEventSourceBursztyn::OnZdarzenieZPetli(
    const BwsZdarzenie& zdarzenie) {
  switch (zdarzenie.typ) {
    case BWS_ZDARZENIE_MYSZ: {
      // Ruch i przyciski sa rozrozniane przez pole `kod` (0 = ruch,
      // 1..3 = numer przycisku) i `stan` (0 = puszczony, 1 = wcisniety).
      const gfx::PointF pozycja(static_cast<float>(zdarzenie.x),
                                static_cast<float>(zdarzenie.y));
      std::unique_ptr<Event> event;
      if (zdarzenie.kod == 0) {
        event = std::make_unique<MouseEvent>(ET_MOUSE_MOVED, pozycja, pozycja,
                                             /*time_stamp=*/base::TimeTicks::Now(),
                                             /*flags=*/0,
                                             /*changed_button_flags=*/0);
      } else {
        const EventType typ =
            zdarzenie.stan ? ET_MOUSE_PRESSED : ET_MOUSE_RELEASED;
        const int flagi = static_cast<int>(zdarzenie.przyciski);
        event = std::make_unique<MouseEvent>(typ, pozycja, pozycja,
                                             base::TimeTicks::Now(), flagi, flagi);
      }
      if (event) PlatformEventSource::DispatchEvent(std::move(event));
      break;
    }

    case BWS_ZDARZENIE_KLAWISZ: {
      // `kod` to juz fizyczny kod klawisza (EVDEV code), ktory BWS tlumaczy
      // na VKEY. Mapowanie VKEY -> KeyboardCode zalatwia ui/events.
      const KeyboardCode klawisz =
          static_cast<KeyboardCode>(zdarzenie.kod & 0xFFFFU);
      const EventType typ =
          zdarzenie.stan ? ET_KEY_PRESSED : ET_KEY_RELEASED;
      auto event = std::make_unique<KeyEvent>(typ, klawisz,
                                              /*flags=*/0,
                                              /*dom_code=*/0,
                                              /*dom_key=*/0,
                                              base::TimeTicks::Now());
      PlatformEventSource::DispatchEvent(std::move(event));
      break;
    }

    case BWS_ZDARZENIE_OKNO: {
      // Zmiana geometrii/aktywnosci jest przekazywana do delegata okna
      // przez PlatformWindow, nie przez PlatformEventSource. Nic do roboty.
      break;
    }

    default:
      break;
  }
}

void PlatformEventSourceBursztyn::StartMessagePump() {
  if (!message_pump_)
    message_pump_ = std::make_unique<base::MessagePumpBursztyn>();
  LOG(INFO) << "BWS: pompa zdarzen gotowa (BWS 37/38)";
  message_pump_->Run(base::MessagePump::Delegate::CreateDefaultDelegate());
}

}  // namespace ui
