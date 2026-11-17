#pragma once
// Jedno zrodlo prawdy dla katalogow uzytkownika (jadro, dialogi plikow, aplikacje).
// /uzytkownicy i /uzytkownicy/Pulpit sa katalogami systemowymi (poziom jadra);
// katalog domowy i Dokumenty maja poziom PZB_UZYTKOWNIK (zapewnia je jadro przy starcie).
namespace KatalogiUzytkownika {
inline constexpr const char* Uzytkownicy = "/uzytkownicy";
inline constexpr const char* Domowy = "/uzytkownicy/domyslny";
inline constexpr const char* Dokumenty = "/uzytkownicy/domyslny/Dokumenty";
inline constexpr const char* Pulpit = "/uzytkownicy/Pulpit";
}
