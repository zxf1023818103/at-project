/* ROM symbol table for the AmebaD A-cut (RTL8721D) KM4 mask-ROM swlib string
 * library: the str* / mem* / Diag*printf* / rand* / *scanf / char-conv ROM
 * functions declared by component/amebad/string_rom/*.h. Partitioned out of the
 * KM4 ROM symbol table so they live with the string_rom component (the
 * ConfigDebug* debug-config family stays in the core table). Cat'd via
 * AMEBAD_STRING_ROM_KM4_LD. PROVIDE so unreferenced entries cost nothing.
 */
PROVIDE(rand_first = 0x1000001c);
PROVIDE(rand_seed = 0x10000020);
PROVIDE(DiagVSprintf = 0x1010a12d);
PROVIDE(DiagPrintfD = 0x1010a445);
PROVIDE(DiagSPrintf = 0x1010a471);
PROVIDE(Rand = 0x1010a765);
PROVIDE(_char2num = 0x10110b2d);
PROVIDE(_2char2dec = 0x10110b5d);
PROVIDE(_2char2hex = 0x10110bd1);
PROVIDE(_memchr = 0x10110c39);
PROVIDE(_memcmp = 0x10110cc9);
PROVIDE(_memcpy = 0x10110d2d);
PROVIDE(_memmove = 0x10110dd9);
PROVIDE(_memset = 0x10110ea1);
PROVIDE(_vsscanf = 0x10110ffd);
PROVIDE(_sscanf = 0x101115c1);
PROVIDE(_stratoi = 0x101115e1);
PROVIDE(_strcat = 0x10111635);
PROVIDE(_strchr = 0x10111675);
PROVIDE(_strcmp = 0x10111745);
PROVIDE(_strcpy = 0x101117b9);
PROVIDE(_stricmp = 0x10111805);
PROVIDE(_strlen = 0x10111839);
PROVIDE(_strncat = 0x1011189d);
PROVIDE(_strncmp = 0x101118f9);
PROVIDE(_strncpy = 0x1011199d);
PROVIDE(_strnlen = 0x10111a05);
PROVIDE(_strpbrk = 0x10111a39);
PROVIDE(_strsep = 0x10111a65);
PROVIDE(_strstr = 0x10111d25);
PROVIDE(_strtoull = 0x10111f3d);
PROVIDE(_strtoll = 0x10111ffd);
PROVIDE(_strtok = 0x1011201d);
PROVIDE(_strtol = 0x101121b1);
PROVIDE(_strtoul = 0x101122e9);
PROVIDE(_strupr = 0x101122ed);
PROVIDE(_strtok_r = 0x1011208d);
PROVIDE(_strtol_r = 0x10112095);
PROVIDE(_strtoul_r = 0x101121b5);
PROVIDE(__strtok_r = 0x10112029);
