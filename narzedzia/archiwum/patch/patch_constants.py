import re

with open("shell.cpp", "r") as f:
    code = f.read()

constants = """
constexpr size_t MAX_SCIEZKA = 512;
constexpr size_t MAX_NAZWA_HOSTA = 254;
constexpr size_t MAX_SCIEZKA_HTTP = 2048;
constexpr size_t HISTORIA_MAX = 64;
constexpr uint32_t BUF_CZYTAJ_PLIK = 4096;
constexpr uint32_t BUF_LISTA = 1024;
constexpr uint32_t BUF_PCI = 2048;
constexpr uint32_t HTTP_BUF_SIZE = 32U * 1024U;
alignas(16) char bufor_http[HTTP_BUF_SIZE] = {};
size_t historia_ilosc = 0;
"""

idx = code.find("using namespace BursztynUI;")
if idx != -1:
    code = code[:idx+27] + "\n" + constants + code[idx+27:]

with open("shell.cpp", "w") as f:
    f.write(code)

print("Patched constants.")
