unit BursztynAPI;

interface

{ STREAMING_CHUNK: Deklaracje publicznych funkcji i procedur }
// Zwraca QWord (odpowiednik uint64_t w C)
function BwsSyscall(Nr: QWord; Arg1: QWord = 0; Arg2: QWord = 0; Arg3: QWord = 0; Arg4: QWord = 0): QWord;

// Wygodne wrappery na Twoje systemowe BWS-y
procedure Wypisz(Tekst: PChar);
procedure RysujOkno(X, Y, Szer, Wys: Integer; Tytul: PChar);
procedure Zakoncz;

implementation

{ STREAMING_CHUNK: Implementacja bramy wywolan (Inline Assembly) }
// Free Pascal dla x86_64 domyślnie korzysta z konwencji System V (jak GCC).
// Parametry wchodzą przez: RDI (Nr), RSI (Arg1), RDX (Arg2), RCX (Arg3), R8 (Arg4)
// Twoja brama "brama_wywolan_systemowych" oczekuje: R8 (Nr), R9 (Arg1), R10 (Arg2), R12 (Arg3), R13 (Arg4)
function BwsSyscall(Nr: QWord; Arg1: QWord; Arg2: QWord; Arg3: QWord; Arg4: QWord): QWord; assembler; nostackframe;
asm
mov r13, r8  // Arg4
mov r12, rcx // Arg3
mov r10, rdx // Arg2
mov r9,  rsi // Arg1
mov r8,  rdi // Numer Funkcji (SYS_NUM)

// Przeskok do Jądra Bursztyn OS (Ring 0)
syscall

// Wynik działania BWS wraca w rejestrze RAX.
// Zgodnie z x86_64 ABI, RAX to jednocześnie rejestr zwracający wartość z funkcji!


end;

{ STREAMING_CHUNK: Ciala wrapperow upraszczajacych korzystanie z API }
procedure Wypisz(Tekst: PChar);
begin
BwsSyscall(1, QWord(Tekst));
end;

procedure RysujOkno(X, Y, Szer, Wys: Integer; Tytul: PChar);
var
Wspolrzedne: QWord;
Wymiary: QWord;
begin
// W BWS 14 pakujesz dwa 32-bitowe inty do jednego 64-bitowego argumentu.
// Używamy przesunięć bitowych (shl - shift left), aby to odtworzyć w Pascalu.
Wspolrzedne := (QWord(X) shl 32) or (QWord(Y) and $FFFFFFFF);
Wymiary := (QWord(Szer) shl 32) or (QWord(Wys) and $FFFFFFFF);
BwsSyscall(14, Wspolrzedne, Wymiary, QWord(Tytul));
end;

procedure Zakoncz;
begin
BwsSyscall(32); // SYS_EXIT
end;

end.