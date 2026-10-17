/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifdef HLTB_LAYOUT_PROBE
#include "layout_probe_blob.h"
#define shell_probe_blob layout_probe_blob
#define PROBE_LOG "/system_tmp/hltb-layout-probe.log"
#else
#include "shell_probe_blob.h"
#define PROBE_LOG "/system_tmp/hltb-shell-probe-v2.log"
#endif
#include "stable_shellui_injector.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>
int main() {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t size = 0;
    pid_t pid = 0;
    if (sysctl(mib, 4, nullptr, &size, nullptr, 0))
        return 1;
    auto *buf = static_cast<char *>(std::malloc(size));
    if (!buf)
        return 1;
    if (sysctl(mib, 4, buf, &size, nullptr, 0)) {
        std::free(buf);
        return 1;
    }
    for (size_t off = 0; off + sizeof(int) <= size;) {
        auto *k = reinterpret_cast<kinfo_proc *>(buf + off);
        if (k->ki_structsize <= 0 || off + k->ki_structsize > size)
            break;
        if (!std::strcmp(k->ki_comm, "SceShellUI"))
            pid = k->ki_pid;
        off += k->ki_structsize;
    }
    std::free(buf);
    if (!pid) {
        std::puts("ShellUI not found");
        return 1;
    }
    FILE *f = nullptr;
#ifndef HLTB_LAYOUT_PROBE
    char marker[128] = {0};
    f = std::fopen(PROBE_LOG, "r");
    if (f) {
        std::fgets(marker, sizeof marker, f);
        std::fclose(f);
        int prev = 0;
        if (std::sscanf(marker, "entered pid=%d", &prev) == 1 && prev == pid) {
            std::puts("Probe already injected into current ShellUI");
            return 1;
        }
    }
#endif
    std::printf("Inject read-only metadata probe into ShellUI pid=%d bytes=%zu\n", pid,
                sizeof shell_probe_blob);
    std::fflush(stdout);
    auto r = common_fps::ps5::inject_shellui_stable(pid, shell_probe_blob, sizeof shell_probe_blob);
    std::printf("attach=%d mapped=%d imports=%u unresolved=%u first=%s thread=%d rc=%d detach=%d "
                "auth-restored=%d\n",
                r.attached, r.elf_loaded, r.imports_resolved, r.imports_unresolved,
                r.first_unresolved ? r.first_unresolved : "", r.pthread_create_ok,
                r.pthread_create_rc, r.detached, r.auth_restored);
    for (int n = 0; n < 10; n++) {
        f = std::fopen(PROBE_LOG, "r");
        if (f) {
            char line[512];
            while (std::fgets(line, sizeof line, f))
                std::fputs(line, stdout);
            std::fclose(f);
            break;
        }
        sleep(1);
    }
    return r.pthread_create_ok ? 0 : 1;
}
