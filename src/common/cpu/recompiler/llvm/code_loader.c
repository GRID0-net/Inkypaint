/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The JIT's loader (code_loader.h): a code arena, and the LLVM backend's ELF objects linked into it (COFF objects:
   code_loader_coff.c) */
#ifdef __linux__
#define _GNU_SOURCE /* memfd_create, syscall */
#elif defined(__APPLE__)
#define _DARWIN_C_SOURCE /* MAP_ANON, MAP_JIT and pthread_jit_write_protect_np besides POSIX */
#endif
#include "common/cpu/recompiler/llvm/code_loader_int.h"
#include "common/cpu/recompiler/llvm/gdb_jit.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "common/cpu/recompiler/llvm/elf_object.h"
#endif
#if defined(__linux__)
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#elif defined(__FreeBSD__)
#include <sys/shm.h>
#endif
/* macOS on Apple silicon: the arena is MAP_JIT memory, which each thread sees either writable or executable
   (apple_arena), and other threads are synchronized with new code through the kernel (sync_threads) */
#if defined(__APPLE__) && defined(__aarch64__) && (TARGET_OS_OSX || TARGET_OS_SIMULATOR)
#define APPLE_JIT 1
#include <mach/mach.h>
#include <pthread.h>
#if TARGET_OS_SIMULATOR
/* The iOS simulator is a Mac process that has MAP_JIT as macOS does, but its SDK marks pthread_jit_write_protect_np
   unavailable on iOS: the symbol is there, found by name */
#include <dlfcn.h>
#endif
#endif
/* iOS on a device: an app may not make memory executable, so a debugger does it for the app (ios_arena), and the other
   threads are synchronized with new code as on macOS (sync_threads) */
#if defined(__APPLE__) && defined(__aarch64__) && TARGET_OS_IOS && !TARGET_OS_SIMULATOR
#define IOS_JIT 1
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <sys/sysctl.h>
#endif
#if defined(APPLE_JIT) || defined(IOS_JIT)
#define APPLE_THREADS 1
#endif
#if defined(__linux__) && defined(__aarch64__)
/* <linux/membarrier.h>'s commands (Linux 4.16), by number for C libraries whose headers predate them */
#define MEMBARRIER_SYNC_CORE          (1 << 5) /* MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE */
#define MEMBARRIER_REGISTER_SYNC_CORE (1 << 6) /* MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE */
#endif

/* the table keeps addresses in SnxLinkNames' size_t values */
typedef char snx_code_loader_needs_64_bits[sizeof(size_t) >= 8 ? 1 : -1];

/* the machine whose code this process runs, as an ELF e_machine: x86-64's 62, AArch64's 183; Windows' x86-64 COFF.
   macOS on Apple silicon loads ELF objects as well: the IR backend writes them for the loader with Darwin's calling
   convention (snx_ir_new_jit), and so does iOS, with a debugger attached (ios_arena), and the iOS simulator on Apple
   silicon as macOS does. 0 where the loader cannot run what it loads: Intel Macs, never tried */
#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
#define HOST_MACHINE SNX_CODE_COFF_X64
#elif defined(APPLE_JIT) || defined(IOS_JIT)
#define HOST_MACHINE 183u
#elif defined(_WIN32) || defined(__APPLE__)
#define HOST_MACHINE 0u
#elif defined(__x86_64__) || defined(_M_X64)
#define HOST_MACHINE 62u
#elif defined(__aarch64__) || defined(_M_ARM64)
#define HOST_MACHINE 183u
#elif defined(__powerpc64__)
#define HOST_MACHINE 21u
#else
#define HOST_MACHINE 0u
#endif

/* the regions (code_loader_int.h), by their short names here */
#define CODE    SNX_CODE_TEXT
#define RODATA  SNX_CODE_RODATA
#define DATA    SNX_CODE_DATA
#define REGIONS SNX_CODE_REGIONS
typedef SnxCodeRegion Region;

#ifdef _WIN32
/* Windows 10 1803's placeholder calls, found at run time (memoryapi.h declares them only for newer targets) */
typedef void*(WINAPI* VirtualAlloc2Fn)(HANDLE, void*, SIZE_T, ULONG, ULONG, void*, ULONG);
typedef void*(WINAPI* MapViewOfFile3Fn)(HANDLE, HANDLE, void*, ULONG64, SIZE_T, ULONG, ULONG, void*, ULONG);
#ifndef MEM_RESERVE_PLACEHOLDER
#define MEM_RESERVE_PLACEHOLDER  0x00040000
#define MEM_REPLACE_PLACEHOLDER  0x00004000
#define MEM_PRESERVE_PLACEHOLDER 0x00000002
#endif

/* The arena: a placeholder reservation cut into the three regions. Code and read-only data are views of a section of
   their own each, into their placeholders (read-execute, read-only), and again read-write anywhere for the loader;
   data is reserved, committed as loads reach it. 0, or -1 with the reason in error */
static int windows_arena(SnxCodeLoader* loader, char* error, size_t error_size) {
    HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll");
    VirtualAlloc2Fn alloc2 = kernelbase ? (VirtualAlloc2Fn)(void*)GetProcAddress(kernelbase, "VirtualAlloc2") : NULL;
    MapViewOfFile3Fn map3 = kernelbase ? (MapViewOfFile3Fn)(void*)GetProcAddress(kernelbase, "MapViewOfFile3") : NULL;
    static const ULONG protection[REGIONS] = {PAGE_EXECUTE_READ, PAGE_READONLY, 0};
    unsigned char* base;
    int i;
    if (!alloc2 || !map3) {
        snprintf(error, error_size,
                 "the JIT's code arena needs Windows 10 1803 or later (VirtualAlloc2, MapViewOfFile3)");
        return -1;
    }
    base = (unsigned char*)alloc2(NULL, NULL, loader->reserved, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS,
                                  NULL, 0);
    if (!base) {
        snprintf(error, error_size, "cannot reserve %lu MiB of address space for code",
                 (unsigned long)(loader->reserved >> 20));
        return -1;
    }
    loader->base = base;
    /* three placeholders: splitting one off the front leaves the rest a placeholder too */
    if (!VirtualFree(base, loader->region[CODE].size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER) ||
        !VirtualFree(base + loader->region[RODATA].start, loader->region[RODATA].size,
                     MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) {
        snprintf(error, error_size, "cannot divide the code arena's reservation (%lu)", (unsigned long)GetLastError());
        return -1;
    }
    for (i = CODE; i <= RODATA; ++i) {
        size_t size = loader->region[i].size;
        HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_EXECUTE_READWRITE,
                                            (DWORD)((uint64_t)size >> 32), (DWORD)size, NULL);
        void *view = NULL, *write = NULL;
        if (section) {
            view = map3(section, NULL, base + loader->region[i].start, 0, size, MEM_REPLACE_PLACEHOLDER, protection[i],
                        NULL, 0);
            if (view) write = map3(section, NULL, NULL, 0, size, 0, PAGE_READWRITE, NULL, 0);
            CloseHandle(section); /* the views keep it */
        }
        if (!view || !write) {
            if (view) UnmapViewOfFile(view);
            snprintf(error, error_size, "cannot map %lu MiB of code memory twice (%lu)", (unsigned long)(size >> 20),
                     (unsigned long)GetLastError());
            return -1;
        }
        loader->region[i].write = (unsigned char*)write;
    }
    if (!alloc2(NULL, base + loader->region[DATA].start, loader->region[DATA].size,
                MEM_RESERVE | MEM_REPLACE_PLACEHOLDER, PAGE_NOACCESS, NULL, 0)) {
        snprintf(error, error_size, "cannot reserve the code arena's data (%lu)", (unsigned long)GetLastError());
        return -1;
    }
    loader->region[DATA].write = base + loader->region[DATA].start;
    return 0;
}
#elif defined(APPLE_JIT)
/* The arena on macOS on Apple silicon, where memory becomes executable only as MAP_JIT memory, which mmap places where
   it likes and never shares (MAP_FIXED and MAP_SHARED fail with it: XNU's bsd/kern/kern_mman.c), so it has no second
   view. Code and read-only data are one MAP_JIT mapping, readable, writable and executable in the page tables, which
   each thread sees either executable or writable, as it last chose with pthread_jit_write_protect_np (executable
   until then). Every thread runs code from it, and only the loading thread sees it writable, while it writes a load
   (snx_code_writable): to the others the read-only data stays read-only, and no thread can write and run the code at
   once. Data is ordinary private memory mapped over the mapping's end, made read-write as loads reach it. An app
   without the hardened runtime, as the ad hoc signed ScarletNX.app is (scripts/apple-app.sh), needs no entitlement for
   MAP_JIT and may have several such mappings, one per loader; with it, com.apple.security.cs.allow-jit allows one. 0,
   or -1 with the reason in error */
