#include <stdint.h>
#include <stddef.h>

// 1. Definicja nagłówka naszego lekkiego formatu .btp (Bursztynowa TaPeta)
// Używamy "packed", aby kompilator nie dodawał pustych bajtów (paddingu) do struktury.
#pragma pack(push, 1)
typedef struct {
uint8_t  magic[4];   // "BTP1" - magiczny numer, pozwala sprawdzić czy to na pewno nasza tapeta
uint32_t width;      // Szerokość w pikselach
uint32_t height;     // Wysokość w pikselach
uint8_t  bpp;        // Bity na piksel (np. 32 dla ARGB)
uint8_t  compressed; // 1 jeśli używamy kompresji RLE, 0 jeśli surowe dane
} btp_header_t;
#pragma pack(pop)

// 2. Bezpieczna funkcja do rysowania tapety (Dekompresja RLE)
// Parametry:
// - file_data: wskaźnik do pliku wczytanego z "Bursztynowego Systemu Plików"
// - file_size: dokładny rozmiar pliku wczytany z dysku (KRYTYCZNE DLA BEZPIECZEŃSTWA!)
// - framebuffer: wskaźnik do pamięci karty graficznej
// - fb_width, fb_height: wymiary ekranu z ustawień VESA/GOP
int load_and_draw_btp(const uint8_t* file_data, size_t file_size, uint32_t* framebuffer, uint32_t fb_width, uint32_t fb_height) {
// [ZASADA BEZPIECZEŃSTWA] 1. Zawsze sprawdzaj, czy plik nie jest mniejszy niż sam nagłówek!
if (file_size < sizeof(btp_header_t)) {
return -1; // Błąd: Plik jest za mały
}

const btp_header_t* header = (const btp_header_t*)file_data;

// [ZASADA BEZPIECZEŃSTWA] 2. Sprawdź Magiczny Numer
if (header->magic[0] != 'B' || header->magic[1] != 'T' ||
    header->magic[2] != 'P' || header->magic[3] != '1') {
    return -2; // Błąd: Nieznany format
}

// [ZASADA BEZPIECZEŃSTWA] 3. Sprawdź, czy tapeta zmieści się na ekranie
if (header->width > fb_width || header->height > fb_height) {
    return -3; // Błąd: Tapeta jest większa niż rozdzielczość ekranu
}

const uint8_t* pixel_data = file_data + sizeof(btp_header_t);
size_t data_size = file_size - sizeof(btp_header_t);

uint32_t pixels_to_draw = header->width * header->height;
uint32_t pixels_drawn = 0;
size_t byte_index = 0;

// Rysowanie z dekompresją RLE
if (header->compressed == 1) {
    while (pixels_drawn < pixels_to_draw && byte_index < data_size) {
        // Czytamy pakiet RLE
        uint8_t count = pixel_data[byte_index++];

        // Jeśli nie ma już miejsca na odczytanie koloru, przerywamy (ochrona przed awarią)
        if (byte_index + 3 >= data_size) break;

        // Odczyt koloru (zakładamy 32 bpp, 4 bajty)
        uint32_t color = *(uint32_t*)(&pixel_data[byte_index]);
        byte_index += 4;

        // Rysujemy piksele zliczając count
        for (uint8_t i = 0; i < count && pixels_drawn < pixels_to_draw; i++) {
            // Obliczamy X i Y na ekranie
            uint32_t x = pixels_drawn % header->width;
            uint32_t y = pixels_drawn / header->width;

            // Zapis prosto do bufora ramki (framebuffer)
            framebuffer[y * fb_width + x] = color;
            pixels_drawn++;
        }
    }
} else {
    // ... (Tu można dodać kod dla niekompresowanych danych wprost memcpy)
}

return 0; // Sukces


}