#pragma once

// Funkcja "wyciągająca" 3-cyfrowy kod z początku odpowiedzi serwera (np. z "HTTP/1.1 404 Not Found")
int wyciagnij_kod_http(const char* odpowiedz) {
    if (odpowiedz[0] == 'H' && odpowiedz[1] == 'T' && odpowiedz[2] == 'T' && odpowiedz[3] == 'P') {
        int i = 0;
        // Przesuń się do pierwszej spacji
        while(odpowiedz[i] != ' ' && odpowiedz[i] != '\0') i++; 
        if (odpowiedz[i] == ' ') i++;
        
        // Jeśli zaraz po spacji jest liczba, to jest to nasz kod (np. 301)
        if (odpowiedz[i] >= '0' && odpowiedz[i] <= '9') {
            return (odpowiedz[i] - '0') * 100 + (odpowiedz[i+1] - '0') * 10 + (odpowiedz[i+2] - '0');
        }
    }
    return 0; // Zwraca 0, jeśli nie znaleziono prawidłowego nagłówka HTTP
}

// Funkcja zwracająca przyjazny opis błędu na podstawie Twojej tabeli
const char* pobierz_opis_kodu_http(int kod) {
    switch(kod) {
        // 1xx - informacje
        case 100: return "Continue - Serwer otrzymal poczatek zadania.";
        case 101: return "Switching Protocols - Serwer zgadza sie na zmiane protokolu.";
        
        // 2xx - sukces
        case 200: return "OK - Zadanie wykonane poprawnie.";
        case 201: return "Created - Utworzono nowy zasob.";
        case 204: return "No Content - Brak tresci do wyswietlenia.";
        case 206: return "Partial Content - Zwracana jest tylko czesc pliku.";
        
        // 3xx - przekierowania
        case 301: return "Moved Permanently - Strona zostala trwale przeniesiona pod inny adres.<br>Przegladarka Hussar nie obsluguje jeszcze automatycznych przekierowan SSL.";
        case 302: return "Found - Tymczasowe przekierowanie na inny adres.";
        case 307: return "Temporary Redirect - Tymczasowe przekierowanie.";
        case 308: return "Permanent Redirect - Trwale przekierowanie.";
        
        // 4xx - blad klienta
        case 400: return "Bad Request - Zadanie jest nieprawidlowe lub uszkodzone.";
        case 401: return "Unauthorized - Strona wymaga uwierzytelnienia (logowania).";
        case 403: return "Forbidden - Serwer rozumie zadanie, ale odmawia dostepu.";
        case 404: return "Not Found - Nie znaleziono strony lub zasobu pod tym adresem.";
        case 405: return "Method Not Allowed - Niedozwolona metoda HTTP.";
        case 408: return "Request Timeout - Serwer czekal zbyt dlugo na zadanie.";
        case 429: return "Too Many Requests - Wyslano zbyt wiele zadan w krotkim czasie.";
        
        // 5xx - blad serwera
        case 500: return "Internal Server Error - Krytyczny blad wewnetrzny serwera.";
        case 502: return "Bad Gateway - Blad bramy sieciowej.";
        case 503: return "Service Unavailable - Usluga jest chwilowo niedostepna.";
        case 504: return "Gateway Timeout - Serwer posredniczacy zbyt dlugo czekal na odpowiedz.";
        
        default: {
            if (kod >= 100 && kod < 200) return "Informacja od serwera.";
            if (kod >= 200 && kod < 300) return "Sukces - Odebrano dane.";
            if (kod >= 300 && kod < 400) return "Przekierowanie - Strona jest pod innym adresem.";
            if (kod >= 400 && kod < 500) return "Blad Klienta - Nie mozna wyswietlic tej strony.";
            if (kod >= 500 && kod < 600) return "Blad Serwera - Problem po stronie hosta.";
            return "Nierozpoznany kod odpowiedzi HTTP.";
        }
    }
}

// Funkcja generująca piękną stronę HTML w przypadku błędu
void zloz_strone_bledu(char* bufor, int kod, const char* opis) {
    int p = 0;
    const char* p1 = "<h1>Status HTTP: ";
    while(p1[p]) { bufor[p] = p1[p]; p++; }
    
    // Wypisanie kodu liczbowego (np. 404)
    bufor[p++] = (kod / 100) + '0';
    bufor[p++] = ((kod / 10) % 10) + '0';
    bufor[p++] = (kod % 10) + '0';
    
    const char* p2 = "</h1><br><p>";
    int i = 0; while(p2[i]) { bufor[p++] = p2[i++]; }
    
    i = 0; while(opis[i]) { bufor[p++] = opis[i++]; }
    
    const char* p3 = "</p><br><p>Sprawdz adres URL, upewnij sie, ze nie wymaga polaczenia SSL, lub sprobuj ponownie pozniej.</p>";
    i = 0; while(p3[i]) { bufor[p++] = p3[i++]; }
    
    bufor[p] = '\0';
}