#!/bin/bash

echo "=== Potężna aktualizacja limitów pamięci w aplikacjach Ring 3 ==="

# Agresywna podmiana limitu tekstu z ignorowaniem starych wartości
sed -i -E 's/[0-9]+,[[:space:]]*[0-9]+,[[:space:]]*0x601000/4096, 61440, 0x601000/g' shell.cpp menedzer_okien.cpp programy/*.cpp

# Agresywna podmiana sekcji danych (szuka 0x605000 i przesuwa na 0x610000, dając aż 128 KB na zmienne!)
sed -i -E 's/[0-9]+,[[:space:]]*[0-9]+,[[:space:]]*0x605000/65536, 131072, 0x610000/g' shell.cpp menedzer_okien.cpp programy/*.cpp

echo "Gotowe! Wszystkie aplikacje (w tym Powłoka) mają twardo zapisane 60 KB kodu i nowy adres danych."