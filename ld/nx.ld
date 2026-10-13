/* APP=nx: the classic layout (app.ld) plus a check of the image size against the application
 * region, and the .noinit crash record (src/nx/nx_fault.h).
 *
 * NX_FLASH_LIMIT: the build budget, 110 KB (112640 bytes, decided on 2026-09-25: the size
 * the vendor bootloader is proven to take, docs/nx/drop0-checklist.md §1; the application region
 * behind the 16 KB bootloader is 112 KB of the MCU's 128 KB). What may be flashed is a separate
 * limit: `make flash` refuses an image above FLASH_PROVEN (Makefile).
 *
 * NX_NOINIT_ADDR: RAM address of the .noinit record, which must survive a reset through the
 * vendor bootloader. Set from docs/nx/drop0-checklist.md results (§1 "RAM kept" of the sizeprobe
 * areas 0x20002000 / 0x20004000 / 0x20006000 / 0x20007000) with `make APP=nx NX_NOINIT_ADDR=...`.
 * .data/.bss must end below it and a 3 KB stack must fit above it. */
INCLUDE ld/app.ld

NX_FLASH_LIMIT = DEFINED(NX_FLASH_LIMIT) ? NX_FLASH_LIMIT : 112640;
NX_NOINIT_ADDR = DEFINED(NX_NOINIT_ADDR) ? NX_NOINIT_ADDR : 0x20007000;
NX_NOINIT_SIZE = 0x100;
NX_MIN_STACK = 3K;

SECTIONS
{
    .noinit NX_NOINIT_ADDR (NOLOAD) :
    {
        KEEP(*(.noinit .noinit.*))
    }
}

ASSERT(LOADADDR(.data) + SIZEOF(.data) - ORIGIN(FLASH) <= NX_FLASH_LIMIT,
       "nx image is larger than NX_FLASH_LIMIT (the image size the vendor bootloader is known to accept)")
ASSERT(NX_NOINIT_ADDR % 4 == 0 && NX_NOINIT_ADDR >= ORIGIN(RAM), "NX_NOINIT_ADDR must be a word address in RAM")
ASSERT(SIZEOF(.noinit) <= NX_NOINIT_SIZE, ".noinit is larger than NX_NOINIT_SIZE")
ASSERT(_ebss <= NX_NOINIT_ADDR, "nx .data/.bss reach NX_NOINIT_ADDR (move the crash record up or shrink RAM use)")
ASSERT(NX_NOINIT_ADDR + NX_NOINIT_SIZE + NX_MIN_STACK <= _estack, "no room for a 3 KB stack above the .noinit record")
