/* Bursztyn OS - Eksplorator Plikow. Ring 3 korzysta tylko z GUI/BWS. */
#include "../../bursztyn_gui.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct NaglowekBur {
    uint8_t magia[4];
    uint64_t punkt_wejscia;
    uint64_t tekst_przesuniecie, tekst_rozmiar, tekst_wirtualny;
    uint64_t dane_przesuniecie, dane_rozmiar, dane_wirtualny;
} __attribute__((packed));

static_assert(sizeof(NaglowekBur) == 60U, "Nieprawidlowy naglowek .bur");
#ifndef AKTOWKA_HOST_TEST
extern "C" __attribute__((noreturn)) void _start();
extern "C" {
__attribute__((section(".naglowek"), used)) NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'}, reinterpret_cast<uint64_t>(&_start),
    UINT64_C(0x1000), UINT64_C(0xC000), UINT64_C(0x601000),
    UINT64_C(0xD000), UINT64_C(0x20000), UINT64_C(0x60D000)};
}
#endif

namespace {

constexpr size_t PATH_CAP = 512;
constexpr size_t NAME_CAP = 56;
constexpr size_t SEARCH_CAP = 128;
constexpr size_t STATUS_CAP = 224;
constexpr size_t SHOW_CAP = 512;
constexpr size_t APP_NAME_CAP = 128;
constexpr size_t ENTRY_POINT_CAP = 256;
constexpr uint32_t LIST_CAP = 64U * 1024U;
constexpr uint32_t MANIFEST_MAX = 16U * 1024U;
constexpr int MAX_ENTRIES = 8192;
constexpr int HISTORY_MAX = 32;
constexpr int QUICK_MAX = 10;
constexpr int REFRESH_TICKS = 3;

/*
 * Jeden cel zawiera pelna, 512-bajtowa sciezke. Cala tablica celow nie moze
 * lezec na 16 KiB stosie procesu BUR: register_drop_targets() jest wywolywane
 * z petli zdarzen, ktora sama ma kilka KiB danych lokalnych. Bufor ma staly
 * rozmiar ABI, nalezy do prywatnej przestrzeni procesu i jest kopiowany przez
 * BWS 49, wiec po powrocie z wywolania jadro nie przechowuje do niego wskaznika.
 */
BwsCelDrop drop_targets_buffer[BWS_DROP_CELE_MAX] = {};

constexpr int DEF_X = 70;
constexpr int DEF_Y = 48;
constexpr int DEF_W = 900;
constexpr int DEF_H = 600;
constexpr int MIN_W = 600;
constexpr int MIN_H = 400;
constexpr int TASKBAR_H = 40;
constexpr int Z_ORDER = 10;
constexpr int TITLE_H = 28;
constexpr int MARGIN = 6;
constexpr int GAP = 4;
constexpr int TOOL_H = 26;
constexpr int TOOLBAR_H = 96;
constexpr int STATUS_H = 25;
constexpr int PANEL_TITLE_H = 27;
constexpr int TABLE_HEAD_H = 27;
constexpr int ROW_H = 24;
constexpr int SCROLL_W = 22;
constexpr int ICON = 13;

constexpr uint32_t BG = 0x00280F00U;
constexpr uint32_t PANEL = 0x00301500U;
constexpr uint32_t PANEL_ALT = 0x00391B02U;
constexpr uint32_t BORDER = 0x00E58A00U;
constexpr uint32_t BORDER_DARK = 0x00794400U;
constexpr uint32_t TEXT = 0x00FFFFFFU;
constexpr uint32_t TEXT2 = 0x00D1D5DBU;
constexpr uint32_t MUTED = 0x009B9B9BU;
constexpr uint32_t DISABLED = 0x00606060U;
constexpr uint32_t SELECT = 0x00603800U;
constexpr uint32_t DROP_TARGET = 0x008A5A00U;
constexpr uint32_t FOLDER = 0x00FFBF00U;
constexpr uint32_t BUR = 0x0038B000U;
constexpr uint32_t CEBULA = 0x00D050A0U;
constexpr uint32_t TXT = 0x004C9BE8U;
constexpr uint32_t FILE_COLOR = 0x00708090U;

struct Rect {
    int x;
    int y;
    int w;
    int h;
};

int clamp_int(int value, int low, int high) {
    if (high < low) high = low;
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

bool hit(int px, int py, const Rect& rect) {
    return rect.w > 0 && rect.h > 0 &&
           px >= rect.x && py >= rect.y &&
           px < rect.x + rect.w && py < rect.y + rect.h;
}

size_t len_limit(const char* text, size_t cap) {
    if (!text) return cap;
    for (size_t i = 0; i < cap; ++i)
        if (text[i] == '\0') return i;
    return cap;
}

void zero_memory(void* pointer, size_t length) {
    if (!pointer) return;
    uint8_t* bytes = static_cast<uint8_t*>(pointer);
    for (size_t i = 0; i < length; ++i) bytes[i] = 0;
}

bool copy_text(char* destination, size_t capacity, const char* source) {
    if (!destination || capacity == 0 || !source) return false;
    const size_t length = len_limit(source, capacity);
    if (length >= capacity) {
        destination[0] = '\0';
        return false;
    }
    for (size_t i = 0; i <= length; ++i) destination[i] = source[i];
    return true;
}

bool equal_text(const char* left, const char* right) {
    if (!left || !right) return false;
    size_t i = 0;
    while (left[i] && right[i]) {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return left[i] == '\0' && right[i] == '\0';
}

uint8_t ascii_lower(uint8_t value) {
    if (value >= 'A' && value <= 'Z') return static_cast<uint8_t>(value + 32U);
    return value;
}

int compare_text_ci(const char* left, const char* right) {
    if (!left || !right) return left ? 1 : (right ? -1 : 0);
    size_t i = 0;
    while (left[i] && right[i]) {
        const uint8_t a = ascii_lower(static_cast<uint8_t>(left[i]));
        const uint8_t b = ascii_lower(static_cast<uint8_t>(right[i]));
        if (a < b) return -1;
        if (a > b) return 1;
        ++i;
    }
    if (left[i]) return 1;
    if (right[i]) return -1;
    return 0;
}

bool contains_text_ci(const char* text, const char* needle) {
    if (!text || !needle) return false;
    const size_t needle_length = len_limit(needle, SEARCH_CAP);
    if (needle_length == 0) return true;
    const size_t text_length = len_limit(text, NAME_CAP);
    if (needle_length >= SEARCH_CAP || text_length >= NAME_CAP || needle_length > text_length)
        return false;
    for (size_t start = 0; start + needle_length <= text_length; ++start) {
        bool match = true;
        for (size_t i = 0; i < needle_length; ++i) {
            if (ascii_lower(static_cast<uint8_t>(text[start + i])) !=
                ascii_lower(static_cast<uint8_t>(needle[i]))) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool ends_with(const char* text, const char* suffix) {
    if (!text || !suffix) return false;
    const size_t text_length = len_limit(text, PATH_CAP);
    const size_t suffix_length = len_limit(suffix, 32);
    if (text_length >= PATH_CAP || suffix_length >= 32 || suffix_length > text_length)
        return false;
    for (size_t i = 0; i < suffix_length; ++i)
        if (text[text_length - suffix_length + i] != suffix[i]) return false;
    return true;
}

bool is_continuation(uint8_t value) {
    return (value & 0xC0U) == 0x80U;
}

int codepoint_bytes(uint8_t value) {
    if (value >= 0xC2U && value <= 0xDFU) return 2;
    if (value >= 0xE0U && value <= 0xEFU) return 3;
    if (value >= 0xF0U && value <= 0xF4U) return 4;
    return 1;
}

size_t next_codepoint(const char* text, size_t offset, size_t length) {
    if (!text || offset >= length) return length;
    const int count = codepoint_bytes(static_cast<uint8_t>(text[offset]));
    if (offset + static_cast<size_t>(count) > length) return offset + 1;
    for (int i = 1; i < count; ++i)
        if (!is_continuation(static_cast<uint8_t>(text[offset + i]))) return offset + 1;
    return offset + static_cast<size_t>(count);
}

size_t previous_codepoint(const char* text, size_t offset) {
    if (!text || offset == 0) return 0;
    size_t previous = offset - 1;
    while (previous > 0 &&
           is_continuation(static_cast<uint8_t>(text[previous]))) --previous;
    return previous;
}

void clip_utf8_px(const char* text, char* output, size_t capacity,
                  int max_pixels, bool keep_tail) {
    if (!output || capacity == 0) return;
    output[0] = '\0';
    if (!text || max_pixels <= 0) return;
    const size_t length = len_limit(text, PATH_CAP);
    if (length >= PATH_CAP) return;
    if (oblicz_szerokosc_tekstu(text, 1) <= max_pixels) {
        (void)copy_text(output, capacity, text);
        return;
    }
    const int dots = oblicz_szerokosc_tekstu("...", 1);
    const int room = max_pixels - dots;
    if (room <= 0 || capacity < 4) return;
    if (keep_tail) {
        size_t start = 0;
        while (start < length && oblicz_szerokosc_tekstu(text + start, 1) > room)
            start = next_codepoint(text, start, length);
        size_t out = 0;
        output[out++] = '.';
        output[out++] = '.';
        output[out++] = '.';
        while (start < length && out + 1 < capacity) output[out++] = text[start++];
        output[out] = '\0';
        return;
    }
    size_t accepted = 0;
    while (accepted < length) {
        const size_t next = next_codepoint(text, accepted, length);
        if (next + 4 > capacity) break;
        char probe[SHOW_CAP] = {};
        for (size_t i = 0; i < next; ++i) probe[i] = text[i];
        if (oblicz_szerokosc_tekstu(probe, 1) > room) break;
        accepted = next;
    }
    if (accepted + 4 > capacity) return;
    for (size_t i = 0; i < accepted; ++i) output[i] = text[i];
    output[accepted++] = '.';
    output[accepted++] = '.';
    output[accepted++] = '.';
    output[accepted] = '\0';
}

bool append_text(char* destination, size_t capacity, const char* source) {
    if (!destination || !source || capacity == 0) return false;
    const size_t current = len_limit(destination, capacity);
    const size_t addition = len_limit(source, capacity);
    if (current >= capacity || addition >= capacity || current + addition + 1 > capacity)
        return false;
    for (size_t i = 0; i <= addition; ++i) destination[current + i] = source[i];
    return true;
}

bool append_unsigned(char* destination, size_t capacity, uint64_t value) {
    char digits[32] = {};
    size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while (value && count < sizeof(digits));
    char ordered[32] = {};
    for (size_t i = 0; i < count; ++i) ordered[i] = digits[count - i - 1];
    ordered[count] = '\0';
    return append_text(destination, capacity, ordered);
}

bool valid_name(const char* name) {
    if (!name) return false;
    const size_t length = len_limit(name, NAME_CAP);
    if (length == 0 || length >= NAME_CAP || equal_text(name, ".") || equal_text(name, ".."))
        return false;
    for (size_t i = 0; i < length; ++i) {
        const uint8_t value = static_cast<uint8_t>(name[i]);
        if (value < 0x20U || value == 0x7FU || value == '/' || value == '\\')
            return false;
    }
    return true;
}

bool valid_path(const char* path) {
    if (!path || path[0] != '/') return false;
    const size_t length = len_limit(path, PATH_CAP);
    if (length == 0 || length >= PATH_CAP || (length > 1 && path[length - 1] == '/'))
        return false;
    size_t position = 1;
    while (position < length) {
        size_t end = position;
        while (end < length && path[end] != '/') ++end;
        const size_t part_length = end - position;
        if (part_length == 0 || part_length >= NAME_CAP) return false;
        char part[NAME_CAP] = {};
        for (size_t i = 0; i < part_length; ++i) part[i] = path[position + i];
        if (!valid_name(part)) return false;
        position = end + (end < length ? 1U : 0U);
    }
    return true;
}

bool join_segment(const char* base, const char* part, char* output, size_t capacity) {
    if (!base || !valid_name(part) || !output || capacity == 0) return false;
    const size_t base_length = len_limit(base, capacity);
    const size_t part_length = len_limit(part, NAME_CAP);
    if (base_length == 0 || base_length >= capacity || part_length >= NAME_CAP || base[0] != '/')
        return false;
    const bool root = base_length == 1;
    if (base_length + (root ? 0U : 1U) + part_length + 1U > capacity) return false;
    size_t out = 0;
    for (size_t i = 0; i < base_length; ++i) output[out++] = base[i];
    if (!root) output[out++] = '/';
    for (size_t i = 0; i < part_length; ++i) output[out++] = part[i];
    output[out] = '\0';
    return true;
}

bool join_relative(const char* base, const char* relative,
                   char* output, size_t capacity) {
    if (!base || !relative || !output || relative[0] == '/') return false;
    const size_t length = len_limit(relative, capacity);
    if (length == 0 || length >= capacity) return false;
    char current[PATH_CAP] = {};
    if (!copy_text(current, sizeof(current), base)) return false;
    size_t position = 0;
    while (position < length) {
        size_t end = position;
        while (end < length && relative[end] != '/') ++end;
        if (end == position || end - position >= NAME_CAP) return false;
        char part[NAME_CAP] = {};
        char next[PATH_CAP] = {};
        for (size_t i = position; i < end; ++i) part[i - position] = relative[i];
        if (!join_segment(current, part, next, sizeof(next)) ||
            !copy_text(current, sizeof(current), next)) return false;
        position = end + (end < length ? 1U : 0U);
    }
    return copy_text(output, capacity, current);
}

bool parent_path(const char* path, char* output, size_t capacity) {
    if (!path || !output || capacity < 2 || path[0] != '/') return false;
    const size_t length = len_limit(path, capacity);
    if (length == 0 || length >= capacity) return false;
    if (length == 1) return copy_text(output, capacity, "/");
    size_t slash = length;
    while (slash && path[slash - 1] != '/') --slash;
    const size_t result_length = slash <= 1 ? 1 : slash - 1;
    if (result_length + 1 > capacity) return false;
    if (result_length == 1) output[0] = '/';
    else for (size_t i = 0; i < result_length; ++i) output[i] = path[i];
    output[result_length] = '\0';
    return true;
}

uint64_t list_hash(const char* data, size_t length) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < length; ++i) {
        hash ^= static_cast<uint8_t>(data[i]);
        hash *= UINT64_C(1099511628211);
    }
    return hash ^ static_cast<uint64_t>(length);
}

enum class EntryType : uint8_t { PARENT, FOLDER, BUR, CEBULA, TXT, FILE };
enum class SortMode : uint8_t { AZ, ZA, TYPE, DATE };
enum class Change : uint8_t { ERROR, UNCHANGED, DETAILS, CHANGED };
enum class Activation : uint8_t { STATUS, NAVIGATION };

struct Entry {
    char name[NAME_CAP];
    EntryType type;
    bool directory;
    int manifest_pzb;
    uint64_t size;
    uint64_t creation_time;
    uint8_t pzb;
    bool size_available;
    bool creation_available;
    bool pzb_available;
};

struct QuickItem {
    char label[32];
    char path[PATH_CAP];
    bool pinned;
};

struct Details {
    bool valid;
    bool size_available;
    bool creation_available;
    bool pzb_available;
    uint64_t size;
    uint64_t creation_time;
    int pzb;
    char full_path[PATH_CAP];
    char application[APP_NAME_CAP];
    char entry_point[ENTRY_POINT_CAP];
};

void format_two_digits(char* output, size_t position, uint64_t value) {
    output[position] = static_cast<char>('0' + (value / 10U) % 10U);
    output[position + 1] = static_cast<char>('0' + value % 10U);
}

bool format_rtc_creation(uint64_t packed, char* output, size_t capacity) {
    if (!output || capacity < 17 || packed == 0) return false;
    const uint64_t second = packed % 100U; packed /= 100U;
    const uint64_t minute = packed % 100U; packed /= 100U;
    const uint64_t hour = packed % 100U; packed /= 100U;
    const uint64_t day = packed % 100U; packed /= 100U;
    const uint64_t month = packed % 100U; packed /= 100U;
    const uint64_t year = packed;
    if (year < 1900 || year > 9999 || month < 1 || month > 12 ||
        day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59)
        return false;
    output[0] = static_cast<char>('0' + (year / 1000U) % 10U);
    output[1] = static_cast<char>('0' + (year / 100U) % 10U);
    output[2] = static_cast<char>('0' + (year / 10U) % 10U);
    output[3] = static_cast<char>('0' + year % 10U);
    output[4] = '-'; format_two_digits(output, 5, month);
    output[7] = '-'; format_two_digits(output, 8, day);
    output[10] = ' '; format_two_digits(output, 11, hour);
    output[13] = ':'; format_two_digits(output, 14, minute);
    output[16] = '\0';
    return true;
}

const char* entry_type_name(EntryType type) {
    switch (type) {
        case EntryType::PARENT: return "Folder nadrzędny";
        case EntryType::FOLDER: return "Folder";
        case EntryType::BUR: return "Program BUR";
        case EntryType::CEBULA: return "Paczka Cebula";
        case EntryType::TXT: return "Dokument tekstowy";
        default: return "Plik";
    }
}

const char* sort_mode_name(SortMode mode) {
    switch (mode) {
        case SortMode::AZ: return "Sortuj: A-Z";
        case SortMode::ZA: return "Sortuj: Z-A";
        case SortMode::TYPE: return "Sortuj: typ";
        default: return "Sortuj: data";
    }
}

class AktowkaModel {
public:
    AktowkaModel()
        : entries_(nullptr), order_(nullptr), list_(nullptr), history_(nullptr),
          quick_(nullptr), count_(0), visible_count_(0), selected_(-1), scroll_(0),
          history_count_(0), history_pos_(-1), quick_count_(0), sort_(SortMode::AZ),
          hash_(0), hash_valid_(false) {
        path_[0] = '/';
        path_[1] = '\0';
        filter_[0] = '\0';
        zero_memory(&details_, sizeof(details_));
        set_status("Uruchamianie Eksploratora Plików...");
    }

    ~AktowkaModel() {
        gui_free(entries_);
        gui_free(order_);
        gui_free(list_);
        gui_free(history_);
        gui_free(quick_);
    }

    bool initialize() {
        entries_ = static_cast<Entry*>(gui_malloc(sizeof(Entry) * MAX_ENTRIES));
        order_ = static_cast<int*>(gui_malloc(sizeof(int) * MAX_ENTRIES));
        list_ = static_cast<char*>(gui_malloc(LIST_CAP));
        history_ = static_cast<char*>(gui_malloc(
            static_cast<unsigned long>(HISTORY_MAX) * PATH_CAP));
        quick_ = static_cast<QuickItem*>(gui_malloc(sizeof(QuickItem) * QUICK_MAX));
        if (!entries_ || !order_ || !list_ || !history_ || !quick_) {
            set_status("Błąd: brak pamięci na model Eksploratora Plików.");
            return false;
        }
        zero_memory(history_, static_cast<size_t>(HISTORY_MAX) * PATH_CAP);
        zero_memory(quick_, sizeof(QuickItem) * QUICK_MAX);
        discover_quick_access();
        if (load("/", false) == Change::ERROR) return false;
        push_history("/");
        return true;
    }

    const char* path() const { return path_; }
    const char* filter() const { return filter_; }
    const char* status() const { return status_; }
    const Details& details() const { return details_; }
    int count() const { return count_; }
    int visible_count() const { return visible_count_; }
    int selected() const { return selected_; }
    int scroll() const { return scroll_; }
    int quick_count() const { return quick_count_; }
    SortMode sort_mode() const { return sort_; }
    bool can_back() const { return history_pos_ > 0; }
    bool can_forward() const {
        return history_pos_ >= 0 && history_pos_ + 1 < history_count_;
    }
    bool has_creation_dates() const { return date_sort_available(); }

    const Entry* entry(int index) const {
        return index >= 0 && index < count_ ? &entries_[index] : nullptr;
    }

    const Entry* visible_entry(int visual_index) const {
        if (visual_index < 0 || visual_index >= visible_count_) return nullptr;
        return entry(order_[visual_index]);
    }

    int visible_entry_index(int visual_index) const {
        if (visual_index < 0 || visual_index >= visible_count_) return -1;
        return order_[visual_index];
    }

    int visible_position(int entry_index) const {
        if (entry_index < 0) return -1;
        for (int i = 0; i < visible_count_; ++i)
            if (order_[i] == entry_index) return i;
        return -1;
    }

    const QuickItem* quick(int index) const {
        return index >= 0 && index < quick_count_ ? &quick_[index] : nullptr;
    }

    void set_status(const char* text) {
        if (!copy_text(status_, sizeof(status_), text))
            (void)copy_text(status_, sizeof(status_), "Błąd komunikatu.");
    }

    Change navigate(const char* requested) {
        const bool different = !equal_text(path_, requested);
        const Change result = load(requested, false);
        if (result != Change::ERROR && different) push_history(path_);
        return result;
    }

    Change refresh() { return load(path_, true); }

    Change back() {
        if (!can_back()) {
            set_status("Brak wcześniejszej pozycji w historii.");
            return Change::ERROR;
        }
        const int next = history_pos_ - 1;
        const Change result = load(history(next), false);
        if (result != Change::ERROR) history_pos_ = next;
        return result;
    }

    Change forward() {
        if (!can_forward()) {
            set_status("Brak następnej pozycji w historii.");
            return Change::ERROR;
        }
        const int next = history_pos_ + 1;
        const Change result = load(history(next), false);
        if (result != Change::ERROR) history_pos_ = next;
        return result;
    }

    Change parent() {
        char parent[PATH_CAP] = {};
        if (!parent_path(path_, parent, sizeof(parent))) {
            set_status("Błąd: nie można wyznaczyć katalogu nadrzędnego.");
            return Change::ERROR;
        }
        if (equal_text(parent, path_)) {
            set_status("To jest katalog główny.");
            return Change::ERROR;
        }
        return navigate(parent);
    }

    bool set_filter(const char* filter) {
        if (!copy_text(filter_, sizeof(filter_), filter)) {
            set_status("Szukana nazwa przekracza 127 bajtów.");
            return false;
        }
        const int old_selected = selected_;
        rebuild_order();
        scroll_ = 0;
        if (visible_position(old_selected) < 0) clear_selection();
        status_visible_count();
        return true;
    }

    void set_sort(SortMode mode) {
        if (mode == SortMode::DATE && !date_sort_available()) {
            set_status("Sortowanie po dacie niedostępne: wpisy nie mają dat RTC.");
            return;
        }
        sort_ = mode;
        rebuild_order();
        scroll_ = 0;
        status_visible_count();
    }

    void cycle_sort() {
        switch (sort_) {
            case SortMode::AZ: set_sort(SortMode::ZA); break;
            case SortMode::ZA: set_sort(SortMode::TYPE); break;
            case SortMode::TYPE:
                if (date_sort_available()) set_sort(SortMode::DATE);
                else {
                    sort_ = SortMode::AZ;
                    rebuild_order();
                    scroll_ = 0;
                    set_status("Data niedostępna; przełączono na sortowanie A-Z.");
                }
                break;
            default: set_sort(SortMode::AZ); break;
        }
    }

    void clamp_scroll(int visible_rows) {
        if (visible_rows < 1) visible_rows = 1;
        int maximum = visible_count_ - visible_rows;
        if (maximum < 0) maximum = 0;
        if (scroll_ < 0) scroll_ = 0;
        if (scroll_ > maximum) scroll_ = maximum;
    }

    bool select_visible(int visual_index, int visible_rows, bool* scroll_changed) {
        if (scroll_changed) *scroll_changed = false;
        const int entry_index = visible_entry_index(visual_index);
        if (entry_index < 0) return false;
        selected_ = entry_index;
        const int old_scroll = scroll_;
        if (visual_index < scroll_) scroll_ = visual_index;
        if (visual_index >= scroll_ + visible_rows)
            scroll_ = visual_index - visible_rows + 1;
        clamp_scroll(visible_rows);
        if (scroll_changed) *scroll_changed = old_scroll != scroll_;
        load_selected_details();
        return true;
    }

    bool move_selection(int direction, int visible_rows,
                        int* old_entry, bool* scroll_changed) {
        if (old_entry) *old_entry = selected_;
        if (visible_count_ <= 0) return false;
        int position = visible_position(selected_);
        if (position < 0) position = direction < 0 ? visible_count_ - 1 : 0;
        else position += direction;
        position = clamp_int(position, 0, visible_count_ - 1);
        return select_visible(position, visible_rows, scroll_changed);
    }

    bool scroll_by(int rows, int visible_rows) {
        const int old = scroll_;
        scroll_ += rows;
        clamp_scroll(visible_rows);
        return old != scroll_;
    }

    Activation activate() {
        if (selected_ < 0 || selected_ >= count_) {
            set_status("Najpierw zaznacz element.");
            return Activation::STATUS;
        }
        const Entry& selected = entries_[selected_];
        if (selected.type == EntryType::PARENT)
            return parent() != Change::ERROR ? Activation::NAVIGATION : Activation::STATUS;
        char full[PATH_CAP] = {};
        if (!join_segment(path_, selected.name, full, sizeof(full))) {
            set_status("Błąd: pełna ścieżka przekracza 511 bajtów.");
            return Activation::STATUS;
        }
        if (selected.type == EntryType::FOLDER || selected.type == EntryType::CEBULA) {
            if (!selected.directory) {
                set_status("Paczka .cebula nie jest folderem.");
                return Activation::STATUS;
            }
            return navigate(full) != Change::ERROR
                ? Activation::NAVIGATION : Activation::STATUS;
        }
        if (selected.type == EntryType::BUR) {
            run_bur(full);
            return Activation::STATUS;
        }
        const wynik_otwarcia_skojarzonego result = otworz_plik_skojarzony(full);
        if (result == OTWORZ_PLIK_URUCHOMIONO)
            set_status("Plik przekazano do skojarzonej aplikacji.");
        else if (result == OTWORZ_PLIK_BRAK_SKOJARZENIA)
            set_status("Brak skojarzenia dla tego typu pliku.");
        else
            set_status("Nie można otworzyć pliku: odmowa PZB lub błąd aplikacji.");
        return Activation::STATUS;
    }

    void run_selected_package() {
        if (selected_ >= 0 && selected_ < count_ &&
            entries_[selected_].type == EntryType::CEBULA) {
            char package[PATH_CAP] = {};
            if (!join_segment(path_, entries_[selected_].name, package, sizeof(package))) {
                set_status("Paczka: ścieżka jest zbyt długa.");
                return;
            }
            run_package(package, entries_[selected_].directory);
            return;
        }
        if (ends_with(path_, ".cebula")) {
            run_package(path_, true);
            return;
        }
        set_status("Zaznacz paczkę .cebula albo otwórz ją jako folder.");
    }

    bool create_folder(const char* name) {
        return create_item(name, true);
    }

    bool create_file(const char* name) {
        return create_item(name, false);
    }

    bool selected_path(char* output, size_t capacity) const {
        if (!output || selected_ < 0 || selected_ >= count_ ||
            entries_[selected_].type == EntryType::PARENT) return false;
        return join_segment(path_, entries_[selected_].name, output, capacity);
    }

    bool entry_path(int index, char* output, size_t capacity) const {
        if (!output || index < 0 || index >= count_) return false;
        if (entries_[index].type == EntryType::PARENT)
            return parent_path(path_, output, capacity);
        return join_segment(path_, entries_[index].name, output, capacity);
    }

    bool rename_selected(const char* new_name) {
        if (!valid_name(new_name)) {
            set_status("Nowa nazwa jest nieprawidłowa (/, \\ i .. są zabronione).");
            return false;
        }
        char source[PATH_CAP] = {};
        if (!selected_path(source, sizeof(source))) {
            set_status("Nie można zmienić nazwy tego wpisu.");
            return false;
        }
        if (!zmien_nazwe_uzytkownika(source, new_name)) {
            set_status("Zmiana nazwy nie powiodła się: cel istnieje albo PZB odmówił.");
            return false;
        }
        hash_valid_ = false;
        set_status("Zmieniono nazwę elementu.");
        return true;
    }

    bool delete_selected() {
        char source[PATH_CAP] = {};
        if (!selected_path(source, sizeof(source))) {
            set_status("Nie można usunąć tego wpisu.");
            return false;
        }
        if (!usun_twor_uzytkownika(source)) {
            set_status("Nie usunięto: folder nie jest pusty, błąd PSF albo odmowa PZB.");
            return false;
        }
        hash_valid_ = false;
        set_status("Element usunięto trwale (Kosz nie jest zaimplementowany).");
        return true;
    }

    bool move_selected(const char* destination_folder) {
        if (!valid_path(destination_folder)) {
            set_status("Docelowy folder ma nieprawidłową ścieżkę.");
            return false;
        }
        char source[PATH_CAP] = {};
        if (!selected_path(source, sizeof(source))) {
            set_status("Nie można przenieść tego wpisu.");
            return false;
        }
        if (!przenies_twor_uzytkownika(source, destination_folder)) {
            set_status("Nie przeniesiono: cel istnieje, własne poddrzewo, błąd PSF lub PZB.");
            return false;
        }
        hash_valid_ = false;
        set_status("Element przeniesiono bez nadpisywania celu.");
        return true;
    }

private:
    Entry* entries_;
    int* order_;
    char* list_;
    char* history_;
    QuickItem* quick_;
    int count_;
    int visible_count_;
    int selected_;
    int scroll_;
    int history_count_;
    int history_pos_;
    int quick_count_;
    SortMode sort_;
    char path_[PATH_CAP];
    char filter_[SEARCH_CAP];
    char status_[STATUS_CAP];
    Details details_;
    uint64_t hash_;
    bool hash_valid_;

    char* history(int index) {
        return history_ + static_cast<size_t>(index) * PATH_CAP;
    }

    const char* history(int index) const {
        return history_ + static_cast<size_t>(index) * PATH_CAP;
    }

    void push_history(const char* path) {
        if (!history_ || !path) return;
        if (history_pos_ >= 0 && equal_text(history(history_pos_), path)) return;
        history_count_ = history_pos_ + 1;
        if (history_count_ == HISTORY_MAX) {
            for (int i = 1; i < HISTORY_MAX; ++i)
                (void)copy_text(history(i - 1), PATH_CAP, history(i));
            history_count_ = HISTORY_MAX - 1;
            history_pos_ = history_count_ - 1;
        }
        if (copy_text(history(history_count_), PATH_CAP, path)) {
            ++history_count_;
            history_pos_ = history_count_ - 1;
        }
    }

    bool directory_exists(const char* path) {
        if (!path || !list_) return false;
        zero_memory(list_, LIST_CAP);
        return wylistuj_katalog_uzytkownika(path, list_, LIST_CAP);
    }

    void add_quick(const char* label, const char* path, bool pinned,
                   bool must_exist) {
        if (quick_count_ >= QUICK_MAX || !label || !path) return;
        if (must_exist && !directory_exists(path)) return;
        QuickItem& item = quick_[quick_count_];
        zero_memory(&item, sizeof(item));
        if (!copy_text(item.label, sizeof(item.label), label) ||
            !copy_text(item.path, sizeof(item.path), path)) return;
        item.pinned = pinned;
        ++quick_count_;
    }

    void discover_quick_access() {
        quick_count_ = 0;
        add_quick("Pulpit", "/uzytkownicy/Pulpit", false, true);
        add_quick("Użytkownicy", "/uzytkownicy", false, true);
        add_quick("Programy", "/programy", false, true);
        if (directory_exists("/uzytkownicy/dokumenty"))
            add_quick("Dokumenty", "/uzytkownicy/dokumenty", false, false);
        else
            add_quick("Dokumenty", "/dokumenty", false, true);
        if (directory_exists("/kosz"))
            add_quick("Kosz", "/kosz", false, false);
        else
            add_quick("Kosz", "/uzytkownicy/kosz", false, true);
        add_quick("System", "/system", true, true);
        add_quick("Logi", "/logi", true, true);
        add_quick("Tymczasowe", "/tymczasowe", true, true);
    }

    bool validate_listing(size_t length, size_t* target_count) {
        if (!target_count) return false;
        size_t position = 0;
        while (position < length) {
            size_t end = position;
            while (end < length && list_[end] != '\n') ++end;
            if (end >= length || end - position <= 7) {
                set_status("BWS6: uszkodzony rekord listy katalogu.");
                return false;
            }
            const bool directory =
                list_[position] == '[' && list_[position + 1] == 'K' &&
                list_[position + 2] == 'A' && list_[position + 3] == 'T' &&
                list_[position + 4] == ']' && list_[position + 5] == ' ' &&
                list_[position + 6] == ' ';
            const bool file =
                list_[position] == '[' && list_[position + 1] == 'P' &&
                list_[position + 2] == 'L' && list_[position + 3] == 'I' &&
                list_[position + 4] == 'K' && list_[position + 5] == ']' &&
                list_[position + 6] == ' ';
            if (!directory && !file) {
                set_status("BWS6: nieznany typ rekordu katalogu.");
                return false;
            }
            const size_t name_length = end - (position + 7);
            if (name_length == 0 || name_length >= NAME_CAP) {
                set_status("BWS6: nazwa przekracza limit 55 bajtów.");
                return false;
            }
            char name[NAME_CAP] = {};
            for (size_t i = 0; i < name_length; ++i)
                name[i] = list_[position + 7 + i];
            if (!valid_name(name)) {
                set_status("BWS6: wpis zawiera niebezpieczną nazwę.");
                return false;
            }
            if (++(*target_count) > static_cast<size_t>(MAX_ENTRIES)) {
                set_status("Katalog zawiera zbyt wiele wpisów.");
                return false;
            }
            position = end + 1;
        }
        return true;
    }

    void fill_entries(size_t length, size_t* index, const char* base_path) {
        size_t position = 0;
        while (position < length && *index < static_cast<size_t>(MAX_ENTRIES)) {
            size_t end = position;
            while (end < length && list_[end] != '\n') ++end;
            Entry& item = entries_[(*index)++];
            zero_memory(&item, sizeof(item));
            item.directory = list_[position + 1] == 'K';
            item.manifest_pzb = -1;
            const size_t name_length = end - (position + 7);
            for (size_t i = 0; i < name_length; ++i)
                item.name[i] = list_[position + 7 + i];
            if (ends_with(item.name, ".cebula")) item.type = EntryType::CEBULA;
            else if (item.directory) item.type = EntryType::FOLDER;
            else if (ends_with(item.name, ".bur")) item.type = EntryType::BUR;
            else if (ends_with(item.name, ".txt")) item.type = EntryType::TXT;
            else item.type = EntryType::FILE;
            char full[PATH_CAP] = {};
            BwsMetadanePliku meta{};
            if (join_segment(base_path, item.name, full, sizeof(full)) &&
                pobierz_metadane_pliku(full, &meta) &&
                meta.wersja == BWS_METADANE_WERSJA) {
                item.size = meta.rozmiar;
                item.creation_time = meta.czas_utworzenia_rtc;
                item.pzb = meta.poziom_pzb;
                item.size_available = (meta.flagi & BWS_META_ROZMIAR_DOSTEPNY) != 0;
                item.creation_available = (meta.flagi & BWS_META_CZAS_DOSTEPNY) != 0;
                item.pzb_available = (meta.flagi & BWS_META_PZB_DOSTEPNY) != 0;
            }
            position = end + 1;
        }
    }

    bool date_sort_available() const {
        for (int i = 0; i < count_; ++i)
            if (entries_[i].type != EntryType::PARENT && entries_[i].creation_available)
                return true;
        return false;
    }

    int type_sort_value(EntryType type) const {
        switch (type) {
            case EntryType::PARENT: return -1;
            case EntryType::FOLDER: return 0;
            case EntryType::CEBULA: return 1;
            case EntryType::BUR: return 2;
            case EntryType::TXT: return 3;
            default: return 4;
        }
    }

    int compare_entries(int left_index, int right_index) const {
        const Entry& left = entries_[left_index];
        const Entry& right = entries_[right_index];
        if (left.type == EntryType::PARENT || right.type == EntryType::PARENT) {
            if (left.type == right.type) return 0;
            return left.type == EntryType::PARENT ? -1 : 1;
        }
        int result = 0;
        if (sort_ == SortMode::TYPE) {
            const int left_type = type_sort_value(left.type);
            const int right_type = type_sort_value(right.type);
            if (left_type < right_type) result = -1;
            else if (left_type > right_type) result = 1;
        }
        if (sort_ == SortMode::DATE) {
            if (left.creation_available != right.creation_available)
                result = left.creation_available ? -1 : 1;
            else if (left.creation_available) {
                if (left.creation_time < right.creation_time) result = -1;
                else if (left.creation_time > right.creation_time) result = 1;
            }
        }
        if (result == 0) result = compare_text_ci(left.name, right.name);
        if (sort_ == SortMode::ZA) result = -result;
        return result;
    }

    void sift_down(int root, int length) {
        while (true) {
            int child = root * 2 + 1;
            if (child >= length) return;
            if (child + 1 < length && compare_entries(order_[child], order_[child + 1]) < 0)
                ++child;
            if (compare_entries(order_[root], order_[child]) >= 0) return;
            const int temp = order_[root];
            order_[root] = order_[child];
            order_[child] = temp;
            root = child;
        }
    }

    void sort_order() {
        if (visible_count_ < 2) return;
        int start = 0;
        if (entries_[order_[0]].type == EntryType::PARENT) start = 1;
        const int length = visible_count_ - start;
        if (length < 2) return;
        int* original = order_;
        order_ += start;
        for (int root = length / 2 - 1; root >= 0; --root) sift_down(root, length);
        for (int end = length - 1; end > 0; --end) {
            const int temp = order_[0];
            order_[0] = order_[end];
            order_[end] = temp;
            sift_down(0, end);
        }
        order_ = original;
    }

    void rebuild_order() {
        visible_count_ = 0;
        for (int i = 0; i < count_; ++i) {
            if (entries_[i].type == EntryType::PARENT ||
                contains_text_ci(entries_[i].name, filter_))
                order_[visible_count_++] = i;
        }
        sort_order();
    }

    void clear_selection() {
        selected_ = -1;
        zero_memory(&details_, sizeof(details_));
    }

    void status_visible_count() {
        char message[STATUS_CAP] = {};
        (void)append_text(message, sizeof(message), "Widoczne wpisy: ");
        int rzeczywiste = visible_count_;
        if (!equal_text(path_, "/") && rzeczywiste > 0) --rzeczywiste;
        (void)append_unsigned(message, sizeof(message), static_cast<uint64_t>(rzeczywiste));
        (void)append_text(message, sizeof(message), ".");
        set_status(message);
    }

    bool same_details(const Details& left, const Details& right) const {
        return left.valid == right.valid &&
               left.size_available == right.size_available &&
               left.creation_available == right.creation_available &&
               left.pzb_available == right.pzb_available &&
               left.size == right.size && left.creation_time == right.creation_time &&
               left.pzb == right.pzb &&
               equal_text(left.full_path, right.full_path) &&
               equal_text(left.application, right.application) &&
               equal_text(left.entry_point, right.entry_point);
    }

    Change load(const char* requested, bool periodic) {
        if (!valid_path(requested)) {
            set_status("Błąd: nieprawidłowa albo zbyt długa ścieżka.");
            return Change::ERROR;
        }
        char target_path[PATH_CAP] = {};
        if (!copy_text(target_path, sizeof(target_path), requested)) {
            set_status("Błąd: ścieżka przekracza 511 bajtów.");
            return Change::ERROR;
        }
        zero_memory(list_, LIST_CAP);
        if (!wylistuj_katalog_uzytkownika(target_path, list_, LIST_CAP)) {
            set_status("BWS6: katalog nie istnieje albo brak prawa odczytu.");
            return Change::ERROR;
        }
        const size_t length = len_limit(list_, LIST_CAP);
        if (length >= LIST_CAP || (length && list_[length - 1] != '\n')) {
            set_status("BWS6: lista katalogu jest niepełna (limit 64 KiB).");
            return Change::ERROR;
        }
        const uint64_t new_hash = list_hash(list_, length);
        if (periodic && equal_text(target_path, path_) && hash_valid_ && new_hash == hash_) {
            if (selected_ < 0 || selected_ >= count_) return Change::UNCHANGED;
            const Details previous = details_;
            const int previous_pzb = entries_[selected_].manifest_pzb;
            load_selected_details();
            return same_details(previous, details_) &&
                   previous_pzb == entries_[selected_].manifest_pzb
                ? Change::UNCHANGED : Change::DETAILS;
        }

        char old_name[NAME_CAP] = {};
        bool old_directory = false;
        if (selected_ >= 0 && selected_ < count_) {
            (void)copy_text(old_name, sizeof(old_name), entries_[selected_].name);
            old_directory = entries_[selected_].directory;
        }
        const int old_scroll = scroll_;
        size_t target_count = equal_text(target_path, "/") ? 0U : 1U;
        if (!validate_listing(length, &target_count)) return Change::ERROR;
        size_t index = 0;
        if (!equal_text(target_path, "/")) {
            Entry& parent = entries_[0];
            zero_memory(&parent, sizeof(parent));
            (void)copy_text(parent.name, sizeof(parent.name), "..");
            parent.type = EntryType::PARENT;
            parent.directory = true;
            parent.manifest_pzb = -1;
            index = 1;
        }
        fill_entries(length, &index, target_path);
        if (index != target_count || !copy_text(path_, sizeof(path_), target_path)) {
            set_status("Błąd wewnętrzny parsera listy katalogu.");
            return Change::ERROR;
        }
        count_ = static_cast<int>(index);
        clear_selection();
        if (old_name[0]) {
            for (int i = 0; i < count_; ++i) {
                if (entries_[i].directory == old_directory &&
                    equal_text(entries_[i].name, old_name)) {
                    selected_ = i;
                    break;
                }
            }
        }
        rebuild_order();
        if (visible_position(selected_) < 0) clear_selection();
        else load_selected_details();
        scroll_ = periodic ? old_scroll : 0;
        hash_ = new_hash;
        hash_valid_ = true;
        status_visible_count();
        return Change::CHANGED;
    }

    bool extract_manifest_quoted(const char* manifest, size_t length,
                                 const char* key, char* output, size_t capacity) {
        if (!manifest || !key || !output || capacity == 0) return false;
        output[0] = '\0';
        bool found = false;
        size_t position = 0;
        const size_t key_length = len_limit(key, 64);
        while (position < length) {
            size_t end = position;
            while (end < length && manifest[end] != '\n' && manifest[end] != '\r') ++end;
            size_t cursor = position;
            while (cursor < end && (manifest[cursor] == ' ' || manifest[cursor] == '\t')) ++cursor;
            bool same_key = cursor + key_length <= end;
            for (size_t i = 0; same_key && i < key_length; ++i)
                if (manifest[cursor + i] != key[i]) same_key = false;
            if (same_key && cursor + key_length < end &&
                manifest[cursor + key_length] != '=' &&
                manifest[cursor + key_length] != ' ' &&
                manifest[cursor + key_length] != '\t') same_key = false;
            if (same_key) {
                cursor += key_length;
                while (cursor < end && (manifest[cursor] == ' ' || manifest[cursor] == '\t')) ++cursor;
                if (cursor >= end || manifest[cursor++] != '=' || found) return false;
                while (cursor < end && (manifest[cursor] == ' ' || manifest[cursor] == '\t')) ++cursor;
                if (cursor >= end || manifest[cursor++] != '"') return false;
                size_t out = 0;
                while (cursor < end && manifest[cursor] != '"') {
                    const uint8_t value = static_cast<uint8_t>(manifest[cursor]);
                    if (value < 0x20U || value == '\\' || out + 1 >= capacity) return false;
                    output[out++] = manifest[cursor++];
                }
                if (cursor >= end || manifest[cursor++] != '"' || out == 0) return false;
                while (cursor < end && (manifest[cursor] == ' ' || manifest[cursor] == '\t')) ++cursor;
                if (cursor != end) return false;
                output[out] = '\0';
                found = true;
            }
            position = end;
            while (position < length &&
                   (manifest[position] == '\n' || manifest[position] == '\r')) ++position;
        }
        return found;
    }

    bool extract_manifest_number(const char* manifest, size_t length,
                                 const char* key, int* value) {
        if (!manifest || !key || !value) return false;
        bool found = false;
        size_t position = 0;
        const size_t key_length = len_limit(key, 64);
        while (position < length) {
            size_t end = position;
            while (end < length && manifest[end] != '\n' && manifest[end] != '\r') ++end;
            size_t cursor = position;
            while (cursor < end && (manifest[cursor] == ' ' || manifest[cursor] == '\t')) ++cursor;
            bool same_key = cursor + key_length <= end;
            for (size_t i = 0; same_key && i < key_length; ++i)
                if (manifest[cursor + i] != key[i]) same_key = false;
            if (same_key && cursor + key_length < end &&
                manifest[cursor + key_length] != '=' &&
                manifest[cursor + key_length] != ' ' &&
                manifest[cursor + key_length] != '\t') same_key = false;
            if (same_key) {
                cursor += key_length;
                while (cursor < end && (manifest[cursor] == ' ' || manifest[cursor] == '\t')) ++cursor;
                if (cursor >= end || manifest[cursor++] != '=' || found) return false;
                while (cursor < end && (manifest[cursor] == ' ' || manifest[cursor] == '\t')) ++cursor;
                if (cursor >= end || manifest[cursor] < '0' || manifest[cursor] > '9') return false;
                int parsed = 0;
                while (cursor < end && manifest[cursor] >= '0' && manifest[cursor] <= '9') {
                    if (parsed > 1000) return false;
                    parsed = parsed * 10 + (manifest[cursor++] - '0');
                }
                while (cursor < end && (manifest[cursor] == ' ' || manifest[cursor] == '\t')) ++cursor;
                if (cursor != end) return false;
                *value = parsed;
                found = true;
            }
            position = end;
            while (position < length &&
                   (manifest[position] == '\n' || manifest[position] == '\r')) ++position;
        }
        return found;
    }

    bool read_package_manifest(const char* package, char* application,
                               size_t application_capacity, char* entry,
                               size_t entry_capacity, int* pzb, bool* pzb_available) {
        if (application && application_capacity) application[0] = '\0';
        if (entry && entry_capacity) entry[0] = '\0';
        if (pzb_available) *pzb_available = false;
        char manifest_path[PATH_CAP] = {};
        if (!join_segment(package, "opis.aplikacji", manifest_path, sizeof(manifest_path)))
            return false;
        uint32_t size = 0;
        if (!pobierz_rozmiar_pliku(manifest_path, &size) || size == 0 || size > MANIFEST_MAX)
            return false;
        char* manifest = static_cast<char*>(gui_malloc(static_cast<unsigned long>(size) + 1U));
        if (!manifest) return false;
        zero_memory(manifest, static_cast<size_t>(size) + 1U);
        if (!czytaj_plik(manifest_path, manifest, size)) {
            gui_free(manifest);
            return false;
        }
        for (uint32_t i = 0; i < size; ++i) {
            if (manifest[i] == '\0') {
                gui_free(manifest);
                return false;
            }
        }
        manifest[size] = '\0';
        bool any = false;
        if (application && application_capacity)
            any = extract_manifest_quoted(manifest, size, "nazwa",
                                          application, application_capacity) || any;
        if (entry && entry_capacity)
            any = extract_manifest_quoted(manifest, size, "plik_startowy",
                                          entry, entry_capacity) || any;
        int parsed_pzb = 0;
        if (pzb && pzb_available &&
            extract_manifest_number(manifest, size, "poziom_zaufania", &parsed_pzb)) {
            *pzb = parsed_pzb;
            *pzb_available = true;
            any = true;
        }
        gui_free(manifest);
        return any;
    }

    void load_selected_details() {
        zero_memory(&details_, sizeof(details_));
        if (selected_ < 0 || selected_ >= count_) return;
        Entry& selected = entries_[selected_];
        if (selected.type == EntryType::PARENT) {
            if (!parent_path(path_, details_.full_path, sizeof(details_.full_path))) return;
        } else if (!join_segment(path_, selected.name, details_.full_path,
                                 sizeof(details_.full_path))) {
            return;
        }
        details_.valid = true;
        BwsMetadanePliku meta{};
        if (pobierz_metadane_pliku(details_.full_path, &meta) &&
            meta.wersja == BWS_METADANE_WERSJA) {
            details_.size_available = (meta.flagi & BWS_META_ROZMIAR_DOSTEPNY) != 0;
            details_.creation_available = (meta.flagi & BWS_META_CZAS_DOSTEPNY) != 0;
            details_.pzb_available = (meta.flagi & BWS_META_PZB_DOSTEPNY) != 0;
            details_.size = meta.rozmiar;
            details_.creation_time = meta.czas_utworzenia_rtc;
            details_.pzb = meta.poziom_pzb;
        }
        if (selected.type == EntryType::CEBULA && selected.directory) {
            int pzb = 0;
            bool has_pzb = false;
            (void)read_package_manifest(details_.full_path,
                details_.application, sizeof(details_.application),
                details_.entry_point, sizeof(details_.entry_point),
                &pzb, &has_pzb);
            if (has_pzb) selected.manifest_pzb = pzb;
        }
    }

    void run_bur(const char* path) {
        if (len_limit(path, PATH_CAP) >= 64U) {
            set_status("Loader: ścieżka .bur przekracza 63 bajty.");
            return;
        }
        if (uruchom_program_uzytkownika(path))
            set_status("Program przekazano do loadera BWS10.");
        else
            set_status("BWS10: odmowa PZB, błąd loadera lub program już działa.");
    }

    void run_package(const char* package, bool directory) {
        if (!directory) {
            set_status("Paczka .cebula musi być folderem z manifestem.");
            return;
        }
        char application[APP_NAME_CAP] = {};
        char entry[ENTRY_POINT_CAP] = {};
        int pzb = 0;
        bool has_pzb = false;
        if (!read_package_manifest(package, application, sizeof(application),
                                   entry, sizeof(entry), &pzb, &has_pzb) ||
            entry[0] == '\0' || !ends_with(entry, ".bur")) {
            set_status("Paczka: brak poprawnego plik_startowy .bur w manifeście.");
            return;
        }
        char program[PATH_CAP] = {};
        if (!join_relative(package, entry, program, sizeof(program))) {
            set_status("Paczka: niebezpieczny albo zbyt długi plik_startowy.");
            return;
        }
        run_bur(program);
    }

    bool create_item(const char* name, bool directory) {
        if (!valid_name(name)) {
            set_status("Nazwa jest pusta, za długa albo zawiera /, \\ lub ..");
            return false;
        }
        char full[PATH_CAP] = {};
        if (!join_segment(path_, name, full, sizeof(full))) {
            set_status("Nie można utworzyć elementu: ścieżka przekracza 511 bajtów.");
            return false;
        }
        const bool success = directory
            ? utworz_katalog_uzytkownika(full)
            : utworz(full);
        if (!success) {
            set_status(directory
                ? "Nie utworzono folderu: istnieje albo PZB odmówił zapisu."
                : "Nie utworzono pliku: istnieje albo PZB odmówił zapisu.");
            return false;
        }
        set_status(directory ? "Utworzono nowy folder." : "Utworzono nowy plik.");
        hash_valid_ = false;
        return true;
    }
};

class AktowkaWindow {
public:
    int x = DEF_X;
    int y = DEF_Y;
    int w = DEF_W;
    int h = DEF_H;
    int restore_x = DEF_X;
    int restore_y = DEF_Y;
    int restore_w = DEF_W;
    int restore_h = DEF_H;
    int screen_w = 1024;
    int screen_h = 768;
    bool maximized = false;
    bool minimized = false;

    void clamp() {
        if (screen_w < 1) screen_w = 1;
        if (screen_h < 1) screen_h = 1;
        int available_h = screen_h - TASKBAR_H;
        if (available_h < 1) available_h = screen_h;
        const int effective_min_w = screen_w < MIN_W ? screen_w : MIN_W;
        const int effective_min_h = available_h < MIN_H ? available_h : MIN_H;
        w = clamp_int(w, effective_min_w, screen_w);
        h = clamp_int(h, effective_min_h, available_h);
        x = clamp_int(x, 0, screen_w - w);
        y = clamp_int(y, 0, available_h - h);
    }

    bool create_layer() {
        clamp();
        return bws_utworz_warstwe(x, y, w, h, Z_ORDER) >= 0;
    }

    bool toggle_maximize() {
        const int old_x = x;
        const int old_y = y;
        const int old_w = w;
        const int old_h = h;
        if (!maximized) {
            restore_x = x;
            restore_y = y;
            restore_w = w;
            restore_h = h;
            x = 0;
            y = 0;
            w = screen_w;
            h = screen_h - TASKBAR_H;
            if (h < 1) h = screen_h;
        } else {
            x = restore_x;
            y = restore_y;
            w = restore_w;
            h = restore_h;
        }
        clamp();
        if (!create_layer()) {
            x = old_x;
            y = old_y;
            w = old_w;
            h = old_h;
            (void)create_layer();
            return false;
        }
        maximized = !maximized;
        return true;
    }
};

struct AktowkaLayout {
    Rect toolbar;
    Rect back;
    Rect forward;
    Rect up;
    Rect refresh;
    Rect new_folder;
    Rect new_file;
    Rect rename;
    Rect remove;
    Rect move;
    Rect run;
    Rect path;
    Rect search;
    Rect sort;
    Rect content;
    Rect left;
    Rect center;
    Rect right;
    Rect left_divider;
    Rect right_divider;
    Rect status;
    Rect center_title;
    Rect table_header;
    Rect list_body;
    Rect scroll_up;
    Rect scroll_down;
    Rect details_body;
    Rect col_name;
    Rect col_type;
    Rect col_date;
    Rect col_security;
};

/* Jedyne zrodlo geometrii calego klienta Aktowki. */
AktowkaLayout calculate_layout(const AktowkaWindow& window) {
    AktowkaLayout layout{};
    const int client_x = window.x + 3;
    const int client_w = window.w > 6 ? window.w - 6 : 0;
    const int client_bottom = window.y + window.h - 3;

    layout.toolbar = {
        client_x,
        window.y + TITLE_H + 2,
        client_w,
        TOOLBAR_H
    };

    const int row_x = layout.toolbar.x + MARGIN;
    const int row_w = layout.toolbar.w > MARGIN * 2
        ? layout.toolbar.w - MARGIN * 2 : 0;
    const int row_y = layout.toolbar.y + 2;
    const int nav_nominal[5] = {32, 32, 32, 88, 76};
    Rect* nav_buttons[5] = {
        &layout.back, &layout.forward, &layout.up, &layout.refresh, &layout.run
    };
    const int nav_nominal_total = 260;
    const int nav_available = row_w > GAP * 4 ? row_w - GAP * 4 : 0;
    const int nav_scale_base = nav_available < nav_nominal_total
        ? nav_nominal_total : nav_available;
    int button_x = row_x;
    for (int i = 0; i < 5; ++i) {
        int width = nav_nominal[i];
        if (nav_available < nav_nominal_total)
            width = nav_nominal[i] * nav_available / nav_scale_base;
        *nav_buttons[i] = {button_x, row_y, width, TOOL_H};
        button_x += width + GAP;
    }

    const int action_y = row_y + TOOL_H + GAP;
    const int action_nominal[5] = {110, 90, 110, 70, 90};
    Rect* action_buttons[5] = {
        &layout.new_folder, &layout.new_file, &layout.rename,
        &layout.remove, &layout.move
    };
    const int action_nominal_total = 470;
    const int action_available = row_w > GAP * 4 ? row_w - GAP * 4 : 0;
    button_x = row_x;
    for (int i = 0; i < 5; ++i) {
        int width = action_nominal[i];
        if (action_available < action_nominal_total)
            width = action_nominal[i] * action_available / action_nominal_total;
        *action_buttons[i] = {button_x, action_y, width, TOOL_H};
        button_x += width + GAP;
    }

    const int field_y = action_y + TOOL_H + GAP;
    int path_w = row_w * 52 / 100;
    int search_w = row_w * 25 / 100;
    int sort_w = row_w - path_w - search_w - GAP * 2;
    if (sort_w < 0) sort_w = 0;
    layout.path = {row_x, field_y, path_w, TOOL_H};
    layout.search = {row_x + path_w + GAP, field_y, search_w, TOOL_H};
    layout.sort = {layout.search.x + search_w + GAP, field_y, sort_w, TOOL_H};

    layout.status = {
        client_x + 1,
        client_bottom - STATUS_H,
        client_w > 2 ? client_w - 2 : 0,
        STATUS_H
    };
    const int content_y = layout.toolbar.y + layout.toolbar.h + 3;
    layout.content = {
        client_x + 1,
        content_y,
        client_w > 2 ? client_w - 2 : 0,
        layout.status.y > content_y ? layout.status.y - content_y - 2 : 0
    };

    int left_w = clamp_int(layout.content.w * 19 / 100, 105, 205);
    int right_w = clamp_int(layout.content.w * 25 / 100, 145, 275);
    const int center_min = layout.content.w >= 500 ? 250 : 120;
    const int side_budget = layout.content.w - center_min - 2;
    if (side_budget < left_w + right_w) {
        const int budget = side_budget > 0 ? side_budget : 0;
        const int total = left_w + right_w;
        left_w = total > 0 ? budget * left_w / total : 0;
        right_w = budget - left_w;
    }
    left_w = left_w > 0 ? left_w : 0;
    right_w = right_w > 0 ? right_w : 0;
    int center_w = layout.content.w - left_w - right_w - 2;
    if (center_w < 0) center_w = 0;

    layout.left = {layout.content.x, layout.content.y, left_w, layout.content.h};
    layout.left_divider = {
        layout.left.x + layout.left.w,
        layout.content.y,
        layout.content.w > 0 ? 1 : 0,
        layout.content.h
    };
    layout.center = {
        layout.left_divider.x + layout.left_divider.w,
        layout.content.y,
        center_w,
        layout.content.h
    };
    layout.right_divider = {
        layout.center.x + layout.center.w,
        layout.content.y,
        layout.content.w > 1 ? 1 : 0,
        layout.content.h
    };
    layout.right = {
        layout.right_divider.x + layout.right_divider.w,
        layout.content.y,
        right_w,
        layout.content.h
    };

    layout.center_title = {
        layout.center.x, layout.center.y, layout.center.w, PANEL_TITLE_H
    };
    layout.table_header = {
        layout.center.x,
        layout.center.y + PANEL_TITLE_H,
        layout.center.w,
        TABLE_HEAD_H
    };
    const int list_y = layout.table_header.y + layout.table_header.h;
    const int list_h = layout.center.y + layout.center.h > list_y
        ? layout.center.y + layout.center.h - list_y : 0;
    const int scrollbar = layout.center.w > SCROLL_W + 40 ? SCROLL_W : 0;
    layout.list_body = {
        layout.center.x,
        list_y,
        layout.center.w - scrollbar,
        list_h
    };
    if (layout.list_body.w < 0) layout.list_body.w = 0;
    layout.scroll_up = {
        layout.center.x + layout.center.w - scrollbar,
        list_y,
        scrollbar,
        scrollbar
    };
    layout.scroll_down = {
        layout.scroll_up.x,
        layout.center.y + layout.center.h - scrollbar,
        scrollbar,
        scrollbar
    };
    layout.details_body = {
        layout.right.x,
        layout.right.y + PANEL_TITLE_H,
        layout.right.w,
        layout.right.h > PANEL_TITLE_H ? layout.right.h - PANEL_TITLE_H : 0
    };

    const int table_w = layout.list_body.w;
    int name_w = table_w * 42 / 100;
    int type_w = table_w * 23 / 100;
    int date_w = table_w * 20 / 100;
    int security_w = table_w - name_w - type_w - date_w;
    if (security_w < 0) security_w = 0;
    layout.col_name = {layout.table_header.x, layout.table_header.y, name_w, TABLE_HEAD_H};
    layout.col_type = {layout.col_name.x + name_w, layout.table_header.y, type_w, TABLE_HEAD_H};
    layout.col_date = {layout.col_type.x + type_w, layout.table_header.y, date_w, TABLE_HEAD_H};
    layout.col_security = {layout.col_date.x + date_w, layout.table_header.y,
                           security_w, TABLE_HEAD_H};
    return layout;
}

enum class EditMode : uint8_t {
    NONE, PATH, SEARCH, CREATE_FOLDER, CREATE_FILE, RENAME, MOVE, CONFIRM_DELETE
};

class AktowkaView {
public:
    AktowkaView(AktowkaModel& model, AktowkaWindow& window)
        : model_(model), window_(window), edit_mode_(EditMode::NONE),
          edit_length_(0), edit_caret_(0), edit_view_start_(0),
          drop_target_entry_(-1), drop_target_body_(false) {
        edit_[0] = '\0';
    }

    AktowkaLayout layout() const { return calculate_layout(window_); }
    EditMode edit_mode() const { return edit_mode_; }
    const char* edit_text() const { return edit_; }

    void begin_edit(EditMode mode) {
        const char* initial = "";
        if (mode == EditMode::PATH) {
            initial = model_.path();
        } else if (mode == EditMode::SEARCH) {
            initial = model_.filter();
        } else if (mode == EditMode::RENAME) {
            const Entry* selected = model_.entry(model_.selected());
            if (selected && selected->type != EntryType::PARENT) initial = selected->name;
        } else if (mode == EditMode::MOVE) {
            initial = model_.path();
        }
        if (!copy_text(edit_, sizeof(edit_), initial)) return;
        edit_length_ = len_limit(edit_, sizeof(edit_));
        edit_caret_ = edit_length_;
        edit_view_start_ = 0;
        edit_mode_ = mode;
    }

    void end_edit() {
        edit_mode_ = EditMode::NONE;
        edit_[0] = '\0';
        edit_length_ = 0;
        edit_caret_ = 0;
        edit_view_start_ = 0;
    }

    bool append_input(char character) {
        const uint8_t value = static_cast<uint8_t>(character);
        if (value < 0x20U || value == 0x7FU) return false;
        size_t capacity = sizeof(edit_);
        if (edit_mode_ == EditMode::SEARCH) capacity = SEARCH_CAP;
        if (edit_mode_ == EditMode::CREATE_FOLDER || edit_mode_ == EditMode::CREATE_FILE ||
            edit_mode_ == EditMode::RENAME)
            capacity = NAME_CAP;
        if (edit_length_ + 1 >= capacity) {
            model_.set_status(edit_mode_ == EditMode::PATH
                ? "Ścieżka osiągnęła limit 511 bajtów."
                : "Pole osiągnęło swój limit długości.");
            return false;
        }
        for (size_t i = edit_length_ + 1; i > edit_caret_; --i)
            edit_[i] = edit_[i - 1];
        edit_[edit_caret_++] = character;
        ++edit_length_;
        return true;
    }

    bool erase_input() {
        if (edit_caret_ == 0) return false;
        const size_t previous = previous_codepoint(edit_, edit_caret_);
        const size_t removed = edit_caret_ - previous;
        for (size_t i = previous; i + removed <= edit_length_; ++i)
            edit_[i] = edit_[i + removed];
        edit_length_ -= removed;
        edit_caret_ = previous;
        if (edit_view_start_ > edit_caret_) edit_view_start_ = edit_caret_;
        return true;
    }

    bool delete_input() {
        if (edit_caret_ >= edit_length_) return false;
        const size_t next = next_codepoint(edit_, edit_caret_, edit_length_);
        const size_t removed = next - edit_caret_;
        for (size_t i = edit_caret_; i + removed <= edit_length_; ++i)
            edit_[i] = edit_[i + removed];
        edit_length_ -= removed;
        return true;
    }

    bool move_caret_left() {
        if (edit_caret_ == 0) return false;
        edit_caret_ = previous_codepoint(edit_, edit_caret_);
        if (edit_caret_ < edit_view_start_) edit_view_start_ = edit_caret_;
        return true;
    }

    bool move_caret_right() {
        if (edit_caret_ >= edit_length_) return false;
        edit_caret_ = next_codepoint(edit_, edit_caret_, edit_length_);
        return true;
    }

    void place_path_caret(int mouse_x) {
        if (edit_mode_ != EditMode::PATH) return;
        const AktowkaLayout value = layout();
        const int prefix = oblicz_szerokosc_tekstu("Ścieżka: ", 1);
        int wanted = mouse_x - (value.path.x + 5 + prefix);
        if (wanted <= 0) {
            edit_caret_ = edit_view_start_;
            return;
        }
        size_t position = edit_view_start_;
        while (position < edit_length_) {
            const size_t next = next_codepoint(edit_, position, edit_length_);
            char fragment[8] = {};
            size_t n = next - position;
            for (size_t i = 0; i < n && i + 1 < sizeof(fragment); ++i)
                fragment[i] = edit_[position + i];
            const int width = oblicz_szerokosc_tekstu(fragment, 1);
            if (wanted < width / 2) break;
            wanted -= width;
            position = next;
        }
        edit_caret_ = position;
    }

    int visible_rows() const {
        const AktowkaLayout value = layout();
        const int rows = value.list_body.h / ROW_H;
        return rows > 0 ? rows : 1;
    }

    int quick_at(int mx, int my) const {
        const AktowkaLayout value = layout();
        int y = value.left.y + PANEL_TITLE_H + 4;
        bool pinned_header = false;
        for (int i = 0; i < model_.quick_count(); ++i) {
            const QuickItem* item = model_.quick(i);
            if (!item) continue;
            if (item->pinned && !pinned_header) {
                y += 23;
                pinned_header = true;
            }
            const Rect row{value.left.x + 2, y, value.left.w > 4 ? value.left.w - 4 : 0, ROW_H};
            if (hit(mx, my, row)) return i;
            y += ROW_H;
        }
        return -1;
    }

    int row_at(int mx, int my) const {
        const AktowkaLayout value = layout();
        if (!hit(mx, my, value.list_body)) return -1;
        const int local = (my - value.list_body.y) / ROW_H;
        const int visual = model_.scroll() + local;
        return visual < model_.visible_count() ? visual : -1;
    }

    int entry_at(int mx, int my) const {
        return model_.visible_entry_index(row_at(mx, my));
    }

    void register_drop_targets() {
        /* Zminimalizowane okno nie moze zostawiac niewidzialnych celow drop. */
        if (window_.minimized) {
            (void)gui_rejestruj_cele_drop(nullptr, 0);
            return;
        }
        BwsCelDrop* const targets = drop_targets_buffer;
        for (uint32_t i = 0; i < BWS_DROP_CELE_MAX; ++i)
            targets[i] = BwsCelDrop{};
        uint32_t count = 0;
        const AktowkaLayout value = layout();
        const int rows = visible_rows();
        for (int i = 0; i < rows && count + 1 < BWS_DROP_CELE_MAX; ++i) {
            const int visual = model_.scroll() + i;
            const int entry_index = model_.visible_entry_index(visual);
            const Entry* item = model_.entry(entry_index);
            if (!item || !item->directory) continue;
            BwsCelDrop& target = targets[count];
            target.x = value.list_body.x;
            target.y = value.list_body.y + i * ROW_H;
            target.szer = value.list_body.w;
            target.wys = ROW_H;
            if (!model_.entry_path(entry_index, target.folder, sizeof(target.folder)))
                continue;
            ++count;
        }
        if (value.list_body.w > 0 && value.list_body.h > 0 && count < BWS_DROP_CELE_MAX) {
            BwsCelDrop& target = targets[count++];
            target.x = value.list_body.x;
            target.y = value.list_body.y;
            target.szer = value.list_body.w;
            target.wys = value.list_body.h;
            (void)copy_text(target.folder, sizeof(target.folder), model_.path());
        }
        /* Duzy cel blokuje przebijanie dropu do pulpitu lezacego pod oknem;
         * mniejsze cele wierszy/listy nadal maja pierwszenstwo. */
        if (window_.w > 0 && window_.h > 0 && count < BWS_DROP_CELE_MAX) {
            BwsCelDrop& target = targets[count++];
            target.x = window_.x;
            target.y = window_.y;
            target.szer = window_.w;
            target.wys = window_.h;
            (void)copy_text(target.folder, sizeof(target.folder), model_.path());
        }
        (void)gui_rejestruj_cele_drop(targets, count);
    }

    void set_drop_hover(int x, int y, bool active) {
        const int old_entry = drop_target_entry_;
        const bool old_body = drop_target_body_;
        drop_target_entry_ = -1;
        drop_target_body_ = false;
        if (active) {
            const int entry_index = entry_at(x, y);
            const Entry* item = model_.entry(entry_index);
            if (item && item->directory) drop_target_entry_ = entry_index;
            else if (hit(x, y, layout().list_body)) drop_target_body_ = true;
        }
        if (old_entry >= 0) draw_entry_row(old_entry);
        if (drop_target_entry_ >= 0 && drop_target_entry_ != old_entry)
            draw_entry_row(drop_target_entry_);
        if (old_body != drop_target_body_) draw_drop_body_border();
    }

    void draw_full(bool clear_desktop) {
        if (window_.minimized) return;
        if (clear_desktop) gui_odswiez_pulpit();
        gui_rysuj_okno(window_.x, window_.y, window_.w, window_.h, "Eksplorator Plików");
        gui_rysuj_standardowa_belke(window_.x, window_.y, window_.w,
                                    "Eksplorator Plików", window_.maximized);
        draw_workspace();
        gui_odswiez();
    }

    void draw_workspace() {
        const AktowkaLayout value = layout();
        const int client_h = window_.h - TITLE_H - 3;
        if (client_h > 0)
            gui_rysuj_prostokat(window_.x + 2, window_.y + TITLE_H,
                                window_.w > 4 ? window_.w - 4 : 0,
                                client_h, BG);
        draw_toolbar();
        draw_left_panel();
        draw_center_panel();
        draw_details_panel();
        gui_rysuj_prostokat(value.left_divider.x, value.left_divider.y,
                            value.left_divider.w, value.left_divider.h, BORDER_DARK);
        gui_rysuj_prostokat(value.right_divider.x, value.right_divider.y,
                            value.right_divider.w, value.right_divider.h, BORDER_DARK);
        draw_status();
        register_drop_targets();
    }

    void draw_toolbar() {
        const AktowkaLayout value = layout();
        gui_rysuj_prostokat(value.toolbar.x, value.toolbar.y,
                            value.toolbar.w, value.toolbar.h, PANEL_ALT);
        draw_button(value.back, model_.can_back() ? BORDER : DISABLED, "<-");
        draw_button(value.forward, model_.can_forward() ? BORDER : DISABLED, "->");
        draw_button(value.up, BORDER, "^");
        draw_button(value.refresh, BORDER, "Odśwież");
        draw_button(value.new_folder, BORDER, "Nowy folder");
        draw_button(value.new_file, BORDER, "Nowy plik");
        draw_button(value.rename, BORDER, "Zmień nazwę");
        draw_button(value.remove, BORDER,
                    edit_mode_ == EditMode::CONFIRM_DELETE ? "Potwierdź" : "Usuń");
        draw_button(value.move, BORDER, "Przenieś");
        draw_button(value.run, BORDER, "Uruchom");
        draw_path_field();
        draw_search_field();
        draw_button(value.sort, BORDER, sort_mode_name(model_.sort_mode()));
    }

    void draw_path_field() {
        const AktowkaLayout value = layout();
        const bool active = edit_mode_ == EditMode::PATH;
        draw_field(value.path, active);
        const char* prefix = "Ścieżka: ";
        draw_clipped(value.path.x + 5, value.path.y + 5,
                     value.path.w - 10, prefix, TEXT, false);
        const int prefix_width = oblicz_szerokosc_tekstu(prefix, 1);
        const int available = value.path.w - 10 - prefix_width;
        if (!active) {
            draw_clipped(value.path.x + 5 + prefix_width, value.path.y + 5,
                         available, model_.path(), TEXT, true);
            return;
        }
        while (edit_view_start_ > edit_caret_)
            edit_view_start_ = previous_codepoint(edit_, edit_view_start_);
        while (edit_view_start_ < edit_caret_) {
            char before[PATH_CAP] = {};
            size_t out = 0;
            for (size_t i = edit_view_start_; i < edit_caret_ && out + 1 < sizeof(before); ++i)
                before[out++] = edit_[i];
            if (oblicz_szerokosc_tekstu(before, 1) +
                oblicz_szerokosc_tekstu("|", 1) <= available) break;
            edit_view_start_ = next_codepoint(edit_, edit_view_start_, edit_length_);
        }
        char shown[PATH_CAP + 2] = {};
        size_t out = 0;
        for (size_t i = edit_view_start_; i < edit_length_ && out + 2 < sizeof(shown); ++i) {
            if (i == edit_caret_) shown[out++] = '|';
            shown[out++] = edit_[i];
        }
        if (edit_caret_ == edit_length_ && out + 1 < sizeof(shown)) shown[out++] = '|';
        shown[out] = '\0';
        draw_clipped(value.path.x + 5 + prefix_width, value.path.y + 5,
                     available, shown, TEXT, false);
    }

    void draw_search_field() {
        const AktowkaLayout value = layout();
        const bool active = edit_mode_ == EditMode::SEARCH ||
                            edit_mode_ == EditMode::CREATE_FOLDER ||
                            edit_mode_ == EditMode::CREATE_FILE ||
                            edit_mode_ == EditMode::RENAME ||
                            edit_mode_ == EditMode::MOVE;
        draw_field(value.search, active);
        char source[PATH_CAP] = {};
        if (edit_mode_ == EditMode::CREATE_FOLDER)
            (void)append_text(source, sizeof(source), "Folder: ");
        else if (edit_mode_ == EditMode::CREATE_FILE)
            (void)append_text(source, sizeof(source), "Plik: ");
        else if (edit_mode_ == EditMode::RENAME)
            (void)append_text(source, sizeof(source), "Nazwa: ");
        else if (edit_mode_ == EditMode::MOVE)
            (void)append_text(source, sizeof(source), "Do folderu: ");
        else
            (void)append_text(source, sizeof(source), "Szukaj: ");
        if (active) {
            (void)append_text(source, sizeof(source), edit_);
            (void)append_text(source, sizeof(source), "|");
        } else {
            (void)append_text(source, sizeof(source), model_.filter());
        }
        draw_clipped(value.search.x + 5, value.search.y + 5,
                     value.search.w - 10, source, TEXT, false);
    }

    void draw_left_panel() {
        const AktowkaLayout value = layout();
        gui_rysuj_prostokat(value.left.x, value.left.y, value.left.w, value.left.h, PANEL);
        draw_panel_title(value.left, "Szybki dostęp");
        int y = value.left.y + PANEL_TITLE_H + 4;
        bool pinned_header = false;
        for (int i = 0; i < model_.quick_count(); ++i) {
            const QuickItem* item = model_.quick(i);
            if (!item) continue;
            if (item->pinned && !pinned_header) {
                draw_clipped(value.left.x + 7, y + 4, value.left.w - 14,
                             "Przypięte foldery", MUTED, false);
                y += 23;
                pinned_header = true;
            }
            if (y + ROW_H > value.left.y + value.left.h) break;
            const bool current = equal_text(item->path, model_.path());
            gui_rysuj_prostokat(value.left.x + 2, y,
                                value.left.w > 4 ? value.left.w - 4 : 0,
                                ROW_H, current ? SELECT : PANEL);
            gui_rysuj_prostokat(value.left.x + 7, y + 7, 10, 10, FOLDER);
            draw_clipped(value.left.x + 23, y + 5, value.left.w - 29,
                         item->label, current ? TEXT : TEXT2, false);
            y += ROW_H;
        }
    }

    void draw_center_panel() {
        const AktowkaLayout value = layout();
        gui_rysuj_prostokat(value.center.x, value.center.y,
                            value.center.w, value.center.h, BG);
        draw_panel_title(value.center, "Zawartość folderu");
        draw_table_header();
        draw_list_only();
    }

    void draw_table_header() {
        const AktowkaLayout value = layout();
        gui_rysuj_prostokat(value.table_header.x, value.table_header.y,
                            value.table_header.w, value.table_header.h, PANEL_ALT);
        draw_header_cell(value.col_name, "Nazwa");
        draw_header_cell(value.col_type, "Typ");
        draw_header_cell(value.col_date, model_.has_creation_dates()
            ? "Data utworzenia" : "Data (niedostępna)");
        draw_header_cell(value.col_security, "Bezpieczeństwo");
    }

    void draw_list_only() {
        const AktowkaLayout value = layout();
        gui_rysuj_prostokat(value.list_body.x, value.list_body.y,
                            value.list_body.w, value.list_body.h, BG);
        const int rows = visible_rows();
        for (int i = 0; i < rows; ++i)
            draw_visual_row(model_.scroll() + i);
        if (value.scroll_up.w > 0) {
            draw_button(value.scroll_up, BORDER, "^");
            draw_button(value.scroll_down, BORDER, "v");
            const int track_y = value.scroll_up.y + value.scroll_up.h;
            const int track_h = value.scroll_down.y - track_y;
            if (track_h > 0)
                gui_rysuj_prostokat(value.scroll_up.x, track_y,
                                    value.scroll_up.w, track_h, PANEL);
        }
        register_drop_targets();
    }

    void draw_visual_row(int visual_index) {
        const AktowkaLayout value = layout();
        const int local = visual_index - model_.scroll();
        if (local < 0 || local >= visible_rows()) return;
        const int y = value.list_body.y + local * ROW_H;
        int height = ROW_H;
        if (y + height > value.list_body.y + value.list_body.h)
            height = value.list_body.y + value.list_body.h - y;
        if (height <= 0) return;
        const int entry_index = model_.visible_entry_index(visual_index);
        const Entry* item = model_.entry(entry_index);
        gui_rysuj_prostokat(value.list_body.x, y, value.list_body.w, height,
                            entry_index >= 0 && entry_index == drop_target_entry_
                                ? DROP_TARGET
                                : (entry_index >= 0 && entry_index == model_.selected()
                                    ? SELECT : BG));
        draw_row_separators(y, height);
        if (!item) return;
        const uint32_t icon_color = color_for(*item);
        gui_rysuj_prostokat(value.col_name.x + 6, y + 6, ICON, ICON, icon_color);
        if (item->directory)
            gui_rysuj_prostokat(value.col_name.x + 8, y + 4, ICON - 5, 4, icon_color);
        draw_clipped(value.col_name.x + 25, y + 5,
                     value.col_name.w - 29, item->name,
                     entry_index == model_.selected() ? TEXT : TEXT2, false);
        draw_clipped(value.col_type.x + 5, y + 5, value.col_type.w - 10,
                     entry_type_name(item->type), TEXT2, false);
        char creation[48] = {};
        if (!item->creation_available ||
            !format_rtc_creation(item->creation_time, creation, sizeof(creation)))
            (void)copy_text(creation, sizeof(creation), "Niedostępna");
        draw_clipped(value.col_date.x + 5, y + 5, value.col_date.w - 10,
                     creation, item->creation_available ? TEXT2 : MUTED, false);
        char security[16] = {};
        if (item->pzb_available) {
            (void)append_text(security, sizeof(security), "PZB ");
            (void)append_unsigned(security, sizeof(security),
                                  static_cast<uint64_t>(item->pzb));
        } else {
            (void)copy_text(security, sizeof(security), "Brak");
        }
        draw_clipped(value.col_security.x + 5, y + 5,
                     value.col_security.w - 10, security, MUTED, false);
    }

    void draw_entry_row(int entry_index) {
        const int visual = model_.visible_position(entry_index);
        if (visual >= model_.scroll() && visual < model_.scroll() + visible_rows())
            draw_visual_row(visual);
    }

    void draw_selected_row() { draw_entry_row(model_.selected()); }

    void draw_details_panel() {
        const AktowkaLayout value = layout();
        gui_rysuj_prostokat(value.right.x, value.right.y,
                            value.right.w, value.right.h, PANEL);
        draw_panel_title(value.right, "Podgląd elementu / Szczegóły");
        gui_rysuj_prostokat(value.details_body.x, value.details_body.y,
                            value.details_body.w, value.details_body.h, PANEL);
        const Entry* item = model_.entry(model_.selected());
        const Details& details = model_.details();
        if (!item || !details.valid) {
            draw_clipped(value.details_body.x + 8, value.details_body.y + 9,
                         value.details_body.w - 16,
                         "Zaznacz element, aby zobaczyć szczegóły.", MUTED, false);
            return;
        }
        int y = value.details_body.y + 8;
        detail_line("Nazwa", item->name, y);
        y += 36;
        detail_line("Typ", entry_type_name(item->type), y);
        y += 36;
        detail_line("Pełna ścieżka", details.full_path, y);
        y += 36;
        char creation[48] = {};
        if (!details.creation_available ||
            !format_rtc_creation(details.creation_time, creation, sizeof(creation)))
            (void)copy_text(creation, sizeof(creation), "Niedostępna (stary BSP2/RTC)");
        detail_line("Data utworzenia", creation, y);
        y += 36;
        char size[40] = {};
        if (details.size_available) {
            (void)append_unsigned(size, sizeof(size), details.size);
            (void)append_text(size, sizeof(size), " B");
        } else {
            (void)copy_text(size, sizeof(size), item->directory
                ? "Nie dotyczy folderu" : "Niedostępny");
        }
        detail_line("Rozmiar", size, y);
        y += 36;
        char pzb[48] = {};
        if (details.pzb_available) {
            (void)append_text(pzb, sizeof(pzb), "Poziom ");
            (void)append_unsigned(pzb, sizeof(pzb),
                                  static_cast<uint64_t>(details.pzb));
        } else {
            (void)copy_text(pzb, sizeof(pzb), "Niedostępny (stary BSP2)");
        }
        detail_line("Bezpieczeństwo / PZB", pzb, y);
        y += 36;
        if (item->type == EntryType::CEBULA) {
            detail_line("Nazwa aplikacji",
                        details.application[0] ? details.application : "Brak w manifeście", y);
            y += 36;
            detail_line("plik_startowy",
                        details.entry_point[0] ? details.entry_point : "Brak w manifeście", y);
            y += 39;
            const Rect run_rect = details_run_rect();
            draw_button(run_rect, BORDER, "Uruchom");
        }
    }

    Rect details_run_rect() const {
        const AktowkaLayout value = layout();
        const int width = value.details_body.w > 20 ? value.details_body.w - 16 : 0;
        return {value.details_body.x + 8,
                value.details_body.y + value.details_body.h - TOOL_H - 8,
                width, TOOL_H};
    }

    void draw_status() {
        const AktowkaLayout value = layout();
        gui_rysuj_prostokat(value.status.x, value.status.y,
                            value.status.w, value.status.h, PANEL_ALT);
        gui_rysuj_prostokat(value.status.x, value.status.y,
                            value.status.w, value.status.h > 0 ? 1 : 0, BORDER);
        draw_clipped(value.status.x + 6, value.status.y + 5,
                     value.status.w - 12, model_.status(), TEXT, false);
    }

private:
    AktowkaModel& model_;
    AktowkaWindow& window_;
    EditMode edit_mode_;
    char edit_[PATH_CAP];
    size_t edit_length_;
    size_t edit_caret_;
    size_t edit_view_start_;
    int drop_target_entry_;
    bool drop_target_body_;

    void draw_drop_body_border() {
        const AktowkaLayout value = layout();
        if (value.list_body.w <= 1 || value.list_body.h <= 1) return;
        const uint32_t color = drop_target_body_ ? DROP_TARGET : BORDER_DARK;
        gui_rysuj_prostokat(value.list_body.x, value.list_body.y,
                            value.list_body.w, 1, color);
        gui_rysuj_prostokat(value.list_body.x, value.list_body.y + value.list_body.h - 1,
                            value.list_body.w, 1, color);
    }

    void draw_button(const Rect& rect, uint32_t border, const char* label) {
        if (rect.w <= 0 || rect.h <= 0) return;
        RysujPrzycisk(rect.x, rect.y, rect.w, rect.h, border, BG, label);
    }

    void draw_field(const Rect& rect, bool active) {
        if (rect.w <= 0 || rect.h <= 0) return;
        gui_rysuj_prostokat(rect.x, rect.y, rect.w, rect.h, active ? SELECT : PANEL);
        gui_rysuj_prostokat(rect.x, rect.y, rect.w, 1, BORDER);
        gui_rysuj_prostokat(rect.x, rect.y + rect.h - 1, rect.w, 1, BORDER);
        gui_rysuj_prostokat(rect.x, rect.y, 1, rect.h, BORDER_DARK);
        gui_rysuj_prostokat(rect.x + rect.w - 1, rect.y, 1, rect.h, BORDER_DARK);
    }

    void draw_clipped(int x, int y, int width, const char* text,
                      uint32_t color, bool keep_tail) {
        if (width <= 0 || !text) return;
        char shown[SHOW_CAP] = {};
        clip_utf8_px(text, shown, sizeof(shown), width, keep_tail);
        gui_wypisz_tekst_kolor(x, y, color, shown);
    }

    void draw_panel_title(const Rect& panel, const char* title) {
        if (panel.w <= 0 || panel.h <= 0) return;
        const int height = panel.h < PANEL_TITLE_H ? panel.h : PANEL_TITLE_H;
        gui_rysuj_prostokat(panel.x, panel.y, panel.w, height, PANEL_ALT);
        gui_rysuj_prostokat(panel.x, panel.y + height - 1,
                            panel.w, height > 0 ? 1 : 0, BORDER_DARK);
        draw_clipped(panel.x + 7, panel.y + 6, panel.w - 14, title, TEXT, false);
    }

    void draw_header_cell(const Rect& rect, const char* text) {
        if (rect.w <= 0 || rect.h <= 0) return;
        gui_rysuj_prostokat(rect.x + rect.w - 1, rect.y,
                            1, rect.h, BORDER_DARK);
        draw_clipped(rect.x + 5, rect.y + 6, rect.w - 10, text, TEXT, false);
    }

    void draw_row_separators(int y, int height) {
        const AktowkaLayout value = layout();
        const int positions[3] = {
            value.col_type.x, value.col_date.x, value.col_security.x
        };
        for (int i = 0; i < 3; ++i)
            gui_rysuj_prostokat(positions[i], y, 1, height, BORDER_DARK);
        gui_rysuj_prostokat(value.list_body.x, y + height - 1,
                            value.list_body.w, 1, PANEL_ALT);
    }

    uint32_t color_for(const Entry& item) const {
        if (item.type == EntryType::PARENT || item.type == EntryType::FOLDER)
            return FOLDER;
        if (item.type == EntryType::BUR) return BUR;
        if (item.type == EntryType::CEBULA) return CEBULA;
        if (item.type == EntryType::TXT) return TXT;
        return FILE_COLOR;
    }

    void detail_line(const char* label, const char* value, int y) {
        const AktowkaLayout layout_value = layout();
        if (y + 30 > layout_value.details_body.y + layout_value.details_body.h) return;
        draw_clipped(layout_value.details_body.x + 8, y,
                     layout_value.details_body.w - 16, label, MUTED, false);
        draw_clipped(layout_value.details_body.x + 8, y + 16,
                     layout_value.details_body.w - 16, value, TEXT2, true);
    }
};

enum class AnsiState : uint8_t { NONE, ESC, CSI, CSI_DELETE };

bool is_double_click(uint64_t previous, uint64_t current) {
    if (!previous || current < previous) return false;
    const uint64_t difference = current - previous;
    return current >= UINT64_C(1000000000)
        ? difference <= UINT64_C(500000000)
        : difference <= 50;
}

void present_workspace_change(AktowkaView& view, Change change) {
    if (change == Change::CHANGED) view.draw_workspace();
    else if (change == Change::DETAILS) {
        view.draw_selected_row();
        view.draw_details_panel();
    }
    else if (change == Change::ERROR) {
        view.draw_toolbar();
        view.draw_status();
    }
    view.register_drop_targets();
    if (change != Change::UNCHANGED) gui_odswiez();
}

void present_activation(AktowkaView& view, Activation activation) {
    if (activation == Activation::NAVIGATION) view.draw_workspace();
    else view.draw_status();
    view.register_drop_targets();
    gui_odswiez();
}

void redraw_selection(AktowkaView& view, AktowkaModel& model,
                      int old_entry, bool scroll_changed) {
    if (scroll_changed) {
        view.draw_list_only();
    } else {
        if (old_entry >= 0) view.draw_entry_row(old_entry);
        if (model.selected() >= 0) view.draw_entry_row(model.selected());
    }
    view.draw_details_panel();
    view.register_drop_targets();
    gui_odswiez();
}

void redraw_filter(AktowkaView& view) {
    view.draw_search_field();
    view.draw_list_only();
    view.draw_details_panel();
    view.draw_status();
    view.register_drop_targets();
    gui_odswiez();
}

void redraw_sort(AktowkaView& view) {
    view.draw_toolbar();
    view.draw_table_header();
    view.draw_list_only();
    view.draw_status();
    view.register_drop_targets();
    gui_odswiez();
}

} // namespace

#ifndef AKTOWKA_HOST_TEST
extern "C" __attribute__((noreturn)) void _start() {
    AktowkaModel model;
    AktowkaWindow window;
    gui_pobierz_rozdzielczosc(&window.screen_w, &window.screen_h);
    window.clamp();
    if (!window.create_layer()) gui_zakoncz_aplikacje();
    gui_ustaw_przejecie_myszy(true);
    (void)model.initialize();
    char startup_path[PATH_CAP] = {};
    if (pobierz_argument_startowy(startup_path, sizeof(startup_path)) &&
        startup_path[0] == '/')
        (void)model.navigate(startup_path);
    AktowkaView view(model, window);
    view.draw_full(true);

    bool quit = false;
    bool dragging = false;
    int drag_x = 0;
    int drag_y = 0;
    int last_entry = -1;
    int refresh_ticks = 0;
    uint64_t last_click_time = 0;
    AnsiState ansi = AnsiState::NONE;
    bool item_drag_candidate = false;
    bool item_dragging = false;
    int item_drag_start_x = 0;
    int item_drag_start_y = 0;
    char item_drag_path[PATH_CAP] = {};

    while (!quit) {
        bws_zdarzenie event{};
        if (!gui_czekaj_na_zdarzenie(&event)) continue;
        if (event.typ == BWS_ZDARZENIE_ZAMKNIJ) {
            quit = true;
            continue;
        }
        if (event.typ == BWS_ZDARZENIE_OTWORZ_PLIK) {
            char requested[PATH_CAP] = {};
            if (pobierz_argument_startowy(requested, sizeof(requested)) &&
                requested[0] == '/')
                present_workspace_change(view, model.navigate(requested));
            continue;
        }
        if (event.typ == BWS_ZDARZENIE_FOCUS) {
            const bool restored = window.minimized;
            window.minimized = false;
            refresh_ticks = 0;
            const Change change = model.refresh();
            if (restored) view.draw_full(false);
            else present_workspace_change(view, change);
            continue;
        }
        if (event.typ == BWS_ZDARZENIE_DRAG_HOVER) {
            view.set_drop_hover(event.x, event.y, true);
            gui_odswiez();
            continue;
        }
        if (event.typ == BWS_ZDARZENIE_DRAG_LEAVE) {
            view.set_drop_hover(event.x, event.y, false);
            gui_odswiez();
            continue;
        }
        if (event.typ == BWS_ZDARZENIE_DRAG_DROP) {
            view.set_drop_hover(event.x, event.y, false);
            present_workspace_change(view, model.refresh());
            continue;
        }
        if (event.typ == BWS_ZDARZENIE_TIMER) {
            if (view.edit_mode() != EditMode::NONE && ansi == AnsiState::ESC) {
                ansi = AnsiState::NONE;
                const EditMode cancelled = view.edit_mode();
                view.end_edit();
                if (cancelled == EditMode::PATH) view.draw_path_field();
                else if (cancelled == EditMode::CONFIRM_DELETE) view.draw_toolbar();
                else view.draw_search_field();
                gui_odswiez();
            }
            if (!window.minimized && ++refresh_ticks >= REFRESH_TICKS) {
                refresh_ticks = 0;
                present_workspace_change(view, model.refresh());
            }
            view.register_drop_targets();
            continue;
        }
        if (window.minimized) continue;

        const int mx = event.x;
        const int my = event.y;
        const bool left_button = (event.przyciski & 1U) != 0;
        if (event.typ == BWS_ZDARZENIE_MYSZ_RUCH && dragging && left_button) {
            window.x = mx - drag_x;
            window.y = my - drag_y;
            window.clamp();
            bws_przesun_warstwe(window.x, window.y);
            continue;
        }
        if (event.typ == BWS_ZDARZENIE_MYSZ_RUCH &&
            item_drag_candidate && left_button) {
            const int dx = mx - item_drag_start_x;
            const int dy = my - item_drag_start_y;
            if (!item_dragging && (dx > 4 || dx < -4 || dy > 4 || dy < -4))
                item_dragging = true;
            if (item_dragging)
                (void)gui_aktualizuj_drag(item_drag_path, mx, my, false);
            continue;
        }
        if (event.typ == BWS_ZDARZENIE_MYSZ_UP && item_drag_candidate) {
            if (item_dragging) {
                const BwsWynikDrop result =
                    gui_aktualizuj_drag(item_drag_path, mx, my, true);
                if (result == BWS_DROP_PRZENIESIONO) {
                    model.set_status("Element przeniesiono metodą drag & drop.");
                    present_workspace_change(view, model.refresh());
                } else {
                    model.set_status(result == BWS_DROP_BRAK_CELU
                        ? "Drop anulowany: poza poprawnym folderem docelowym."
                        : "Drop nie zmienił danych: błąd celu, konflikt albo PZB.");
                    view.draw_status();
                    gui_odswiez();
                }
            }
            item_drag_candidate = false;
            item_dragging = false;
            item_drag_path[0] = '\0';
            gui_ustaw_capture_myszy(false);
            continue;
        }
        if (event.typ == BWS_ZDARZENIE_MYSZ_UP && dragging) {
            dragging = false;
            gui_ustaw_capture_myszy(false);
            view.register_drop_targets();
            continue;
        }

        if (event.typ == BWS_ZDARZENIE_MYSZ_DOWN) {
            item_drag_candidate = false;
            item_dragging = false;
            const gui_akcja_belki title_action = gui_hit_test_belki(
                mx, my, window.x, window.y, window.w);
            if (title_action == GUI_BELKA_ZAMKNIJ) {
                quit = true;
                continue;
            }
            if (title_action == GUI_BELKA_MINIMALIZUJ) {
                window.minimized = gui_minimalizuj_okno();
                dragging = false;
                gui_ustaw_capture_myszy(false);
                continue;
            }
            if (title_action == GUI_BELKA_MAKSYMALIZUJ) {
                if (!window.toggle_maximize())
                    model.set_status("Błąd: nie można zmienić rozmiaru okna.");
                model.clamp_scroll(view.visible_rows());
                view.draw_full(true);
                continue;
            }
            if (title_action == GUI_BELKA_DRAG && !window.maximized) {
                dragging = true;
                drag_x = mx - window.x;
                drag_y = my - window.y;
                gui_ustaw_capture_myszy(true);
                continue;
            }

            const AktowkaLayout layout = view.layout();
            if (hit(mx, my, layout.path)) {
                if (view.edit_mode() != EditMode::PATH) {
                    view.begin_edit(EditMode::PATH);
                    view.draw_path_field();
                }
                view.place_path_caret(mx);
                view.draw_path_field();
                gui_odswiez();
                continue;
            }
            if (hit(mx, my, layout.search)) {
                view.begin_edit(EditMode::SEARCH);
                view.draw_search_field();
                gui_odswiez();
                continue;
            }
            if (hit(mx, my, layout.back)) {
                view.end_edit();
                present_workspace_change(view, model.back());
                continue;
            }
            if (hit(mx, my, layout.forward)) {
                view.end_edit();
                present_workspace_change(view, model.forward());
                continue;
            }
            if (hit(mx, my, layout.up)) {
                view.end_edit();
                present_workspace_change(view, model.parent());
                continue;
            }
            if (hit(mx, my, layout.refresh)) {
                present_workspace_change(view, model.refresh());
                continue;
            }
            if (hit(mx, my, layout.new_folder)) {
                view.begin_edit(EditMode::CREATE_FOLDER);
                model.set_status("Wpisz nazwę folderu i naciśnij Enter.");
                view.draw_search_field();
                view.draw_status();
                gui_odswiez();
                continue;
            }
            if (hit(mx, my, layout.new_file)) {
                view.begin_edit(EditMode::CREATE_FILE);
                model.set_status("Wpisz nazwę pliku i naciśnij Enter.");
                view.draw_search_field();
                view.draw_status();
                gui_odswiez();
                continue;
            }
            if (hit(mx, my, layout.rename)) {
                view.begin_edit(EditMode::RENAME);
                model.set_status("Wpisz nową nazwę i naciśnij Enter.");
                view.draw_search_field();
                view.draw_status();
                gui_odswiez();
                continue;
            }
            if (hit(mx, my, layout.remove)) {
                if (view.edit_mode() != EditMode::CONFIRM_DELETE) {
                    view.begin_edit(EditMode::CONFIRM_DELETE);
                    model.set_status("Usuwanie jest trwałe. Kliknij Potwierdź albo naciśnij Enter.");
                    view.draw_toolbar();
                    view.draw_status();
                    gui_odswiez();
                } else {
                    const bool removed = model.delete_selected();
                    view.end_edit();
                    if (removed) present_workspace_change(view, model.refresh());
                    else {
                        view.draw_toolbar();
                        view.draw_status();
                        gui_odswiez();
                    }
                }
                continue;
            }
            if (hit(mx, my, layout.move)) {
                view.begin_edit(EditMode::MOVE);
                model.set_status("Wpisz pełną ścieżkę folderu docelowego i naciśnij Enter.");
                view.draw_search_field();
                view.draw_status();
                gui_odswiez();
                continue;
            }
            if (hit(mx, my, layout.run)) {
                model.run_selected_package();
                view.draw_status();
                gui_odswiez();
                continue;
            }
            if (hit(mx, my, layout.sort)) {
                model.cycle_sort();
                redraw_sort(view);
                continue;
            }
            if (hit(mx, my, layout.col_name)) {
                model.set_sort(model.sort_mode() == SortMode::AZ
                    ? SortMode::ZA : SortMode::AZ);
                redraw_sort(view);
                continue;
            }
            if (hit(mx, my, layout.col_type)) {
                model.set_sort(SortMode::TYPE);
                redraw_sort(view);
                continue;
            }
            if (hit(mx, my, layout.col_date)) {
                model.set_sort(SortMode::DATE);
                redraw_sort(view);
                continue;
            }
            if (hit(mx, my, view.details_run_rect())) {
                model.run_selected_package();
                view.draw_status();
                gui_odswiez();
                continue;
            }
            const int quick_index = view.quick_at(mx, my);
            if (quick_index >= 0) {
                const QuickItem* quick = model.quick(quick_index);
                if (quick) {
                    view.end_edit();
                    present_workspace_change(view, model.navigate(quick->path));
                }
                continue;
            }
            if (hit(mx, my, layout.scroll_up)) {
                if (model.scroll_by(-1, view.visible_rows())) {
                    view.draw_list_only();
                    gui_odswiez();
                }
                continue;
            }
            if (hit(mx, my, layout.scroll_down)) {
                if (model.scroll_by(1, view.visible_rows())) {
                    view.draw_list_only();
                    gui_odswiez();
                }
                continue;
            }
            if (view.edit_mode() != EditMode::NONE) {
                const EditMode old_mode = view.edit_mode();
                view.end_edit();
                if (old_mode == EditMode::PATH) view.draw_path_field();
                else if (old_mode == EditMode::CONFIRM_DELETE) view.draw_toolbar();
                else view.draw_search_field();
                gui_odswiez();
            }
            const int visual_index = view.row_at(mx, my);
            if (visual_index >= 0) {
                const int old_entry = model.selected();
                bool scroll_changed = false;
                (void)model.select_visible(visual_index, view.visible_rows(), &scroll_changed);
                redraw_selection(view, model, old_entry, scroll_changed);
                const int current_entry = model.selected();
                if (model.entry_path(current_entry, item_drag_path,
                                     sizeof(item_drag_path)) &&
                    model.entry(current_entry) &&
                    model.entry(current_entry)->type != EntryType::PARENT) {
                    item_drag_candidate = true;
                    item_drag_start_x = mx;
                    item_drag_start_y = my;
                    gui_ustaw_capture_myszy(true);
                }
                const bool double_click = current_entry == last_entry &&
                    is_double_click(last_click_time, event.timestamp);
                last_entry = current_entry;
                last_click_time = event.timestamp;
                if (double_click) {
                    present_activation(view, model.activate());
                    last_entry = -1;
                    last_click_time = 0;
                }
            } else {
                last_entry = -1;
                last_click_time = 0;
            }
            continue;
        }

        if (event.typ != BWS_ZDARZENIE_KLAWISZ) continue;
        const char character = static_cast<char>(event.kod);
        if (view.edit_mode() != EditMode::NONE) {
            const EditMode mode = view.edit_mode();
            if (ansi == AnsiState::ESC) {
                if (character == '[') {
                    ansi = AnsiState::CSI;
                    continue;
                }
                ansi = AnsiState::NONE;
                const EditMode cancelled = view.edit_mode();
                view.end_edit();
                view.draw_path_field();
                if (cancelled == EditMode::CONFIRM_DELETE) view.draw_toolbar();
                else view.draw_search_field();
                gui_odswiez();
                continue;
            }
            if (ansi == AnsiState::CSI) {
                if (character == 'D') {
                    ansi = AnsiState::NONE;
                    if (view.move_caret_left()) {
                        if (mode == EditMode::PATH) view.draw_path_field();
                        else view.draw_search_field();
                        gui_odswiez();
                    }
                } else if (character == 'C') {
                    ansi = AnsiState::NONE;
                    if (view.move_caret_right()) {
                        if (mode == EditMode::PATH) view.draw_path_field();
                        else view.draw_search_field();
                        gui_odswiez();
                    }
                } else if (character == '3') {
                    ansi = AnsiState::CSI_DELETE;
                } else {
                    ansi = AnsiState::NONE;
                }
                continue;
            }
            if (ansi == AnsiState::CSI_DELETE) {
                ansi = AnsiState::NONE;
                if (character == '~' && view.delete_input()) {
                    if (mode == EditMode::SEARCH) {
                        (void)model.set_filter(view.edit_text());
                        redraw_filter(view);
                    } else {
                        if (mode == EditMode::PATH) view.draw_path_field();
                        else view.draw_search_field();
                        gui_odswiez();
                    }
                }
                continue;
            }
            if (character == '\x1B') {
                ansi = AnsiState::ESC;
                continue;
            }
            if (character == '\n' || character == '\r') {
                if (mode == EditMode::PATH) {
                    const Change change = model.navigate(view.edit_text());
                    if (change != Change::ERROR) view.end_edit();
                    present_workspace_change(view, change);
                } else if (mode == EditMode::SEARCH) {
                    view.end_edit();
                    view.draw_search_field();
                    gui_odswiez();
                } else if (mode == EditMode::CONFIRM_DELETE) {
                    const bool removed = model.delete_selected();
                    view.end_edit();
                    if (removed) present_workspace_change(view, model.refresh());
                    else {
                        view.draw_toolbar();
                        view.draw_status();
                        gui_odswiez();
                    }
                } else if (mode == EditMode::RENAME) {
                    char name[NAME_CAP] = {};
                    (void)copy_text(name, sizeof(name), view.edit_text());
                    const bool renamed = model.rename_selected(name);
                    view.end_edit();
                    if (renamed) present_workspace_change(view, model.refresh());
                    else {
                        view.draw_search_field();
                        view.draw_status();
                        gui_odswiez();
                    }
                } else if (mode == EditMode::MOVE) {
                    char destination[PATH_CAP] = {};
                    (void)copy_text(destination, sizeof(destination), view.edit_text());
                    const bool moved = model.move_selected(destination);
                    view.end_edit();
                    if (moved) present_workspace_change(view, model.refresh());
                    else {
                        view.draw_search_field();
                        view.draw_status();
                        gui_odswiez();
                    }
                } else {
                    char name[NAME_CAP] = {};
                    (void)copy_text(name, sizeof(name), view.edit_text());
                    const bool created = mode == EditMode::CREATE_FOLDER
                        ? model.create_folder(name) : model.create_file(name);
                    view.end_edit();
                    if (created) present_workspace_change(view, model.refresh());
                    else {
                        view.draw_search_field();
                        view.draw_status();
                        gui_odswiez();
                    }
                }
            } else if (character == '\b' || static_cast<uint8_t>(character) == 0x7FU) {
                if (view.erase_input()) {
                    if (mode == EditMode::SEARCH) {
                        (void)model.set_filter(view.edit_text());
                        redraw_filter(view);
                    } else if (mode == EditMode::PATH) {
                        view.draw_path_field();
                        gui_odswiez();
                    } else {
                        view.draw_search_field();
                        gui_odswiez();
                    }
                }
            } else if (view.append_input(character)) {
                if (mode == EditMode::SEARCH) {
                    (void)model.set_filter(view.edit_text());
                    redraw_filter(view);
                } else if (mode == EditMode::PATH) {
                    view.draw_path_field();
                    gui_odswiez();
                } else {
                    view.draw_search_field();
                    gui_odswiez();
                }
            } else {
                view.draw_status();
                gui_odswiez();
            }
            continue;
        }

        if (ansi == AnsiState::ESC) {
            ansi = character == '[' ? AnsiState::CSI : AnsiState::NONE;
            continue;
        }
        if (ansi == AnsiState::CSI) {
            ansi = AnsiState::NONE;
            const int direction = character == 'A' ? -1 : (character == 'B' ? 1 : 0);
            if (direction) {
                int old_entry = -1;
                bool scroll_changed = false;
                if (model.move_selection(direction, view.visible_rows(),
                                         &old_entry, &scroll_changed))
                    redraw_selection(view, model, old_entry, scroll_changed);
            }
            continue;
        }
        if (character == '\x1B') {
            ansi = AnsiState::ESC;
            continue;
        }
        if (character == '\n' || character == '\r') {
            present_activation(view, model.activate());
            continue;
        }
        if (character == '\b' || static_cast<uint8_t>(character) == 0x7FU) {
            present_workspace_change(view, model.back());
        }
    }

    gui_ustaw_capture_myszy(false);
    gui_zakoncz_aplikacje();
}
#endif
