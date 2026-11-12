// Bursztyn OS - message pump dla Chromium: implementacja.

#include "base/message_loop/message_pump_bursztyn.h"

#include <algorithm>

#include "base/logging.h"
#include "third_party/bursztyn/posix_bws.h"

namespace base {

namespace {

// Maksymalny czas blokowania w jednym wywolaniu BWS 38. Krotki timeout
// gwarantuje, ze petla sprawdzi zadania odroczone i Quit() nawet gdy nie ma
// zadnego zdarzenia wejsciowego (bez tego aplikacja nie zareaguje na
// zamkniecie okna z paska zadan).
constexpr int64_t kMaksBlokadaMs = 8;

}  // namespace

MessagePumpBursztyn::MessagePumpBursztyn() = default;
MessagePumpBursztyn::~MessagePumpBursztyn() = default;

void MessagePumpBursztyn::Run(Delegate* delegate) {
  for (;;) {
    if (quit_) return;

    // 1. Wykonaj zadania, ktore sa juz gotowe.
    Delegate::NextWorkInfo info = delegate->DoWork();
    if (quit_) return;

    // 2. Wykonaj zadania odroczone, jesli termin uplynal.
    if (info.delayed_run_time.is_null() ||
        info.delayed_run_time <= TimeTicks::Now()) {
      delegate->DoDelayedWork(&info.delayed_run_time);
      if (quit_) return;
    }

    // 3. Czekaj na zdarzenie wejsciowe albo na najblizszy termin.
    int64_t timeout_ms = kMaksBlokadaMs;
    if (!info.delayed_run_time.is_null()) {
      const TimeDelta do_terminu = info.delayed_run_time - TimeTicks::Now();
      const int64_t ms = do_terminu.InMilliseconds();
      timeout_ms = std::clamp<int64_t>(ms, 0, kMaksBlokadaMs);
    }

    // BWS 38 blokuje do timeoutu i zwraca false przy braku zdarzenia.
    // Timeout jest w tickach schedulera; przy 60..240 Hz jeden tick to
    // 4..16 ms, wiec przekazujemy liczbe tickow wprost.
    BwsZdarzenie zdarzenie{};
    bws_czekaj_na_zdarzenie_z_timeoutem(&zdarzenie,
                                        static_cast<uint64_t>(timeout_ms));
    if (quit_) return;
  }
}

void MessagePumpBursztyn::Quit() { quit_ = true; }

void MessagePumpBursztyn::ScheduleWork() {
  // Pompa nie spi na osobnym watku (model jednowatkowy), wiec nie ma czego
  // budzic. Flaga `quit_` jest sprawdzana w kazdej iteracji.
}

void MessagePumpBursztyn::ScheduleDelayedWork(
    const Delegate::NextWorkInfo& next_work_info) {
  // Termin jest czytany z NextWorkInfo w kazdej iteracji Run(), wiec nie
  // trzeba go nigdzie zapisywac.
  (void)next_work_info;
}

}  // namespace base