static int apple_arena(SnxCodeLoader* loader, char* error, size_t error_size) {
    void* base =
        mmap(NULL, loader->reserved, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    int i;
    if (base == MAP_FAILED) {
        snprintf(error, error_size, "cannot map %lu MiB of JIT memory for code",
                 (unsigned long)(loader->reserved >> 20));
        return -1;
    }
    loader->base = (unsigned char*)base;
    if (mmap(loader->base + loader->region[DATA].start, loader->region[DATA].size, PROT_NONE,
             MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) == MAP_FAILED) {
        snprintf(error, error_size, "cannot map the code arena's data");
        return -1;
    }
    for (i = CODE; i < REGIONS; ++i) loader->region[i].write = loader->base + loader->region[i].start;
    return 0;
}
#elif defined(IOS_JIT)
/* StikJIT's protocol (INTEGRATION.md): the command in x16, the arguments in x0 and x1, and brk #0xf00d, which only an
   attached debugger answers; without one the brk kills the process, so ios_arena checks first. Command 1 prepares a
   region: with x0 NULL the debugger maps x1 bytes read-execute (read and execute, not execute-only, on an A16 with
   iOS 26.6.1: .context/ios-jit-results.md) and touches each of its pages, about 4 s for a GiB, and returns its address
   in x0, which stays as it was when it fails. Written from the protocol's description, not from the debugger's code */
__attribute__((noinline, optnone, naked)) static void* ios_jit_prepare(void* address, size_t length) {
    __asm__ volatile("mov x16, #1\n brk #0xf00d\n ret\n");
}

/* P_TRACED: a debugger is attached now. CS_DEBUGGED stays set after it lets go, so it says nothing about the brk */
static int debugger_attached(void) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)getpid()};
    struct kinfo_proc kp;
    size_t n = sizeof(kp);
    memset(&kp, 0, sizeof(kp));
    return sysctl(mib, 4, &kp, &n, NULL, 0) == 0 && (kp.kp_proc.p_flag & P_TRACED);
}

/* The arena on iOS. The debugger prepares the whole arena as one region (read-execute), and code and read-only data
   are written through a read-write alias of their part of it (vm_remap, then vm_protect), which every load goes
   through. The data region is the region's tail, given back and mapped again as ordinary memory at the same place:
   the pages after a debugger's region are not always free (an arena of a device's app found them taken), and this way
   the three regions stay one arena's reach apart whatever is around it. The debugger can be let go after this: the
   prepared pages stay executable, and what is written into them later runs. 0, or -1 with the reason in error */
static int ios_arena(SnxCodeLoader* loader, char* error, size_t error_size) {
    size_t code = loader->region[DATA].start; /* CODE and RODATA */
    vm_address_t alias = 0, data;
    vm_prot_t cur, max;
    kern_return_t kr;
    unsigned char* base;
    int i;
    if (!debugger_attached()) {
        snprintf(error, error_size,
                 "iOS makes memory executable only for an attached debugger: enable JIT for this app (StikDebug, with "
                 "universal.js assigned to it) before opening a game");
        return -1;
    }
    fprintf(stderr, "jit arena: debugger attached, asking it for %lu MiB (CODE %lu, RODATA %lu, DATA %lu MiB)\n",
            (unsigned long)(loader->reserved >> 20), (unsigned long)(loader->region[CODE].size >> 20),
            (unsigned long)(loader->region[RODATA].size >> 20), (unsigned long)(loader->region[DATA].size >> 20));
    base = (unsigned char*)ios_jit_prepare(NULL, loader->reserved);
    fprintf(stderr, "jit arena: the debugger answered %p\n", (void*)base);
    if (!base || base == (unsigned char*)(size_t)-1 || (size_t)base % loader->page) {
        snprintf(error, error_size, "the debugger did not prepare %lu MiB of code memory",
                 (unsigned long)(loader->reserved >> 20));
        return -1;
    }
    loader->base = base;
    kr = vm_remap(mach_task_self(), &alias, code, 0, VM_FLAGS_ANYWHERE, mach_task_self(), (vm_address_t)base, FALSE,
                  &cur, &max, VM_INHERIT_NONE);
    if (kr == KERN_SUCCESS) kr = vm_protect(mach_task_self(), alias, code, FALSE, VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS) {
        snprintf(error, error_size, "cannot map the prepared code memory writable (%d)", (int)kr);
        return -1;
    }
    fprintf(stderr, "jit arena: write alias at %p (current %d, maximum %d)\n", (void*)alias, (int)cur, (int)max);
    for (i = CODE; i <= RODATA; ++i) loader->region[i].write = (unsigned char*)alias + loader->region[i].start;
    data = (vm_address_t)(base + loader->region[DATA].start);
    /* the tail as read-write where it is first (it drops the execute right only), and when the system refuses, given
       back and mapped again as ordinary memory. The pages are zeroed: the debugger touched each one */
    kr = vm_protect(mach_task_self(), data, loader->region[DATA].size, FALSE, VM_PROT_READ | VM_PROT_WRITE);
    if (kr == KERN_SUCCESS) {
        memset((void*)data, 0, loader->region[DATA].size);
        kr = vm_protect(mach_task_self(), data, loader->region[DATA].size, FALSE, VM_PROT_NONE);
    } else {
        kern_return_t freed = vm_deallocate(mach_task_self(), data, loader->region[DATA].size), mapped = freed;
        if (!freed) mapped = vm_allocate(mach_task_self(), &data, loader->region[DATA].size, VM_FLAGS_FIXED);
        if (!freed && !mapped)
            kr = vm_protect(mach_task_self(), data, loader->region[DATA].size, FALSE, VM_PROT_NONE);
        else {
            snprintf(error, error_size,
                     "cannot make the code arena's data out of the debugger's region (in place %d, given back %d, "
                     "mapped again %d)",
                     (int)kr, (int)freed, (int)mapped);
            return -1;
        }
    }
    if (kr != KERN_SUCCESS) {
        snprintf(error, error_size, "cannot protect the code arena's data (%d)", (int)kr);
        return -1;
    }
    fprintf(stderr, "jit arena: data at %p ready\n", (void*)data);
    loader->region[DATA].write = base + loader->region[DATA].start;
    return 0;
}
#else
/* Shared memory of size bytes, mapped at at (in the arena's reservation) readable, and executable when asked, and once
   more read-write where the system puts it: that view, or NULL with the reason in error */
