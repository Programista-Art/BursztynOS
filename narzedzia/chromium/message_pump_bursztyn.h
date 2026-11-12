// Bursztyn OS - message pump dla Chromium.
//
// Chromium zaklada pompe komunikatow systemowych (epoll/kqueue/io_uring).
// U nas jedynym zrodlem zdarzen jest BWS 38 (`czekaj_na_zdarzenie_z_timeoutem`),
// wiec pompa jest trywialna: czekaj, dostarcz, powtorz. To jest tez miejsce,
// w ktorym mozna pozniej dodac epoll-like API bez zmiany warstwy wyzej.

#ifndef BASE_MESSAGE_LOOP_MESSAGE_PUMP_BURSZTYN_H_
#define BASE_MESSAGE_LOOP_MESSAGE_PUMP_BURSZTYN_H_

#include "base/message_loop/message_pump.h"
#include "base/time/time.h"

namespace base {

class MessagePumpBursztyn : public MessagePump {
 public:
  MessagePumpBursztyn();
  ~MessagePumpBursztyn() override;

  // MessagePump:
  void Run(Delegate* delegate) override;
  void Quit() override;
  void ScheduleWork() override;
  void ScheduleDelayedWork(
      const Delegate::NextWorkInfo& next_work_info) override;

 private:
  bool quit_ = false;
};

}  // namespace base

#endif  // BASE_MESSAGE_LOOP_MESSAGE_PUMP_BURSZTYN_H_
