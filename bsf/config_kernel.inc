// Fixed-size settings transport only. No font bytes or TTF tables in Ring 0.
#include "config.h"
static uint64_t bsf_config_request(proces_t *process, uint64_t command,
                                   uint64_t pointer, uint64_t size) {
  static BSF::Config active;
  static bool initialized = false;
  if (!process || size != sizeof(BSF::Config) || !pointer)
    return 0;
  if (!initialized) {
    initialized = true;
    BSF::Config stored{};
    active.checksum = BSF::checksum(active);
    if (rozmiar_pliku("/system/bsf.cfg") == sizeof(stored) &&
        czytaj_z_pliku("/system/bsf.cfg", reinterpret_cast<char *>(&stored),
                       sizeof(stored)) &&
        BSF::valid(stored)) {
      active = stored;
      active.generation = 1;
    }
  }
  if (command == 0)
    return skopiuj_do_przestrzeni_uzytkownika(reinterpret_cast<void *>(pointer),
                                              &active, sizeof(active))
               ? 1
               : 0;
  if (command != 1 || !(process->uprawnienia & PRAWO_GUI))
    return 0;
  const char allowed[] = "/programy/ustawienia.cebula/ustawienia.bur";
  unsigned i = 0;
  while (allowed[i] && allowed[i] == process->sciezka_pliku[i])
    ++i;
  if (allowed[i] || process->sciezka_pliku[i])
    return 0;
  BSF::Config next{};
  if (!skopiuj_z_przestrzeni_uzytkownika(
          &next, reinterpret_cast<const void *>(pointer), sizeof(next)) ||
      !BSF::valid(next))
    return 0;
  next.generation = active.generation + 1;
  if (!next.generation)
    next.generation = 1;
  next.checksum = BSF::checksum(next);
  if (!zapewnij_katalog("/system"))
    return 0;
  (void)utworz_plik("/system/bsf.cfg");
  if (!zapisz_do_pliku("/system/bsf.cfg", reinterpret_cast<const char *>(&next),
                       sizeof(next)) ||
      !psf_flush_cache())
    return 0;
  active = next;
  motyw_rozglos_zmiane();
  return 1;
}