static unsigned char* map_views(unsigned char* at, size_t size, int executable, char* error, size_t error_size) {
    void *view, *write = MAP_FAILED;
    int fd;
#if defined(__sun__) || defined(__HAIKU__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
    fd = shm_open_anon(O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
#elif defined(__FreeBSD__)
    fd = shm_open(SHM_ANON, O_RDWR, 0600);
#elif defined(__APPLE__) || defined(__managarm__)
    /* macOS doesn't have memfd_create, use anonymous temporary file */
    char template_path[] = "/tmp/eden_mem_XXXXXX";
    fd = mkstemp(template_path);
    if (fd >= 0) {
        unlink(template_path);
    }
#elif defined(__linux__) && defined(SYS_memfd_create)
    /* the system call itself: Android's C library has memfd_create only from Android 11 (API 30) */
    fd = (int)syscall(SYS_memfd_create, "snx-jit-code", 1u /* MFD_CLOEXEC */);
#else
    fd = memfd_create("HostMemory", 0);
#endif
    if (fd < 0) {
        snprintf(error, error_size, "no anonymous shared memory for the code arena");
        return NULL;
    }
    if (!ftruncate(fd, (off_t)size)) {
        view = mmap(at, size, PROT_READ | (executable ? PROT_EXEC : 0), MAP_SHARED | MAP_FIXED, fd, 0);
        if (view == (void*)at) write = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    }
    close(fd); /* the mappings keep the memory */
    if (write == MAP_FAILED) {
        snprintf(error, error_size, "cannot map %lu MiB of code memory twice", (unsigned long)(size >> 20));
        return NULL;
    }
    return (unsigned char*)write;
}
#endif

/* Whether code written by this thread can be published to threads running on other cores (code_loader.h). An
   AArch64 core may hold instructions it fetched before the code was written, which neither the writer's cache
   maintenance nor the reader's acquire load discards: each core needs a context synchronization event, which
   membarrier's SYNC_CORE command makes every thread of the process take (an ISB on the way back to user space), and
   which the process registers for once (on macOS, a context switch: sync_threads). Without it a load would hand out
   code another core might run stale, so the loader refuses and the JIT stays off (the interpreter runs those
   functions) */
static int can_publish(unsigned machine, char* error, size_t error_size) {
    if (machine != HOST_MACHINE) return 1; /* nothing loaded runs: tests of another machine's objects */
#if defined(__aarch64__)
#if defined(__linux__) && defined(__NR_membarrier)
    if (!syscall(__NR_membarrier, MEMBARRIER_REGISTER_SYNC_CORE, 0u, 0)) return 1;
    snprintf(error, error_size, "the kernel has no membarrier SYNC_CORE (Linux 4.16), which publishing JIT code needs");
#elif defined(APPLE_THREADS)
    (void)error;
    (void)error_size;
    return 1; /* XNU has no membarrier: each load passes the other threads through the kernel instead (sync_threads) */
#else
    snprintf(error, error_size, "no way to synchronize other cores with new JIT code on this system");
#endif
    return 0;
#else
    (void)error;
    (void)error_size;
    /* x86-64 fetches what stores wrote, on every core (code_loader.h). PowerPC64: icbi invalidates the line in every
       processor's instruction cache (Power ISA Book II, 4.3.1), and the loading thread's flush ends in isync. Another
       thread (the NX runtime JIT's guest threads, the A32 executor's) reaches the code through a pointer it loads
       after the load returned, at addresses the arena never had code at, so it holds no stale instructions of them */
    return 1;
#endif
}

SnxCodeLoader* snx_code_loader_new(size_t reserve, char* error, size_t error_size) {
    if (!HOST_MACHINE) {
        snprintf(error, error_size, "the JIT loader runs on x86-64, AArch64 and PowerPC64 hosts");
        return NULL;
    }
    return snx_code_loader_new_machine(reserve, HOST_MACHINE, error, error_size);
}

static SnxCodeLoader* new_loader(size_t reserve, unsigned machine, uint64_t toc, int shared, char* error,
                                 size_t error_size);

SnxCodeLoader* snx_code_loader_new_machine(size_t reserve, unsigned machine, char* error, size_t error_size) {
    return new_loader(reserve, machine, 0, 0, error, error_size);
}

#if defined(__powerpc64__)
/* reserve bytes of address space within 32-bit reach of toc both ways (TOC-relative addis, and ELFv2's global entry,
   reach 2 GiB below and above it): the first place mmap gives, asked for from toc outwards in 64 MiB steps. NULL when
   there is none */
static unsigned char* reserve_near(size_t reserve, uint64_t toc) {
    const uint64_t step = (uint64_t)1 << 26, reach = ((uint64_t)1 << 31) - step;
    uint64_t centre = toc / step * step, k;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS, side;
#ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;
#endif
    for (k = 1; k * step <= reach; ++k)
        for (side = 0; side < 2; ++side) {
            uint64_t want = side ? centre - k * step - reserve : centre + k * step, got;
            void* p;
            if ((side && centre < k * step + reserve) || want + reach < toc || want + reserve > toc + reach) continue;
            p = mmap((void*)(size_t)want, reserve, PROT_NONE, flags, -1, 0);
            if (p == MAP_FAILED) continue;
            got = (uint64_t)(size_t)p; /* the hint is only a hint: anywhere in reach will do */
            if (got + reach >= toc && got + reserve <= toc + reach) return (unsigned char*)p;
            munmap(p, reserve);
        }
    return NULL;
}
#endif

SnxCodeLoader* snx_code_loader_new_toc(size_t reserve, SnxU64 toc, char* error, size_t error_size) {
#if defined(__powerpc64__)
    SnxCodeLoader* loader = NULL;
    if (!toc) {
        snprintf(error, error_size, "no TOC to share");
        return NULL;
    }
    if (!reserve) reserve = SNX_CODE_ARENA_DEFAULT;
    /* the module and what the system mapped near it may leave less room in reach: a smaller arena then */
    for (; !loader && reserve >= ((size_t)1 << 26); reserve /= 2)
        loader = new_loader(reserve, HOST_MACHINE, toc, 1, error, error_size);
    return loader;
#else
    (void)toc;
    return snx_code_loader_new(reserve, error, error_size);
#endif
}

static SnxCodeLoader* new_loader(size_t reserve, unsigned machine, uint64_t toc, int shared, char* error,
                                 size_t error_size) {
#ifdef SNX_IOS_AOT_ONLY
    snprintf(error, error_size, "runtime executable-memory loading is disabled in the signed iOS AOT build");
    return NULL;
#endif
    SnxCodeLoader* loader;
    size_t page = snx_vm_page_size();
    int i;
    if (!reserve) {
        reserve = SNX_CODE_ARENA_DEFAULT;
#ifdef IOS_JIT
        /* address space and the debugger's preparing time (4 s a GiB) are scarce on a device, and the code the JIT
           adds to a game's is small (17 KiB in a minute of one game) */
        reserve = (size_t)1 << 27;
#endif
    }
    if (reserve <= (size_t)1 << 31) reserve = (reserve + 8 * page - 1) / (8 * page) * (8 * page); /* whole pages */
    if (reserve > (size_t)1 << 31) {
        snprintf(error, error_size, "a code arena holds at most 2 GiB, so 32-bit displacements reach across it");
        return NULL;
    }
    if (!can_publish(machine, error, error_size)) return NULL;
    loader = (SnxCodeLoader*)calloc(1, sizeof(*loader));
    if (!loader) {
        snprintf(error, error_size, "out of memory");
        return NULL;
    }
    loader->reserved = reserve;
    loader->page = page;
    loader->machine = machine;
    /* code is most of what a unit holds. Read-only data is its jump tables, constants, function table and GOT, and
       generated code writes no data of its own (nor does the runtime library) */
    loader->region[CODE].size = reserve / 8 * 5;
    loader->region[RODATA].start = loader->region[CODE].size;
    loader->region[RODATA].size = reserve / 8 * 2;
    loader->region[DATA].start = loader->region[RODATA].start + loader->region[RODATA].size;
    loader->region[DATA].size = reserve - loader->region[DATA].start;
#ifdef _WIN32
    (void)i;
    if (windows_arena(loader, error, error_size)) {
        snx_code_loader_free(loader);
        return NULL;
    }
#elif defined(APPLE_JIT)
    (void)i;
    if (apple_arena(loader, error, error_size)) {
        snx_code_loader_free(loader);
        return NULL;
    }
#elif defined(IOS_JIT)
    (void)i;
    if (ios_arena(loader, error, error_size)) {
        snx_code_loader_free(loader);
        return NULL;
    }
#else
#if defined(__powerpc64__)
    loader->base = toc ? reserve_near(reserve, toc) : (unsigned char*)snx_vm_reserve(reserve);
#else
    loader->base = (unsigned char*)snx_vm_reserve(reserve);
#endif
    if (!loader->base) {
        snprintf(error, error_size, "cannot reserve %lu MiB of address space for code", (unsigned long)(reserve >> 20));
        free(loader);
        return NULL;
    }
    for (i = CODE; i <= RODATA; ++i) {
        loader->region[i].write =
            map_views(loader->base + loader->region[i].start, loader->region[i].size, i == CODE, error, error_size);
        if (!loader->region[i].write) {
            snx_code_loader_free(loader);
            return NULL;
        }
    }
    loader->region[DATA].write = loader->base + loader->region[DATA].start;
#endif
    /* PowerPC64: every load's code keeps the arena's one TOC in r2, so a call or a jump from one load to another needs
       no r2 of its own. A call out of the arena would (the runtime's functions have their TOC): loaded code reaches
       the runtime through pointers only (RtAPI), which LLVM calls with r2 saved around them. The arena's middle
       reaches all of it; a shared TOC is the AOT module's, whose functions loads then call directly */
    loader->toc = toc ? toc : (uint64_t)(size_t)loader->base + reserve / 2;
    loader->toc_shared = shared;
    return loader;
}

void snx_code_loader_free(SnxCodeLoader* loader) {
    if (!loader) return;
    snx_gdb_jit_remove(loader);
#ifdef _WIN32
    /* each part of the arena goes as it was made: the views, the data reservation, a placeholder left over */
    {
        int i;
        for (i = CODE; i <= RODATA; ++i)
            if (loader->region[i].write) {
                UnmapViewOfFile(loader->region[i].write);
                UnmapViewOfFile(loader->base + loader->region[i].start);
            }
        if (loader->base)
            for (i = CODE; i < REGIONS; ++i) VirtualFree(loader->base + loader->region[i].start, 0, MEM_RELEASE);
    }
#else
    /* the read-write views, where they are mappings of their own (macOS writes the arena itself: apple_arena) */
    {
        int i;
        for (i = CODE; i <= RODATA; ++i)
            if (loader->region[i].write && loader->region[i].write != loader->base + loader->region[i].start)
                munmap(loader->region[i].write, loader->region[i].size);
        if (loader->base) snx_vm_release(loader->base, loader->reserved); /* the arena's views with it */
    }
#endif
    snx_link_names_free(&loader->symbols);
    snx_link_names_free(&loader->entries);
    free(loader);
}

int snx_code_loader_unshare(SnxCodeLoader* loader, char* error, size_t error_size) {
#if defined(_WIN32) || defined(APPLE_JIT) || defined(IOS_JIT)
    (void)loader;
    snprintf(error, error_size, "a code arena is unshared only on Linux and the BSDs");
    return -1;
#else
    int i;
    for (i = CODE; i <= RODATA; ++i) {
        Region* r = &loader->region[i];
        unsigned char *copy = (unsigned char*)malloc(r->used ? r->used : 1), *write;
        if (!copy) {
            snprintf(error, error_size, "out of memory");
            return -1;
        }
        /* the loads' bytes kept aside, since the new memory goes in the old's place empty */
        memcpy(copy, r->write, r->used);
        write = map_views(loader->base + r->start, r->size, i == CODE, error, error_size);
        if (!write) {
            free(copy); /* the arena lost its contents: nothing loaded may run */
            return -1;
        }
        memcpy(write, copy, r->used);
        free(copy);
        munmap(r->write, r->size);
        r->write = write;
        if (i == CODE) snx_vm_flush_code(loader->base + r->start, r->used);
    }
    return 0;
#endif
}

int snx_code_define(SnxCodeLoader* loader, const char* name, SnxU64 address) {
    size_t found;
    if (snx_link_find(&loader->symbols, name, &found)) return -1;
    snx_link_insert(&loader->symbols, name, (size_t)address);
    return 0;
}

void snx_code_set_resolver(SnxCodeLoader* loader, SnxU64 (*resolver)(void* user, const char* name), void* user) {
    loader->resolver = resolver;
    loader->resolver_user = user;
}

int snx_code_find(const SnxCodeLoader* loader, const char* name, SnxU64* address) {
    if (snx_code_lookup(loader, name, address)) return 1;
    if (!loader->resolver) return 0;
    *address = loader->resolver(loader->resolver_user, name);
    return *address != 0;
}

int snx_code_lookup(const SnxCodeLoader* loader, const char* name, SnxU64* address) {
    size_t found;
    if (!snx_link_find(&loader->symbols, name, &found)) return 0;
    *address = found;
    return 1;
}

size_t snx_code_used(const SnxCodeLoader* loader) {
    return loader->region[CODE].used + loader->region[RODATA].used + loader->region[DATA].used;
}

unsigned char* snx_code_at(const SnxCodeLoader* loader, uint64_t address) {
    size_t offset = (size_t)(address - (uint64_t)(size_t)loader->base);
    const Region* r = &loader->region[offset >= loader->region[DATA].start     ? DATA
                                      : offset >= loader->region[RODATA].start ? RODATA
                                                                               : CODE];
    return r->write + (offset - r->start);
}

uint64_t snx_code_next_free(const SnxCodeLoader* loader, int region) {
    return (uint64_t)(size_t)loader->base + loader->region[region].start + loader->region[region].used;
}

/* data pages become read-write as loads reach them; pages that are stay so, used or not */
int snx_code_commit(SnxCodeLoader* loader, uint64_t end, char* error, size_t error_size) {
    uint64_t start = (uint64_t)(size_t)loader->base + loader->region[DATA].start;
    uint64_t need = (end - start + loader->page - 1) / loader->page * loader->page;
    if (need <= loader->committed) return 0;
    if (snx_vm_protect(loader->region[DATA].write + loader->committed, (size_t)(need - loader->committed),
                       SNX_VM_READ_WRITE)) {
        snprintf(error, error_size, "cannot commit %lu KiB of data memory",
                 (unsigned long)((need - loader->committed) >> 10));
        return -1;
    }
    loader->committed = (size_t)need;
    return 0;
}

#ifdef APPLE_THREADS
/* Every other thread of the process through a context synchronization event, as membarrier's SYNC_CORE does on Linux
   (can_publish), which XNU lacks. thread_get_state on a thread that is running stops it until it has left its core
   (thread_get_state_internal's thread_stop: osfmk/kern/thread_act.c, sched_prim.c), and a thread off its core comes
   back through a context switch, which XNU synchronizes for user space (an ISB requested on each switch within a task,
   machine_switch_context's arm_context_switch_requires_sync in osfmk/arm64/pcb.c, where an exception return is not one
   by itself). Threads started after the list was taken have fetched nothing. 0, or -1 with the reason in error */
static int sync_threads(char* error, size_t error_size) {
    thread_act_array_t threads;
    mach_msg_type_number_t count, i, n;
    mach_port_t self = mach_thread_self();
    arm_thread_state64_t state;
    kern_return_t kr = task_threads(mach_task_self(), &threads, &count);
    if (kr != KERN_SUCCESS) {
        mach_port_deallocate(mach_task_self(), self);
        snprintf(error, error_size, "cannot list the process's threads to publish JIT code (%d)", (int)kr);
        return -1;
    }
    for (i = 0; i < count; ++i) {
        /* again when interrupted before the thread had stopped. One that has ended runs nothing */
        n = ARM_THREAD_STATE64_COUNT;
        while (threads[i] != self &&
               thread_get_state(threads[i], ARM_THREAD_STATE64, (thread_state_t)&state, &n) == KERN_ABORTED)
            n = ARM_THREAD_STATE64_COUNT;
        mach_port_deallocate(mach_task_self(), threads[i]);
    }
    vm_deallocate(mach_task_self(), (vm_address_t)threads, count * sizeof(*threads));
    mach_port_deallocate(mach_task_self(), self);
    return 0;
}
#endif

/* The code is complete: visible to instruction fetch before any address goes out (code_loader.h). On AArch64 the
   clear-cache builtin cleans the data cache to the point of unification and invalidates the instruction cache by
   virtual address, and it is given the executable view's addresses, not the read-write view's the bytes were written
   through. Data caches behave as physically indexed and tagged, so the clean finds the written lines through either
   view (it needs a readable one: the arena's code is read-execute); an instruction cache may be virtually indexed, so
   only the addresses instruction fetch uses invalidate the lines it would hit. Both operations reach every core of the
   inner shareable domain. The read-only data and GOT need none: no instruction is fetched from them, and data accesses
   are coherent between the views. On macOS (one view, apple_arena) the flush is sys_icache_invalidate, the same IC IVAU
   with the DSBs and the ISB around it; Apple's data caches need no cleaning for instruction fetch (libplatform's
   sys_dcache_flush is a DSB alone) */
int snx_code_publish(const SnxCodeLoader* loader, uint64_t start, uint64_t end, char* error, size_t error_size) {
    if (end <= start) return 0;
    snx_vm_flush_code((void*)(size_t)start, (size_t)(end - start));
    /* and every other core of the process drops what it fetched before (can_publish) */
#if defined(__linux__) && defined(__aarch64__) && defined(__NR_membarrier)
    if (loader->machine == HOST_MACHINE) syscall(__NR_membarrier, MEMBARRIER_SYNC_CORE, 0u, 0);
#elif defined(APPLE_THREADS)
    if (loader->machine == HOST_MACHINE) return sync_threads(error, error_size);
#endif
    (void)loader;
    (void)error;
    (void)error_size;
    return 0;
}

void snx_code_writable(int writable) {
#if defined(APPLE_JIT) && TARGET_OS_SIMULATOR
    static void (*protect)(int);
    if (!protect) *(void**)&protect = dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_np");
    if (protect) protect(!writable);
#elif defined(APPLE_JIT)
    pthread_jit_write_protect_np(!writable);
#else
    (void)writable; /* the loader writes through the read-write views */
#endif
}

#if !defined(_WIN32)
/* A stub is how a call reaches a table symbol beyond its displacement, through the stub area after the load's code.
   x86-64: jmp *slot(%rip) through the symbol's GOT slot, and two int3 to the next stub (8 bytes), for a symbol
   outside the arena. AArch64: an elf_object.c veneer (12 bytes), for any table symbol, since B and BL reach 128 MiB
   and the arena is larger: direct (ADRP, ADD) to one in the arena, indirect through its GOT slot (ADRP, LDR) to one
   outside, which ADRP's 4 GiB may not reach */
#define STUB_SIZE_X86 8
/* where each load's code starts: a line no running code shares, so writing it never touches a line another core may
   be executing from */
#define CODE_LINE     64

/* One snx_code_load call. Addresses are absolute, the arena's [arena_start, arena_end) */
typedef struct {
    SnxElfObject* objs;
    size_t count;
    unsigned machine;
    SnxLinkNames globals; /* the unit's own: name -> object << 32 | symbol index */
    SnxLinkNames got;     /* "object:symbol" of a unit definition, or "@address" of a table symbol -> GOT slot */
    SnxLinkNames stubs;   /* "@address" of a table symbol that a call may not reach -> stub */
    size_t got_count, stub_count;
    uint64_t arena_start, arena_end, got_address, stub_address;
    uint64_t toc; /* PowerPC64's: the loader's */
    unsigned abi; /* PowerPC64's: SNX_ELF_PPC_ELFV1 or ELFV2 */
} Load;

/* What a relocation's symbol resolves to */
typedef struct {
    uint64_t address;
    int far; /* a table symbol outside the arena, which a 32-bit displacement may not reach */
    /* its GOT slot's and stub's key: "object:symbol" for the unit's own, "@address" for a table symbol; empty for the
       GOT itself and an undefined weak symbol (address 0) */
    char key[48];
    /* PowerPC64's calls: the unit's definition (here, def_object, def_index), or the table symbol's name */
    int here;
    size_t def_object, def_index;
    const char* name;
} Target;

static uint64_t align_up(uint64_t value, uint64_t alignment) { return (value + alignment - 1) / alignment * alignment; }

static uint64_t stub_size(const Load* ld) {
    return ld->machine == EM_AARCH64 ? SNX_ELF_VENEER_SIZE
           : ld->machine == EM_PPC64 ? SNX_ELF_PPC_VENEER_SIZE
                                     : STUB_SIZE_X86;
}

#define at        snx_code_at
#define next_free snx_code_next_free

/* The unit's definitions first, then the table, as code_loader.h says. The unit's addresses are final once the
   sections are placed; before, only the key is */
static int resolve(const SnxCodeLoader* loader, const Load* ld, size_t object, size_t index, Target* t, char* error,
                   size_t error_size) {
    const SnxElfObject* o = &ld->objs[object];
    size_t def_object, def_index;
    SnxU64 address;
    int absolute, where;
    const char* name;
    memset(t, 0, sizeof(*t));
    if (!index) return 0; /* no symbol: S is 0 (PowerPC64's R_PPC64_TOC writes the TOC whatever its symbol) */
    where = snx_elf_definition(ld->objs, &ld->globals, object, index, &def_object, &def_index, error, error_size);
    if (where < 0) return -1;
    if (where == SNX_ELF_HERE) {
        t->address = snx_elf_symbol_address(ld->objs, def_object, def_index, &absolute);
        sprintf(t->key, "%zu:%zu", def_object, def_index);
        t->here = 1;
        t->def_object = def_object;
        t->def_index = def_index;
        return 0;
    }
    name = snx_elf_symbol_name(o, &o->syms[index]);
    t->name = name;
    if (ld->machine == EM_PPC64 && !strcmp(name, ".TOC.")) {
        t->address = ld->toc;
        return 0;
    }
    if (snx_code_find(loader, name, &address)) {
        t->address = address;
        t->far = t->address < ld->arena_start || t->address >= ld->arena_end;
        sprintf(t->key, "@%llx", (unsigned long long)t->address);
        return 0;
    }
    if (!strcmp(name, "_GLOBAL_OFFSET_TABLE_")) {
        t->address = ld->got_address;
        return 0;
    }
    if (ELF64_ST_BIND(o->syms[index].st_info) == STB_WEAK) return 0;
    snprintf(error, error_size, "undefined symbol %s (referenced by %s)", name, o->input->name);
    return -1;
}

/* Whether a call to t may need a stub: on x86-64 one outside the arena (inside, rel32 reaches across it); on AArch64
   any table symbol, earlier loads' included, since the arena is larger than B's reach. The unit's own definitions are
   in reach of its calls, the unit being far smaller than 128 MiB */
static int may_need_stub(const Load* ld, const Target* t) {
    if (ld->machine == EM_AARCH64 || ld->machine == EM_PPC64) return t->key[0] == '@';
    return t->far;
}

/* Section number section when it holds the relocations of a loaded section, which the two passes walk; else NULL */
static const Elf64_Shdr* relocations(const SnxElfObject* o, size_t section) {
    const Elf64_Shdr* s = &o->sh[section];
    if (s->sh_type != SHT_RELA || s->sh_info >= o->eh->e_shnum || o->kind[s->sh_info] < 0) return NULL;
    return s;
}

/* First pass: the GOT slots and stubs the unit needs. A stub that jumps through a GOT slot gets one */
static int scan(const SnxCodeLoader* loader, Load* ld, char* error, size_t error_size) {
    size_t i, j, k, n, slot;
    Target t;
    for (i = 0; i < ld->count; ++i) {
        const SnxElfObject* o = &ld->objs[i];
        for (j = 0; j < o->eh->e_shnum; ++j) {
            const Elf64_Shdr* s = relocations(o, j);
            const Elf64_Rela* r;
            if (o->sh[j].sh_type == SHT_REL) {
                snprintf(error, error_size, "%s: REL relocations are not supported", o->input->name);
                return -1;
            }
            if (!s) continue;
            r = (const Elf64_Rela*)(o->input->data + s->sh_offset);
            n = s->sh_size / sizeof(Elf64_Rela);
            for (k = 0; k < n; ++k) {
                unsigned type = ELF64_R_TYPE(r[k].r_info);
                if (resolve(loader, ld, i, ELF64_R_SYM(r[k].r_info), &t, error, error_size)) return -1;
                if (ld->machine == EM_PPC64 && t.far && !loader->toc_shared &&
                    snx_elf_call_relocation(ld->machine, type)) {
                    snprintf(error, error_size, "%s: a call to %s, outside the code arena: another TOC",
                             o->input->name, t.name ? t.name : "?");
                    return -1;
                }
                if (snx_elf_got_relocation(ld->machine, type)) {
                    if (!t.key[0]) {
                        snprintf(error, error_size, "%s: GOT reference to an undefined weak symbol", o->input->name);
                        return -1;
                    }
                    if (!snx_link_find(&ld->got, t.key, &slot)) snx_link_insert(&ld->got, t.key, ld->got_count++);
                } else if (snx_elf_call_relocation(ld->machine, type) && may_need_stub(ld, &t) &&
                           !snx_link_find(&ld->stubs, t.key, &slot)) {
                    snx_link_insert(&ld->stubs, t.key, ld->stub_count++);
                    if (t.far && !snx_link_find(&ld->got, t.key, &slot))
                        snx_link_insert(&ld->got, t.key, ld->got_count++);
                }
            }
        }
    }
    return 0;
}

/* t's GOT slot, written with its address: the slot's own address */
static uint64_t got_slot(const SnxCodeLoader* loader, const Load* ld, const Target* t) {
    size_t slot = 0;
    uint64_t address;
    snx_link_find(&ld->got, t->key, &slot);
    address = ld->got_address + 8 * (uint64_t)slot;
    snx_link_put64(at(loader, address), t->address);
    return address;
}

/* The call of relocation type at p (address P, addend A) to t, which it cannot reach, through t's stub: the stub
   written and the call to it. 0, or SNX_LINK_RANGE when t has no stub */
static int call_stub(const SnxCodeLoader* loader, const Load* ld, const Target* t, unsigned char* p, size_t room,
                     unsigned type, int64_t A, uint64_t P) {
    size_t index;
    uint64_t address, slot;
    unsigned char* s;
    if (!t->key[0] || !snx_link_find(&ld->stubs, t->key, &index)) return SNX_LINK_RANGE;
    address = ld->stub_address + stub_size(ld) * (uint64_t)index;
    s = at(loader, address);
    slot = t->far ? got_slot(loader, ld, t) : 0;
    if (ld->machine == EM_AARCH64) {
        /* the stub goes to the symbol itself, shared by every call to it: a call to an offset from it has none */
        if (A) return SNX_LINK_RANGE;
        return snx_elf_branch_veneer(p, P, s, address, t->far ? slot : t->address, t->far);
    }
    s[0] = 0xFF; /* jmp qword ptr [rip + slot - next] */
    s[1] = 0x25;
    snx_link_rel32(s + 2, slot, address + 6); /* both in the arena: in reach */
    /* PLT32's addend is the call's own, its displacement's distance from the next instruction */
    return snx_elf_relocate(ld->machine, p, room, type, address, A, P, ld->got_address, 0);
}

/* PowerPC64's call (REL24, REL14) of relocation type at p (address P, addend A) to t: to its entry point, which keeps
   the TOC (ELFv2's local entry, ELFv1's code), directly or through t's veneer, which enters as a pointer call does
   (ELFv2's global entry, computing r2 from r12 again: the same TOC). A function outside the arena that shares its TOC
   (snx_code_loader_new_toc) is called as its address says: an ELFv2 global entry through the veneer, which sets r12,
   and an ELFv1 descriptor's code. 0 or what snx_elf_relocate returns */
static int ppc_call(const SnxCodeLoader* loader, const Load* ld, const Target* t, unsigned char* p, size_t room,
                    unsigned type, int64_t A, uint64_t P, char* error, size_t error_size) {
    uint64_t global = t->address + (uint64_t)A, local = global;
    size_t index, found;
    int written, veneer = 0;
    if (t->far && loader->toc_shared) {
        if (A) return SNX_LINK_RANGE; /* an entry is the function's own, not an offset from it */
        if (ld->abi == SNX_ELF_PPC_ELFV2)
            veneer = 1;
        else if (loader->machine == HOST_MACHINE) /* the descriptor is this process's: its first doubleword */
            memcpy(&local, (const void*)(size_t)t->address, sizeof(local)), global = local;
        else
            return SNX_LINK_RANGE;
    } else if (t->here) {
        if (snx_elf_ppc_entry(ld->objs, &ld->globals, ld->abi, t->def_object, t->def_index, t->address, A, &global,
                              &local, error, error_size))
            return -1;
    } else if (t->name && snx_link_find(&loader->entries, t->name, &found)) {
        if (A) return SNX_LINK_RANGE; /* an entry is the function's own, not an offset from it */
        local = (uint64_t)found;
        if (ld->abi != SNX_ELF_PPC_ELFV2) global = local; /* ELFv1: the descriptor's code */
    }
    written = veneer ? SNX_LINK_RANGE : snx_elf_relocate(EM_PPC64, p, room, type, local, 0, P, ld->toc, 0);
    if (written != SNX_LINK_RANGE || type != SNX_PPC_REL24 || !t->key[0] || !snx_link_find(&ld->stubs, t->key, &index))
        return written;
    return snx_elf_ppc_branch_veneer(p, P, at(loader, ld->stub_address + stub_size(ld) * (uint64_t)index),
                                     ld->stub_address + stub_size(ld) * (uint64_t)index, global, ld->toc);
}

/* Second pass: every relocation of the unit, written into its placed sections */
static int relocate(const SnxCodeLoader* loader, const Load* ld, char* error, size_t error_size) {
    size_t i, j, k, n;
    for (i = 0; i < ld->count; ++i) {
        const SnxElfObject* o = &ld->objs[i];
        for (j = 0; j < o->eh->e_shnum; ++j) {
            const Elf64_Shdr* s = relocations(o, j);
            const Elf64_Shdr* target;
            const Elf64_Rela* r;
            if (!s) continue;
            target = &o->sh[s->sh_info];
            r = (const Elf64_Rela*)(o->input->data + s->sh_offset);
            n = s->sh_size / sizeof(Elf64_Rela);
            for (k = 0; k < n; ++k) {
                unsigned type = ELF64_R_TYPE(r[k].r_info);
                uint64_t P = o->address[s->sh_info] + r[k].r_offset, slot = 0;
                unsigned char* p;
                Target t;
                int written = SNX_LINK_OUTSIDE;
                if (resolve(loader, ld, i, ELF64_R_SYM(r[k].r_info), &t, error, error_size)) return -1;
                if (r[k].r_offset <= target->sh_size) {
                    size_t room = (size_t)(target->sh_size - r[k].r_offset);
                    p = at(loader, P);
                    if (ld->machine == EM_PPC64 && snx_elf_call_relocation(ld->machine, type)) {
                        written = ppc_call(loader, ld, &t, p, room, type, r[k].r_addend, P, error, error_size);
                        if (written < 0) return -1; /* the reason in error */
                    } else {
                        if (snx_elf_got_relocation(ld->machine, type)) slot = got_slot(loader, ld, &t);
                        written = snx_elf_relocate(ld->machine, p, room, type, t.address, r[k].r_addend, P,
                                                   ld->machine == EM_PPC64 ? ld->toc : ld->got_address, slot);
                        if (written == SNX_LINK_RANGE && snx_elf_call_relocation(ld->machine, type))
                            written = call_stub(loader, ld, &t, p, room, type, r[k].r_addend, P);
                    }
                }
                if (written == SNX_LINK_OUTSIDE) {
                    snprintf(error, error_size, "%s: relocation outside its section", o->input->name);
                    return -1;
                }
                if (written == SNX_LINK_RANGE) {
                    snprintf(error, error_size, "%s: relocation %u out of range of %s", o->input->name, type,
                             t.far ? "a symbol outside the code arena" : "its target");
                    return -1;
                }
                if (written == SNX_LINK_MISALIGNED) {
                    snprintf(error, error_size, "%s: relocation %u to a misaligned address", o->input->name, type);
                    return -1;
                }
                if (written) {
                    snprintf(error, error_size, "%s: unsupported relocation type %u", o->input->name, type);
                    return -1;
                }
            }
        }
    }
    return 0;
}

/* Hands an attached GDB a copy of o, a load's object, with its loaded sections' sh_addr where the load put them
   (gdb_jit.c), for loader's free to take back */
static void gdb_register(const SnxCodeLoader* loader, const SnxElfObject* o) {
    unsigned char* copy = snx_gdb_jit_copy(loader, o->input->data, o->input->size);
    size_t i;
    if (!copy) return;
    for (i = 0; i < o->eh->e_shnum; ++i)
        if (o->kind[i] >= 0)
            memcpy(copy + o->eh->e_shoff + i * sizeof(Elf64_Shdr) + offsetof(Elf64_Shdr, sh_addr), &o->address[i],
                   sizeof(Elf64_Addr));
    snx_gdb_jit_add(copy);
}

/* snx_code_load, and snx_code_load_private when publish is 0 */
static int load(SnxCodeLoader* loader, const SnxLinkInput* inputs, size_t count, const char* const* names,
                size_t name_count, SnxU64* addresses, int publish, char* error, size_t error_size) {
    static const char* const region_names[REGIONS] = {"code", "read-only data", "data"};
    Load ld;
    uint64_t kind_size[SNX_ELF_KINDS], kind_align[SNX_ELF_KINDS], kind_address[SNX_ELF_KINDS];
    uint64_t start[REGIONS], end[REGIONS], limit;
    size_t i, j, found;
    unsigned abi = loader->abi; /* PowerPC64's: the first load's (0 takes the object's), every load's after it */
    int rc = -1, absolute, failed;
    if (loader->machine == SNX_CODE_COFF_X64)
        return snx_code_load_coff(loader, inputs, count, names, name_count, addresses, error, error_size);
    memset(&ld, 0, sizeof(ld));
    ld.count = count;
    ld.machine = loader->machine;
    ld.arena_start = (uint64_t)(size_t)loader->base;
    ld.arena_end = ld.arena_start + loader->reserved;
    ld.toc = loader->toc;
    ld.objs = (SnxElfObject*)calloc(count ? count : 1, sizeof(SnxElfObject));
    if (!ld.objs) {
        snprintf(error, error_size, "out of memory");
        return -1;
    }
    /* the parser takes an object's ABI only with its machine, which the loader names: the first load's from its
       first object's header, which the parser then checks */
    if (ld.machine == EM_PPC64 && !abi && count && inputs[0].size >= sizeof(Elf64_Ehdr)) {
        Elf64_Ehdr header;
        memcpy(&header, inputs[0].data, sizeof(header));
        abi = snx_elf_ppc_abi(header.e_flags);
    }
    for (i = 0; i < count; ++i)
        if (snx_elf_parse(&ld.objs[i], &inputs[i], &ld.machine, &abi, error, error_size)) goto done;
    ld.abi = abi;
    /* a private load's globals are its own: they may repeat an earlier load's, which they come before */
    if (snx_elf_collect_globals(ld.objs, count, publish ? &loader->symbols : NULL, &ld.globals, error, error_size))
        goto done;
    for (i = 0; i < name_count; ++i)
        if (!snx_link_find(&ld.globals, names[i], &found) && !snx_link_find(&loader->symbols, names[i], &found)) {
            snprintf(error, error_size, "symbol %s is not defined", names[i]);
            goto done;
        }
    if (scan(loader, &ld, error, error_size)) goto done;
    /* the unit's parts, each at its region's next free byte: [code, stubs] [read-only data, GOT] [data, bss] */
    snx_elf_layout(ld.objs, count, kind_size, kind_align);
    start[CODE] =
        align_up(next_free(loader, CODE), kind_align[SNX_ELF_TEXT] > CODE_LINE ? kind_align[SNX_ELF_TEXT] : CODE_LINE);
    kind_address[SNX_ELF_TEXT] = start[CODE];
    ld.stub_address = align_up(start[CODE] + kind_size[SNX_ELF_TEXT], 8);
    end[CODE] = ld.stub_address + stub_size(&ld) * (uint64_t)ld.stub_count;
    start[RODATA] = align_up(next_free(loader, RODATA), kind_align[SNX_ELF_RODATA]);
    kind_address[SNX_ELF_RODATA] = start[RODATA];
    ld.got_address = align_up(start[RODATA] + kind_size[SNX_ELF_RODATA], 8);
    end[RODATA] = ld.got_address + 8 * (uint64_t)ld.got_count;
    start[DATA] = align_up(next_free(loader, DATA), kind_align[SNX_ELF_DATA]);
    kind_address[SNX_ELF_DATA] = start[DATA];
    kind_address[SNX_ELF_BSS] = align_up(start[DATA] + kind_size[SNX_ELF_DATA], kind_align[SNX_ELF_BSS]);
    end[DATA] = kind_address[SNX_ELF_BSS] + kind_size[SNX_ELF_BSS];
    for (i = 0; i < REGIONS; ++i) {
        limit = ld.arena_start + loader->region[i].start + loader->region[i].size;
        if (end[i] > limit) {
            snprintf(error, error_size, "the code arena's %s is full (%lu KiB)", region_names[i],
                     (unsigned long)(loader->region[i].size >> 10));
            goto done;
        }
    }
    if (snx_code_commit(loader, end[DATA], error, error_size)) goto done;
    /* Written through the read-write views (on macOS the arena itself, writable to this thread meanwhile), from each
       region's first free byte: a failed load leaves its bytes for the next to write over, since no address of theirs
       went out and nothing runs there. Between functions and in unused stubs, an instruction that traps: int3 on
       x86-64, UDF #0 (all zero bits) on AArch64 */
    snx_code_writable(1);
    memset(at(loader, next_free(loader, CODE)), ld.machine == EM_AARCH64 || ld.machine == EM_PPC64 ? 0 : 0xCC,
           (size_t)(end[CODE] - next_free(loader, CODE)));
    memset(at(loader, next_free(loader, RODATA)), 0, (size_t)(end[RODATA] - next_free(loader, RODATA)));
    memset(at(loader, next_free(loader, DATA)), 0, (size_t)(end[DATA] - next_free(loader, DATA)));
    for (i = 0; i < count; ++i) {
        SnxElfObject* o = &ld.objs[i];
        for (j = 0; j < o->eh->e_shnum; ++j) {
            if (o->kind[j] < 0) continue;
            o->address[j] = kind_address[o->kind[j]] + o->position[j];
            if (o->kind[j] != SNX_ELF_BSS && o->sh[j].sh_size)
                memcpy(at(loader, o->address[j]), o->input->data + o->sh[j].sh_offset, o->sh[j].sh_size);
        }
    }
    failed = relocate(loader, &ld, error, error_size);
    snx_code_writable(0);
    if (failed || snx_code_publish(loader, start[CODE], end[CODE], error, error_size)) goto done;
    for (i = 0; i < name_count; ++i)
        if (snx_link_find(&ld.globals, names[i], &found))
            addresses[i] = snx_elf_symbol_address(ld.objs, found >> 32, found & 0xFFFFFFFFu, &absolute);
        else
            snx_code_lookup(loader, names[i], &addresses[i]);
    for (i = 0; publish && i < ld.globals.capacity; ++i)
        if (ld.globals.keys[i]) {
            size_t object = ld.globals.values[i] >> 32, index = ld.globals.values[i] & 0xFFFFFFFFu;
            uint64_t address = snx_elf_symbol_address(ld.objs, object, index, &absolute);
            snx_link_insert(&loader->symbols, ld.globals.keys[i], (size_t)address);
#ifdef __powerpc64__
            uint64_t global, local;
            /* where later loads' branches enter a function (code_loader_int.h) */
            if (ld.machine == EM_PPC64 && ELF64_ST_TYPE(ld.objs[object].syms[index].st_info) == STT_FUNC &&
                !snx_elf_ppc_entry(ld.objs, &ld.globals, ld.abi, object, index, address, 0, &global, &local, error,
                                   error_size))
                snx_link_insert(&loader->entries, ld.globals.keys[i], (size_t)local);
#endif
        }
    error[0] = 0;
    loader->abi = abi;
    /* a region the unit put nothing into keeps its next free byte, so an empty load uses nothing */
    for (i = 0; i < REGIONS; ++i)
        if (end[i] > start[i]) loader->region[i].used = (size_t)(end[i] - ld.arena_start - loader->region[i].start);
    /* another machine's objects (a test's) run nowhere here, nor does GDB debug them */
    if (loader->machine == HOST_MACHINE)
        for (i = 0; i < count; ++i) gdb_register(loader, &ld.objs[i]);
    rc = 0;
done:
    for (i = 0; i < count; ++i) snx_elf_free(&ld.objs[i]);
    free(ld.objs);
    snx_link_names_free(&ld.globals);
    snx_link_names_free(&ld.got);
    snx_link_names_free(&ld.stubs);
    return rc;
}
int snx_code_load(SnxCodeLoader* loader, const SnxLinkInput* inputs, size_t count, const char* const* names,
                  size_t name_count, SnxU64* addresses, char* error, size_t error_size) {
    return load(loader, inputs, count, names, name_count, addresses, 1, error, error_size);
}
int snx_code_load_private(SnxCodeLoader* loader, const SnxLinkInput* inputs, size_t count, const char* const* names,
                          size_t name_count, SnxU64* addresses, char* error, size_t error_size) {
    if (loader->machine == SNX_CODE_COFF_X64) {
        snprintf(error, error_size, "the JIT loader loads COFF objects with their globals published");
        return -1;
    }
    return load(loader, inputs, count, names, name_count, addresses, 0, error, error_size);
}
#else
int snx_code_load(SnxCodeLoader* loader, const SnxLinkInput* inputs, size_t count, const char* const* names,
                  size_t name_count, SnxU64* addresses, char* error, size_t error_size) {
    if (loader->machine == SNX_CODE_COFF_X64)
        return snx_code_load_coff(loader, inputs, count, names, name_count, addresses, error, error_size);
    snprintf(error, error_size, "the JIT loader reads COFF objects on Windows");
    return -1;
}
int snx_code_load_private(SnxCodeLoader* loader, const SnxLinkInput* inputs, size_t count, const char* const* names,
                          size_t name_count, SnxU64* addresses, char* error, size_t error_size) {
    (void)loader, (void)inputs, (void)count, (void)names, (void)name_count, (void)addresses;
    snprintf(error, error_size, "the JIT loader loads COFF objects with their globals published");
    return -1;
}
#endif
