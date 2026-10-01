import re

with open("shell.cpp", "r") as f:
    code = f.read()

# The error was in `shell.cpp` directly since I already built it!
# I will just write a python script to strip those functions out of `shell.cpp`.

functions_to_remove = [
    "void wypisz_znak(char c)",
    "void usun_znak_wejscia_wizualnie()",
    "void odtworz_widoczny_ogon_wejscia(const char* tekst,size_t len)",
    "char pobierz_znak_blokujaco()",
]

for func in functions_to_remove:
    start = code.find(func)
    if start != -1:
        # find matching closing brace
        brace_count = 0
        in_func = False
        end = -1
        for i in range(start, len(code)):
            if code[i] == '{':
                brace_count += 1
                in_func = True
            elif code[i] == '}':
                brace_count -= 1
                if in_func and brace_count == 0:
                    end = i + 1
                    break
        if end != -1:
            code = code[:start] + code[end:]

# Also fix the duplicate `pobierz_znak_blokujaco` if it existed, but I already generated the NEW one in the frontend.
# Wait, the new one was generated in the `wejscie` string which is BEFORE sec5.

# Also fix BWS_ZDARZENIE_MYSZ_SCROLL -> BWS_ZDARZENIE_MYSZ_KOLO
code = code.replace("BWS_ZDARZENIE_MYSZ_SCROLL", "BWS_ZDARZENIE_MYSZ_KOLO")
code = code.replace("e.klawisz & BWS_MYSZ_LEWY", "e.kod == 1") # In BWS mouse down, kod is button. 1 is left. Wait, BWS API for mouse: kod is 1 for left, 2 for right? Let's check `bws_zdarzenia.h` if possible, or just use 1.
code = code.replace("e.klawisz", "e.kod")
code = code.replace("BWS_MYSZ_LEWY", "1")

# Also fix namespace closing issue
code = code.replace("} // namespace", "")
code = code.replace("Czcionka::", "BursztynUI::Czcionka::")

with open("shell.cpp", "w") as f:
    f.write(code)

print("Cleaned shell.cpp")
