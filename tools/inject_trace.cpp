/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "stable_shellui_injector.hpp"
#include "trace_protocol.h"
#include <ps5/kernel.h>
#include <ps5/mdbg.h>
#include <sys/mman.h>
extern "C" {
#include "pt.h"
}
#include "shell_trace_blob.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>
static bool write_code(pid_t pid, uint64_t addr, const void *buf, size_t size) {
    uint64_t page = addr & ~UINT64_C(0x3fff);
    size_t len = ((addr + size + 0x3fff) & ~UINT64_C(0x3fff)) - page;
    if (kernel_mprotect(pid, page, len, PROT_READ | PROT_WRITE | PROT_EXEC))
        return false;
    int rc = mdbg_copyin(pid, buf, addr, size);
    if (rc)
        rc = pt_copyin(pid, buf, addr, size);
    int restore = kernel_mprotect(pid, page, len, PROT_READ | PROT_EXEC);
    unsigned char verify[64];
    return !rc && !restore && size <= sizeof verify && !mdbg_copyout(pid, addr, verify, size) &&
           !std::memcmp(verify, buf, size);
}
static bool apply_trace(pid_t pid) {
    hltb_trace_request q{};
    FILE *f = nullptr;
    for (int i = 0; i < 30; i++) {
        f = std::fopen(HLTB_TRACE_REQUEST, "rb");
        if (f)
            break;
        usleep(100000);
    }
    if (!f)
        return false;
    bool read = std::fread(&q, 1, sizeof q, f) == sizeof q;
    std::fclose(f);
    if (!read || q.magic != HLTB_TRACE_MAGIC || q.pid != pid)
        return false;
    uint64_t auth = kernel_get_ucred_authid(getpid());
    if (!auth || kernel_set_ucred_authid(getpid(), UINT64_C(0x4800000000010003)))
        return false;
    bool attached = pt_attach(pid) == 0, ok = false;
    if (attached) {
        unsigned char b[14];
        if (!mdbg_copyout(pid, q.method, b, sizeof b) &&
            !std::memcmp(b, "\x55\x48\x89\xe5\x80\x3d", 6) && b[10] == 0 &&
            !std::memcmp(b + 11, "\x48\x89\xf9", 3)) {
            // Relocate the RIP-relative cmp into an absolute R11 operand. R11 is caller scratch.
            int32_t disp;
            std::memcpy(&disp, b + 6, 4);
            uint64_t cmp = q.method + 11 + disp, back = q.method + 14;
            unsigned char t[35] = {0x55, 0x48, 0x89, 0xe5, 0x49, 0xbb};
            std::memcpy(t + 6, &cmp, 8);
            const unsigned char mid[] = {0x41, 0x80, 0x3b, 0, 0x48, 0x89, 0xf9,
                                         0xff, 0x25, 0,    0, 0,    0};
            std::memcpy(t + 14, mid, sizeof mid);
            std::memcpy(t + 27, &back, 8);
            unsigned char patch[14] = {0xff, 0x25, 0, 0, 0, 0};
            std::memcpy(patch + 6, &q.hook, 8);
            FILE *undo = std::fopen("/system_tmp/hltb-trace.undo", "wb");
            bool saved = false;
            if (undo) {
                saved = std::fwrite(&q, 1, sizeof q, undo) == sizeof q &&
                        std::fwrite(b, 1, sizeof b, undo) == sizeof b;
                std::fclose(undo);
            }
            if (saved && write_code(pid, q.trampoline, t, sizeof t)) {
                ok = write_code(pid, q.method, patch, sizeof patch);
                if (!ok)
                    (void)write_code(pid, q.method, b, sizeof b);
            }
        }
        bool detached = pt_detach(pid, 0) == 0;
        ok = ok && detached;
    }
    bool restored = kernel_set_ucred_authid(getpid(), auth) == 0;
    return ok && restored;
}
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
#ifdef HLTB_TRACE_REMOVE
    hltb_trace_request q{};
    unsigned char original[14];
    FILE *undo = std::fopen("/system_tmp/hltb-trace.undo", "rb");
    if (!undo) {
        std::puts("No trace undo record");
        return 1;
    }
    bool valid = std::fread(&q, 1, sizeof q, undo) == sizeof q &&
                 std::fread(original, 1, sizeof original, undo) == sizeof original;
    std::fclose(undo);
    if (!valid || q.magic != HLTB_TRACE_MAGIC || q.pid != pid ||
        std::memcmp(original, "\x55\x48\x89\xe5\x80\x3d", 6)) {
        std::puts("Undo record does not match current ShellUI");
        return 1;
    }
    uint64_t auth = kernel_get_ucred_authid(getpid());
    if (!auth || kernel_set_ucred_authid(getpid(), UINT64_C(0x4800000000010003)))
        return 1;
    bool ok = false;
    if (pt_attach(pid) == 0) {
        unsigned char expected[14] = {0xff, 0x25, 0, 0, 0, 0}, actual[14];
        std::memcpy(expected + 6, &q.hook, 8);
        if (!mdbg_copyout(pid, q.method, actual, sizeof actual) &&
            !std::memcmp(expected, actual, sizeof actual))
            ok = write_code(pid, q.method, original, sizeof original);
        bool detached = pt_detach(pid, 0) == 0;
        ok = ok && detached;
    }
    bool auth_ok = kernel_set_ucred_authid(getpid(), auth) == 0;
    ok = ok && auth_ok;
    if (ok) {
        unlink("/system_tmp/hltb-trace.undo");
        unlink(HLTB_TRACE_REQUEST);
    }
    std::printf("native SetText restored=%d; trace thread remains inert until reboot\n", ok);
    return ok ? 0 : 1;
#endif

    char marker[128] = {0};
    FILE *f = std::fopen("/system_tmp/hltb-trace.log", "r");
    if (f) {
        std::fgets(marker, sizeof marker, f);
        std::fclose(f);
        int prev = 0;
        if (std::sscanf(marker, "entered pid=%d", &prev) == 1 && prev == pid) {
            std::puts("Probe already injected into current ShellUI");
            return 1;
        }
    }
    std::printf("Inject native text trace into ShellUI pid=%d bytes=%zu\n", pid,
                sizeof shell_trace_blob);
    std::fflush(stdout);
    auto r = common_fps::ps5::inject_shellui_stable(pid, shell_trace_blob, sizeof shell_trace_blob);
    std::printf("attach=%d mapped=%d imports=%u unresolved=%u first=%s thread=%d rc=%d detach=%d "
                "auth-restored=%d\n",
                r.attached, r.elf_loaded, r.imports_resolved, r.imports_unresolved,
                r.first_unresolved ? r.first_unresolved : "", r.pthread_create_ok,
                r.pthread_create_rc, r.detached, r.auth_restored);
    for (int n = 0; n < 10; n++) {
        f = std::fopen("/system_tmp/hltb-trace.log", "r");
        if (f) {
            char line[512];
            while (std::fgets(line, sizeof line, f))
                std::fputs(line, stdout);
            std::fclose(f);
            break;
        }
        sleep(1);
    }
    bool hooked = r.pthread_create_ok && apply_trace(pid);
    std::printf("native SetText trace installed=%d\n", hooked);
    return hooked ? 0 : 1;
}
