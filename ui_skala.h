#pragma once
#include <stdint.h>
#include <limits.h>
// Jedyny procent i jedyne zaokraglanie metryk UI, bez float/FPU.
constexpr bool ui_skala_poprawna(uint32_t p) { return p>=100 && p<=200 && p%25==0; }
constexpr int ui_skaluj(int baza, uint32_t procent) {
    const int64_t n=int64_t(baza)*procent;
    const int64_t r=n>=0?(n+50)/100:(n-50)/100;
    return r>INT_MAX?INT_MAX:(r<INT_MIN?INT_MIN:int(r));
}
// Odwrotnosc ui_skaluj dla zapisywania logicznego rozmiaru podanego w
// pikselach fizycznych. Stosuje to samo zaokraglenie do najblizszej wartosci.
constexpr int ui_odskaluj(int fizyczne, uint32_t procent) {
    if (procent == 0) return 0;
    const int64_t n = int64_t(fizyczne) * 100;
    const int64_t polowa = procent / 2;
    const int64_t r = n >= 0 ? (n + polowa) / procent : (n - polowa) / procent;
    return r > INT_MAX ? INT_MAX : (r < INT_MIN ? INT_MIN : int(r));
}
// Adapter zachowuje stare wywolania ustaw_skale_dpi(1/2).
// Metryki mnozone przez SkalaUI zawsze uzywaja ui_skaluj.
class SkalaUI {
    uint32_t procent_;
public:
    constexpr SkalaUI(int mnoznik=1):procent_(mnoznik<1?100:(mnoznik>4?400:uint32_t(mnoznik)*100)) {}
    static constexpr SkalaUI procent(uint32_t p) { SkalaUI s; s.procent_=p>=100&&p<=400?p:100; return s; }
    constexpr uint32_t procent() const { return procent_; }
    constexpr operator int() const { return ui_skaluj(1,procent_); }
    friend constexpr int operator*(int a,SkalaUI s) { return ui_skaluj(a,s.procent_); }
    friend constexpr int operator*(SkalaUI s,int a) { return a*s; }
    friend constexpr int64_t operator*(int64_t a,SkalaUI s) { return a>INT_MAX?INT_MAX:(a<INT_MIN?INT_MIN:ui_skaluj(int(a),s.procent_)); }
    friend constexpr uint64_t operator*(uint64_t a,SkalaUI s) { return a>(UINT64_MAX-50)/s.procent_?UINT64_MAX:(a*s.procent_+50)/100; }
    friend constexpr int operator/(int a,SkalaUI s) { return int(int64_t(a)*100/s.procent_); }
};
inline uint32_t ui_skala_procent = 100;
inline SkalaUI ui_skala() { return SkalaUI::procent(ui_skala_procent); }


constexpr uint32_t ui_procent_legacy(int s){return s>=100&&s<=400?uint32_t(s):(s<1?100:(s>4?400:uint32_t(s)*100));}
