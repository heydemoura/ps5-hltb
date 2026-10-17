/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "native_protocol.h"
#include "stable_shellui_injector.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ps5/kernel.h>
#include <ps5/mdbg.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>
extern "C" {
#include "pt.h"
int hltb_export_catalog(void);
}
#include "shell_native_blob.h"
struct undo_record {
    hltb_native_request q;
    uint32_t size;
    unsigned char original[16];
};
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
    if (rc || restore || size > sizeof verify)
        return false;
    int read_rc = mdbg_copyout(pid, addr, verify, size);
    if (read_rc)
        read_rc = pt_copyout(pid, addr, verify, size);
    return !read_rc && !std::memcmp(verify, buf, size);
}
static bool patch(pid_t pid, const char *path, bool remove) {
    hltb_native_request q{};
    undo_record undo{};
    FILE *f = nullptr;
    char undo_path[128];
    std::snprintf(undo_path, sizeof undo_path, "%s.undo", path);
    if (remove) {
        f = std::fopen(undo_path, "rb");
        if (!f)
            return false;
        bool ok = std::fread(&undo, 1, sizeof undo, f) == sizeof undo;
        std::fclose(f);
        if (!ok)
            return false;
        q = undo.q;
    } else {
        for (int i = 0; i < 50; i++) {
            f = std::fopen(path, "rb");
            if (f)
                break;
            usleep(100000);
        }
        if (!f)
            return false;
        bool ok = std::fread(&q, 1, sizeof q, f) == sizeof q;
        std::fclose(f);
        if (!ok)
            return false;
    }
    if (q.magic != HLTB_NATIVE_MAGIC || q.pid != pid || (q.kind != 1 && q.kind != 2) ||
        q.method < 0x10000 || q.hook < 0x10000 || q.trampoline < 0x10000)
        return false;
    uint64_t auth = kernel_get_ucred_authid(getpid());
    if (!auth || kernel_set_ucred_authid(getpid(), UINT64_C(0x4800000000010003)))
        return false;
    bool ok = false;
    int attach_rc = pt_attach(pid);
    if (attach_rc == 0) {
        unsigned char b[16],
            jump[16] = {0xff, 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x90, 0x90};
        std::memcpy(jump + 6, &q.hook, 8);
        size_t size = q.kind == 1 ? 14 : 16;
        int read_rc = mdbg_copyout(pid, q.method, b, sizeof b);
        if (read_rc)
            read_rc = pt_copyout(pid, q.method, b, sizeof b);
        if (!read_rc) {
            if (remove) {
                if (undo.size == size && !std::memcmp(b, jump, size))
                    ok = write_code(pid, q.method, undo.original, size);
            } else {
                unsigned char t[64] = {0};
                size_t tsize = 0;
                bool valid = false;
                if (q.kind == 1 && !std::memcmp(b, "\x55\x48\x89\xe5\x80\x3d", 6) && b[10] == 0 &&
                    !std::memcmp(b + 11, "\x48\x89\xf9", 3)) {
                    int32_t disp;
                    std::memcpy(&disp, b + 6, 4);
                    uint64_t cmp = q.method + 11 + disp, back = q.method + 14;
                    const unsigned char begin[] = {0x55, 0x48, 0x89, 0xe5, 0x49, 0xbb};
                    std::memcpy(t, begin, 6);
                    std::memcpy(t + 6, &cmp, 8);
                    const unsigned char mid[] = {0x41, 0x80, 0x3b, 0, 0x48, 0x89, 0xf9,
                                                 0xff, 0x25, 0,    0, 0,    0};
                    std::memcpy(t + 14, mid, sizeof mid);
                    std::memcpy(t + 27, &back, 8);
                    tsize = 35;
                    valid = true;
                } else if (q.kind == 1 &&
                           !std::memcmp(b, "\x55\x48\x89\xe5\x41\x56\x53\x80\x3d", 9) &&
                           b[13] == 0) {
                    // ReactTextShadowNode.UpdateLabel: relocate its RIP-relative guard.
                    int32_t disp;
                    std::memcpy(&disp, b + 9, 4);
                    uint64_t cmp = q.method + 14 + disp, back = q.method + 14;
                    std::memcpy(t, b, 7);
                    t[7] = 0x49;
                    t[8] = 0xbb;
                    std::memcpy(t + 9, &cmp, 8);
                    const unsigned char mid[] = {0x41, 0x80, 0x3b, 0, 0xff, 0x25, 0, 0, 0, 0};
                    std::memcpy(t + 17, mid, sizeof mid);
                    std::memcpy(t + 27, &back, 8);
                    tsize = 35;
                    valid = true;
                } else if (q.kind == 2 &&
                           !std::memcmp(
                               b,
                               "\x55\x48\x8b\xec\x48\x83\xec\x60\x4c\x89\x6d\xe8\x4c\x89\x75\xf0",
                               16)) {
                    std::memcpy(t, b, 16);
                    const unsigned char absjmp[] = {0xff, 0x25, 0, 0, 0, 0};
                    std::memcpy(t + 16, absjmp, 6);
                    uint64_t back = q.method + 16;
                    std::memcpy(t + 22, &back, 8);
                    tsize = 30;
                    valid = true;
                }
                if (valid) {
                    undo.q = q;
                    undo.size = size;
                    std::memcpy(undo.original, b, sizeof b);
                    f = std::fopen(undo_path, "wb");
                    bool saved = false;
                    if (f) {
                        saved = std::fwrite(&undo, 1, sizeof undo, f) == sizeof undo;
                        std::fclose(f);
                    }
                    if (saved && write_code(pid, q.trampoline, t, tsize)) {
                        ok = write_code(pid, q.method, jump, size);
                        if (!ok)
                            (void)write_code(pid, q.method, b, size);
                    }
                }
            }
        } else
            std::printf("method read failed kind=%u rc=%d errno=%d\n", q.kind, read_rc, errno);
        bool detached = pt_detach(pid, 0) == 0;
        ok = ok && detached;
        // Let the target leave its debug stop before the next independent attach.
        usleep(100000);
    } else
        std::printf("attach failed kind=%u rc=%d errno=%d\n", q.kind, attach_rc, errno);
    bool restored = kernel_set_ucred_authid(getpid(), auth) == 0;
    ok = ok && restored;
    if (ok && remove)
        unlink(undo_path);
    std::printf("%s kind=%u method=%lx verified=%d\n", remove ? "restore" : "install", q.kind,
                (unsigned long)q.method, ok);
    std::fflush(stdout);
    return ok;
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
    if (!pid)
        return 1;
#ifdef HLTB_NATIVE_REMOVE
    // Let the frame callback dispose its sibling labels before restoring hooks.
    unlink("/system_tmp/hltb-native.enable");
    usleep(400000);
    bool a = patch(pid, HLTB_NATIVE_TEXT, true);
    bool b = patch(pid, HLTB_NATIVE_UPDATE, true);
    return a && b ? 0 : 1;
#else
#ifdef HLTB_NATIVE_RETRY
    if (!patch(pid, HLTB_NATIVE_TEXT, false))
        return 1;
    if (!patch(pid, HLTB_NATIVE_UPDATE, false)) {
        (void)patch(pid, HLTB_NATIVE_TEXT, true);
        return 1;
    }
    return 0;
#endif
#ifndef HLTB_NATIVE_REPLACE
    char marker[128] = {0};
    FILE *f = std::fopen(HLTB_NATIVE_LOG, "r");
    if (f) {
        std::fgets(marker, sizeof marker, f);
        std::fclose(f);
        int prev = 0;
        if (std::sscanf(marker, "entered pid=%d", &prev) == 1 && prev == pid) {
            std::puts("Native payload already resident; refusing duplicate");
            return 1;
        }
    }
#else
    // Replacement is permitted only after the old hooks have been restored.
    if (access(HLTB_NATIVE_TEXT ".undo", F_OK) == 0 ||
        access(HLTB_NATIVE_UPDATE ".undo", F_OK) == 0) {
        std::puts("Restore previous native hooks first");
        return 1;
    }
#endif
    if (hltb_export_catalog())
        return 1;
    unlink(HLTB_NATIVE_TEXT);
    unlink(HLTB_NATIVE_UPDATE);
    unlink("/system_tmp/hltb-native.enable");
    auto r =
        common_fps::ps5::inject_shellui_stable(pid, shell_native_blob, sizeof shell_native_blob);
    std::printf("native inject pid=%d attach=%d mapped=%d imports=%u unresolved=%u thread=%d "
                "detach=%d auth-restored=%d\n",
                pid, r.attached, r.elf_loaded, r.imports_resolved, r.imports_unresolved,
                r.pthread_create_ok, r.detached, r.auth_restored);
    std::fflush(stdout);
    if (!r.pthread_create_ok)
        return 1;
    if (!patch(pid, HLTB_NATIVE_TEXT, false))
        return 1;
    if (!patch(pid, HLTB_NATIVE_UPDATE, false)) {
        (void)patch(pid, HLTB_NATIVE_TEXT, true);
        return 1;
    }
#ifdef HLTB_NATIVE_AUTOSTART
    FILE *enabled = std::fopen("/system_tmp/hltb-native.enable", "w");
    if (!enabled) {
        std::perror("enable native annotations");
        return 1;
    }
    std::fclose(enabled);
    std::puts("HowLongToBeat native annotations enabled");
#else
    std::puts("Native observer online; rendering remains disabled for inspection");
#endif
    return 0;
#endif
}
