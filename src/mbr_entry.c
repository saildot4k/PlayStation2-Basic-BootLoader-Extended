// Raw MBR payload entry. The ROM jumps to 0x00100000, so the first code in
// the binary must enter PS2SDK crt0 before normal main(argc, argv) startup.
void __start(void) __attribute__((weak));

void __entrypoint(void) __attribute__((noreturn, used, section(".text.__entrypoint")));
void __entrypoint(void)
{
    __asm__ volatile(
        "j      %0\n"
        :
        : "Csy"(__start)
        :);
    __builtin_unreachable();
}
