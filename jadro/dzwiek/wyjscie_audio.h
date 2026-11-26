#pragma once
/*
 * Bursztyn OS - wspolne wyjscie audio (BWS 110, glosnosc Zasobnika, diagnostyka).
 *
 * Publiczne PcmAplikacji:: (jadro/dzwiek/pcm_aplikacji.h) jest zaimplementowane w wyjscie_audio.cpp i kieruje
 * kazde wywolanie do JEDNEGO urzadzenia wybranego przy starcie (polityka: wyjscie_audio_polityka.h):
 * Intel HDA albo Intel ICH AC97. Oba korzystaja z tej samej klasy miksera (jadro/dzwiek/mikser_pcm.h).
 */
#include "wyjscie_audio_polityka.h"
#include "sterowniki/dzwiek/hda.h"

// Jednorazowe ustalenie urzadzenia (kernel.cpp po probie inicjalizacji). Ponowne wywolanie jest odrzucane.
bool wyjscie_audio_ustal(const WyjscieAudio::Dostepnosc& d);
WyjscieAudio::Urzadzenie wyjscie_audio_aktywne();
WyjscieAudio::Dostepnosc wyjscie_audio_dostepnosc();

void wyjscie_audio_glosnosc_info(HdaGlosnoscInfo* out);
uint32_t wyjscie_audio_glosnosc_ustaw(uint32_t procent);
uint32_t wyjscie_audio_wyciszenie_ustaw(bool wyciszony);
