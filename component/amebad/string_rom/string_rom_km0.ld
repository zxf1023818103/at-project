/* ROM symbol table for the AmebaD A-cut (RTL8721D) KM0 mask-ROM swlib string
 * library: the str* / mem* / Diag*printf* / rand* / *scanf / char-conv ROM
 * functions declared by component/amebad/string_rom/*.h. Partitioned out of the
 * KM0 ROM symbol table so they live with the string_rom component (the
 * ConfigDebug* debug-config family stays in the core table). Cat'd via
 * AMEBAD_STRING_ROM_KM0_LD. PROVIDE so unreferenced entries cost nothing.
 */
PROVIDE(DiagVSprintf = 0x115);
PROVIDE(DiagPrintfD = 0x4b9);
PROVIDE(DiagSPrintf = 0x4dd);
PROVIDE(Rand = 0x12f5);
PROVIDE(_char2num = 0x63c1);
PROVIDE(_2char2dec = 0x63ed);
PROVIDE(_2char2hex = 0x645d);
PROVIDE(_memchr = 0x64c5);
PROVIDE(_memcmp = 0x6545);
PROVIDE(_memcpy = 0x6591);
PROVIDE(_memmove = 0x6611);
PROVIDE(_memset = 0x66ad);
PROVIDE(_vsscanf = 0x67d1);
PROVIDE(_sscanf = 0x6c51);
PROVIDE(_stratoi = 0x6c69);
PROVIDE(_strcat = 0x6cc1);
PROVIDE(_strchr = 0x6d11);
PROVIDE(_strcmp = 0x6de9);
PROVIDE(_strcpy = 0x6e49);
PROVIDE(_stricmp = 0x6e91);
PROVIDE(_strlen = 0x6ec5);
PROVIDE(_strncat = 0x6f29);
PROVIDE(_strncmp = 0x6f8d);
PROVIDE(_strncpy = 0x7019);
PROVIDE(_strnlen = 0x7081);
PROVIDE(_strpbrk = 0x70a5);
PROVIDE(_strsep = 0x70d1);
PROVIDE(_strstr = 0x73cd);
PROVIDE(_strtoull = 0x75d5);
PROVIDE(_strtoll = 0x76d5);
PROVIDE(_strtok = 0x76f5);
PROVIDE(_strtol = 0x7899);
PROVIDE(_strtoul = 0x79d9);
PROVIDE(_strupr = 0x79e1);
PROVIDE(rand_first = 0x801dc);
PROVIDE(rand_seed = 0x801e0);
PROVIDE(_strtok_r = 0x7761);
PROVIDE(_strtol_r = 0x776d);
PROVIDE(_strtoul_r = 0x78a1);
PROVIDE(__strtok_r = 0x7705);
