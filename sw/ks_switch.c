#include <proto/exec.h>

#include <stdio.h>
#include <stdlib.h>

static void do_magic(__reg("d0") uint32_t data) =
"   movem.l a0-a3,-(a7)\n"
"   lea.l   $f80104,a0\n"
"   lea.l   $f80894,a1\n"
"   lea.l   $fff4fe,a2\n"
"   lea.l   $f80000,a3\n"
"   add.l   d0,a3\n"
"   move    (a0),d1\n"
"   move    (a1),d1\n"
"   move    (a2),d1\n"
"   move    (a3),d1\n"
"   movem.l (a7)+,a0-a3\n";

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: ks_switch <flash-slot>\n");
        return 0;
    }

    uint32_t slot = atoi(argv[1]);
    if (slot < 1 || slot > 7)
    {
        printf("Error: Slot must be between 1 and 7\n");
        return 0;
    }

    printf("Reset Amiga to boot to new kickstart\n");

    volatile uint8_t *ciaa_pra = (volatile uint8_t *)0xbfe001;

    Disable();

    for (int i = 0; i < 100; i++)
    {
        (void)*ciaa_pra;
    }

    do_magic(slot + slot);

    while (1)
    {
        (void)*ciaa_pra;
    }

    // This will never be reached.
    Enable();

    return 0;
}
