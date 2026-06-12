/*
 * cpp_selftest.cpp — exercise the POSIX VFS (component/syscall_config) from the C++
 * standard library. The first .cpp compiled into the KM4 image; it proves libstdc++
 * iostreams + fstreams + std::filesystem ride the newlib retarget correctly.
 *
 * Console split (set up by amebad_console_init_lpuart() before this runs):
 *   std::cout / std::cin -> LPUART (UART3, PB1/PB2, host COM7)
 *   std::cerr            -> LOGUART (the boot/log console)
 * The printf diagnostics below follow the at_console backend, which the LPUART init
 * switched to LPUART — so they print there alongside std::cout (std::cerr stays on
 * LOGUART, demonstrating the split).
 *
 * Build-gated by AMEBAD_CPP_SELFTEST (make CPPTEST=ON). Lives in liblfs_config.a
 * whose .text is pinned to the XIP tail (see rlx8721d_img2_is.ld), so enabling it
 * does not shift the boot/WiFi code addresses.
 *
 * The image is built -fno-exceptions, so every std::filesystem call uses the
 * std::error_code& overload — the throwing overloads would std::terminate().
 */
#ifdef AMEBAD_CPP_SELFTEST

#include <cstdio>           /* printf */
#include <iostream>
#include <fstream>
#include <string>
#include <system_error>
#include <filesystem>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

/* Bare-metal C++ runtime support: libstdc++ static-init objects (system_error.o,
 * cxx11-ios_failure.o) reference __dso_handle, normally provided by crtbegin.o —
 * which -nostartfiles drops. The KM4 image never unloads, so a single dummy is
 * enough (it is only used as the per-DSO handle passed to __cxa_atexit). */
extern "C" void *__dso_handle = nullptr;

namespace fs = std::filesystem;

extern "C" void cpp_selftest(void)
{
    printf("\n\r[CPP] ==== libstdc++ over POSIX VFS self-test ====\n\r");

    /* --- A) iostreams: std::cout/std::cin over LPUART, std::cerr over LOGUART --- */
    std::cerr << "[CPP] (this line is std::cerr -> LOGUART)\r\n" << std::flush;

    std::cout << "\r\n[CPP] std::cout -> LPUART (COM7). Type a line + Enter: "
              << std::flush;
    std::string line;
    std::getline(std::cin, line);                       /* blocks on LPUART RX */
    std::cout << "[CPP] you typed: \"" << line << "\"\r\n" << std::flush;
    printf("[CPP] cout/cin over LPUART -> %s\n\r",
               line.empty() ? "FAIL (empty)" : "PASS");

    /* --- DIAG) raw POSIX open() to isolate the _open VFS path from libstdc++ --- */
    {
        errno = 0;
        int fd = open("/cpp_raw.txt", O_WRONLY | O_CREAT | O_TRUNC, 0);
        printf("[CPP] raw open() fd=%d errno=%d\n\r", fd, errno);
        if (fd >= 0) {
            int w = (int)write(fd, "raw", 3);
            printf("[CPP] raw write()=%d errno=%d\n\r", w, errno);
            close(fd);
        }
    }

    /* --- B) fstreams: write then read back a LittleFS file --- */
    const char *path = "/cpp_fs.txt";
    const std::string payload = "hello from std::ofstream";
    {
        errno = 0;
        std::ofstream ofs(path, std::ios::out | std::ios::trunc);
        printf("[CPP] ofstream is_open=%d errno=%d\n\r", (int)ofs.is_open(), errno);
        ofs << payload << '\n';
        ofs.close();
        printf("[CPP] ofstream write '%s' -> %s\n\r", path,
                   ofs.fail() ? "FAIL" : "PASS");
    }
    {
        std::ifstream ifs(path);
        std::string got;
        std::getline(ifs, got);
        bool ok = (got == payload);
        printf("[CPP] ifstream readback -> %s (got \"%s\")\n\r",
                   ok ? "PASS" : "FAIL", got.c_str());
    }

    /* --- C) std::filesystem metadata (error_code overloads only) --- */
    std::error_code ec;

    bool ex = fs::exists(path, ec);
    printf("[CPP] fs::exists -> %s\n\r", (ex && !ec) ? "PASS" : "FAIL");

    auto sz = fs::file_size(path, ec);
    printf("[CPP] fs::file_size = %d B -> %s\n\r",
               (int)(ec ? -1 : sz), ec ? "FAIL" : "PASS");

    fs::file_status st = fs::status(path, ec);
    bool reg = (!ec && fs::is_regular_file(st));
    printf("[CPP] fs::is_regular_file -> %s\n\r", reg ? "PASS" : "FAIL");

    bool removed = fs::remove(path, ec);
    bool gone = !fs::exists(path, ec);
    printf("[CPP] fs::remove + exists==false -> %s\n\r",
               (removed && gone) ? "PASS" : "FAIL");

    std::cout << "[CPP] self-test done.\r\n" << std::flush;
    printf("[CPP] ==== self-test done ====\n\r");
}

#endif /* AMEBAD_CPP_SELFTEST */
