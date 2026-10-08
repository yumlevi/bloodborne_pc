/* Minimal x86-64 native loader experiment. Not a PS4 emulator or game port. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "runtime.h"
#include "gpu/bbgpu.h"
#if !defined(__x86_64__) || !defined(__GNUC__)
#error This prototype requires x86-64 GCC or Clang (including MinGW).
#endif
#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#include <process.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <wchar.h>
#else
#include <sys/mman.h>
#include <malloc.h>
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#endif

typedef struct { uint64_t address, size, flags; } Segment;
typedef struct { uint64_t target, kind, value, addend; } Reloc;
static char (*names)[128];
static uint64_t import_count;
static unsigned char *image;
static size_t page_size;
typedef struct { uint64_t base, size, init, tls_address, tls_memsz, tls_filesz, tls_module; } LinkedModule;
static LinkedModule modules[16];
static uint64_t module_count;
static int entered_game;
static int gpu_enabled;
int vulkan_smoke(void);

static __attribute__((noreturn)) void fail(const char *message) { fprintf(stderr, "ERROR: %s\n", message); exit(1); }
static uint64_t read64(FILE *f) {
    unsigned char b[8];
    if (fread(b, 1, 8, f) != 8) fail("truncated boot file");
    uint64_t n = 0;
    for (int i = 7; i >= 0; --i) n = (n << 8) | b[i];
    return n;
}
static size_t round_page(size_t size) { return (size + page_size - 1) & ~(page_size - 1); }
static void *allocate(size_t size) {
    void *low=runtime_low_map(size,PROT_READ|PROT_WRITE);
    if (low) return low;
#ifdef _WIN32
    void *p = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!p) fail("VirtualAlloc failed");
#else
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) fail("mmap failed");
#endif
    return p;
}
static void protect(void *p, size_t size, unsigned flags) {
#ifdef _WIN32
    DWORD old, mode = PAGE_NOACCESS;
    if (flags & 1) mode = (flags & 2) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    else if (flags & 2) mode = PAGE_READWRITE;
    else if (flags & 4) mode = PAGE_READONLY;
    if (!VirtualProtect(p, size, mode, &old)) fail("VirtualProtect failed");
    FlushInstructionCache(GetCurrentProcess(), p, size);
#else
    int mode = ((flags & 4) ? PROT_READ : 0) | ((flags & 2) ? PROT_WRITE : 0) | ((flags & 1) ? PROT_EXEC : 0);
    if (mprotect(p, size, mode)) fail("mprotect failed");
#endif
}
static ABI __attribute__((noreturn)) void unresolved(uint32_t id, uintptr_t argument) {
    if (id >= import_count) fail("bad import trap index");
    printf("STOP: first unsupported PS4 import: %s (index %u)\n", names[id], id);
    printf("API: %s\n", runtime_import_name(names[id]));
    printf("Caller return offset: 0x%" PRIxPTR "; first argument: 0x%" PRIxPTR "\n",
           (uintptr_t)__builtin_return_address(0) - (uintptr_t)image, argument);
    uintptr_t caller=(uintptr_t)__builtin_return_address(0)-(uintptr_t)image;
    for (uint64_t m=0;m<module_count;++m)
        if (caller>=modules[m].base && caller-modules[m].base<modules[m].size)
            printf("Caller in linked module %" PRIu64 " (%s): +0x%" PRIxPTR "\n",m,m==0 ? "libc.prx" : "system module",caller-modules[m].base);
    runtime_report();
    puts(entered_game ? "Original guest entry instructions executed; game initialization is incomplete." :
                        "Native libc initialization is incomplete; game entry has not run.");
    fflush(NULL);
    _exit(20); /* no destructors: GPU, audio and guest threads are still running */
}
#ifndef _WIN32
/* enter_on_stack(entry, arg0, arg1, stack_top): call entry(arg0,arg1) on a new stack. */
void enter_on_stack(void *entry, void *arg0, void *arg1, void *top);
__asm__(".text\n.globl enter_on_stack\nenter_on_stack:\n"
        " push %rbp\n mov %rsp,%rbp\n and $-16,%rcx\n mov %rcx,%rsp\n"
        " mov %rdi,%rax\n mov %rsi,%rdi\n mov %rdx,%rsi\n call *%rax\n"
        " mov %rbp,%rsp\n pop %rbp\n ret\n");
#endif
static ABI void guest_exit(void) { puts("Runtime: process finalizer callback reached"); }
#ifndef _WIN32
static void fault(int sig, siginfo_t *info, void *context) {
    /* GPU page tracking (write-protected guest pages) is resolved first. */
    if (gpu_enabled && sig == SIGSEGV && bbgpu_handle_fault(context, info->si_addr)) return;
    /* A speculative guest memory read (runtime_memory.c) failed: resume its recovery point. */
    if ((sig == SIGSEGV || sig == SIGBUS) && runtime_fault_recover) {
        sigjmp_buf *recover = runtime_fault_recover;
        runtime_fault_recover = NULL;
        sigset_t unblock;
        sigemptyset(&unblock);
        sigaddset(&unblock, sig);
        pthread_sigmask(SIG_UNBLOCK, &unblock, NULL);
        siglongjmp(*recover, 1);
    }
    /* The process is terminating: dladdr/snprintf are acceptable here. */
    ucontext_t *uc = context;
    uintptr_t rip = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    char line[512];
    Dl_info where;
    if (rip - (uintptr_t)image < 0x10000000)
        snprintf(line, sizeof(line), "Guest fault (signal %d) at guest offset 0x%lx, address %p\n",
                 sig, (unsigned long)(rip - (uintptr_t)image), info->si_addr);
    else if (dladdr((void *)rip, &where) && where.dli_fname)
        snprintf(line, sizeof(line), "Host fault (signal %d) in %s+0x%lx (%s), address %p\n", sig, where.dli_fname,
                 (unsigned long)(rip - (uintptr_t)where.dli_fbase), where.dli_sname ? where.dli_sname : "?", info->si_addr);
    else
        snprintf(line, sizeof(line), "Fault (signal %d) at RIP %p, address %p\n", sig, (void *)rip, info->si_addr);
    { ssize_t written_=write(2, line, strlen(line)); (void)written_; }
    if (gpu_enabled) bbgpu_dump_guest_writes(context);
    /* Host call chain (frames with unwind info; guest frames end it). */
    void *frames[32];
    int depth = backtrace(frames, 32);
    for (int i = 2; i < depth; ++i) {
        if (dladdr(frames[i], &where) && where.dli_fname)
            snprintf(line, sizeof(line), "  #%d %s+0x%lx (%s)\n", i, where.dli_fname,
                     (unsigned long)((uintptr_t)frames[i] - (uintptr_t)where.dli_fbase), where.dli_sname ? where.dli_sname : "?");
        else
            snprintf(line, sizeof(line), "  #%d %p\n", i, frames[i]);
        ssize_t written_=write(2, line, strlen(line)); (void)written_;
    }
    _exit(128 + sig);
}
#endif
#ifdef _WIN32
/* Windows: one vectored handler takes the roles of the SIGSEGV handler. GPU page tracking
 * first, then the recovery point of a speculative guest memory read, then crash reports for
 * faults in guest code (it has no unwind data, so frame-based handlers cannot see it). */
static unsigned char *traps_base;
static size_t traps_size, image_size;
static void recover_jump(void) {
    RuntimeRecoverBuf *recover = runtime_fault_recover;
    runtime_fault_recover = NULL;
    RUNTIME_RECOVER_JUMP(*recover);
}
static int guest_address(uintptr_t a) {
    return (image && a - (uintptr_t)image < image_size) || (traps_base && a - (uintptr_t)traps_base < traps_size);
}
/* Module+offset without the loader lock (another thread may hold it while this one crashed). */
static void describe(char *out, size_t size, uintptr_t a) {
    MEMORY_BASIC_INFORMATION info;
    char path[MAX_PATH];
    if (image && a - (uintptr_t)image < image_size) snprintf(out, size, "guest+0x%llx", (unsigned long long)(a - (uintptr_t)image));
    else if (VirtualQuery((void *)a, &info, sizeof(info)) && info.Type == MEM_IMAGE &&
             GetMappedFileNameA(GetCurrentProcess(), (void *)a, path, sizeof(path))) {
        const char *name = strrchr(path, '\\');
        snprintf(out, size, "%s+0x%llx", name ? name + 1 : path, (unsigned long long)(a - (uintptr_t)info.AllocationBase));
    } else snprintf(out, size, "0x%llx", (unsigned long long)a);
}
static const char *exception_name(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_INT_OVERFLOW: return "integer overflow";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page I/O error";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "misaligned data";
    case EXCEPTION_BREAKPOINT: return "breakpoint";
    default: return "unknown exception";
    }
}
/* No DLL detach or CRT teardown: other threads may hold the locks those need. */
static __attribute__((noreturn)) void terminate(unsigned code) {
    fflush(NULL);
    TerminateProcess(GetCurrentProcess(), code);
    _exit((int)code);
}
static void report_exception(EXCEPTION_POINTERS *e) {
    static volatile LONG reporting;
    if (InterlockedExchange(&reporting, 1)) { Sleep(INFINITE); }
    EXCEPTION_RECORD *r = e->ExceptionRecord;
    CONTEXT *c = e->ContextRecord;
    char where[512];
    describe(where, sizeof(where), (uintptr_t)c->Rip);
    SYSTEMTIME utc;
    GetSystemTime(&utc);
    fprintf(stderr, "\n=== bbport crash report ===\n");
    fprintf(stderr, "UTC %04u-%02u-%02u %02u:%02u:%02u.%03u  pid=%lu tid=%lu\n",
            utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond,
            utc.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId());
    fprintf(stderr, "%s fault 0x%08lx (%s) at %s",
            guest_address((uintptr_t)c->Rip) ? "Guest" : "Host", r->ExceptionCode,
            exception_name(r->ExceptionCode), where);
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
        fprintf(stderr, ", %s address %p", r->ExceptionInformation[0] == 1 ? "writing" : r->ExceptionInformation[0] == 8 ? "executing" : "reading",
                (void *)r->ExceptionInformation[1]);
    PWSTR description = NULL;
    char thread_name[64] = "";
    if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &description)) && description) {
        WideCharToMultiByte(CP_UTF8, 0, description, -1, thread_name, sizeof(thread_name), NULL, NULL);
        LocalFree(description);
    }
    fprintf(stderr, " (thread %lu %s)\n", GetCurrentThreadId(), thread_name);
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
        MEMORY_BASIC_INFORMATION fault_info;
        const void *address = (const void *)r->ExceptionInformation[1];
        if (VirtualQuery(address, &fault_info, sizeof(fault_info))) {
            fprintf(stderr, "  target region: base=%p allocation=%p size=0x%llx state=0x%lx protect=0x%lx type=0x%lx\n",
                    fault_info.BaseAddress, fault_info.AllocationBase,
                    (unsigned long long)fault_info.RegionSize, fault_info.State,
                    fault_info.Protect, fault_info.Type);
        } else fprintf(stderr, "  target region: VirtualQuery failed (%lu)\n", GetLastError());
    }
    fprintf(stderr, "  rip=%016llx eflags=%08lx exception_address=%p parameters=%lu\n",
            c->Rip, c->EFlags, r->ExceptionAddress, r->NumberParameters);
    fprintf(stderr, "  rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx\n  rsi=%016llx rdi=%016llx rbp=%016llx rsp=%016llx\n",
            c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi, c->Rbp, c->Rsp);
    fprintf(stderr, "  r8 =%016llx r9 =%016llx r10=%016llx r11=%016llx\n"
                    "  r12=%016llx r13=%016llx r14=%016llx r15=%016llx\n",
            c->R8, c->R9, c->R10, c->R11, c->R12, c->R13, c->R14, c->R15);
    /* rbp frame chain; ReadProcessMemory so a bad frame cannot fault again. */
    uintptr_t rbp = c->Rbp;
    for (int depth = 0; depth < 24 && rbp; ++depth) {
        uintptr_t frame[2];
        SIZE_T n = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (void *)rbp, frame, sizeof(frame), &n) || n != sizeof(frame) || frame[0] <= rbp) break;
        describe(where, sizeof(where), frame[1]);
        fprintf(stderr, "  #%d %s\n", depth, where);
        rbp = frame[0];
    }
    /* The host call chain from unwind data (the rbp chain above often stops in host code). */
    void *frames[48];
    USHORT depth = RtlCaptureStackBackTrace(0, 48, frames, NULL);
    for (USHORT i = 0; i < depth; ++i) {
        describe(where, sizeof(where), (uintptr_t)frames[i]);
        fprintf(stderr, "  host #%u %s\n", i, where);
    }
    if (gpu_enabled && r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) bbgpu_dump_guest_writes(e);
    fprintf(stderr, "=== end crash report; launcher exit code 139 ===\n");
    fflush(NULL);
}
/* Thread dump on request: SetEvent on "Local\bbport-dump-<pid>" prints every thread's call
 * chain. Threads are suspended only to copy their registers (a suspended thread may hold the
 * heap or loader lock); the stacks are walked afterwards with a fault recovery point. Host
 * frames unwind by their unwind data, guest frames by the rbp chain. */
enum { DUMP_THREADS = 512 };
static struct { DWORD id; CONTEXT context; } dumped[DUMP_THREADS];
static void dump_chain(CONTEXT c) {
    char where[512];
    for (int depth = 0; depth < 32 && c.Rip; ++depth) {
        describe(where, sizeof(where), (uintptr_t)c.Rip);
        fprintf(stderr, "  #%d %s\n", depth, where);
        DWORD64 base = 0;
        RUNTIME_FUNCTION *function = guest_address((uintptr_t)c.Rip) ? NULL : RtlLookupFunctionEntry(c.Rip, &base, NULL);
        if (function) {
            void *handler_data;
            DWORD64 frame;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, function, &c, &handler_data, &frame, NULL);
        } else if (guest_address((uintptr_t)c.Rip) && c.Rbp > c.Rsp) {
            uintptr_t *frame = (uintptr_t *)c.Rbp; /* [rbp] caller's rbp, [rbp+8] return address */
            c.Rip = frame[1]; c.Rsp = c.Rbp + 16; c.Rbp = frame[0];
        } else {
            c.Rip = *(DWORD64 *)c.Rsp; c.Rsp += 8; /* leaf without unwind data (JIT code, thunks) */
        }
    }
}
static unsigned __stdcall dump_thread(void *event) {
    for (;;) {
        WaitForSingleObject(event, INFINITE);
        DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
        unsigned count = 0;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 entry = {.dwSize = sizeof(entry)};
        for (BOOL more = Thread32First(snapshot, &entry); more && count < DUMP_THREADS; more = Thread32Next(snapshot, &entry)) {
            if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self) continue;
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, entry.th32ThreadID);
            if (!thread) continue;
            dumped[count].id = entry.th32ThreadID;
            dumped[count].context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            if (SuspendThread(thread) != (DWORD)-1) {
                if (GetThreadContext(thread, &dumped[count].context)) ++count;
                ResumeThread(thread);
            }
            CloseHandle(thread);
        }
        CloseHandle(snapshot);
        fprintf(stderr, "Thread dump: %u threads\n", count);
        for (unsigned i = 0; i < count; ++i) {
            char name[64] = "";
            HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, dumped[i].id);
            PWSTR description = NULL;
            if (thread && SUCCEEDED(GetThreadDescription(thread, &description)) && description) {
                WideCharToMultiByte(CP_UTF8, 0, description, -1, name, sizeof(name), NULL, NULL);
                LocalFree(description);
            }
            if (thread) CloseHandle(thread);
            CONTEXT *c = &dumped[i].context;
            fprintf(stderr, "thread %lu %s: rcx=%llx rdx=%llx rdi=%llx rsi=%llx\n", dumped[i].id, name, c->Rcx, c->Rdx, c->Rdi, c->Rsi);
            RuntimeRecoverBuf recover;
            if (!RUNTIME_RECOVER_SET(recover)) {
                runtime_fault_recover = &recover;
                dump_chain(*c);
            } else fprintf(stderr, "  (bad frame)\n");
            runtime_fault_recover = NULL;
        }
        fprintf(stderr, "Thread dump end\n");
        fflush(NULL);
    }
    return 0;
}
static void start_dump_thread(void) {
    wchar_t name[64];
    swprintf(name, 64, L"Local\\bbport-dump-%lu", GetCurrentProcessId());
    HANDLE event = CreateEventW(NULL, FALSE, FALSE, name);
    if (event) CloseHandle((HANDLE)_beginthreadex(NULL, 0, dump_thread, event, 0, NULL));
}
/* BB_HW_WATCH=<file holding a hex address, "r" first for reads too>: a hardware watchpoint (DR0, 4 bytes) on every
 * thread. The file is re-read and the threads set again every 200 ms (new threads, new address);
 * each distinct writing instruction is printed once per address. */
static volatile uintptr_t hw_watch;
static volatile char hw_mode = 'w';
static const char *hw_watch_file;
static volatile LONG hw_hits;
static uintptr_t hw_rips[64];
static void hw_watch_hit(CONTEXT *c) {
    uintptr_t rip = (uintptr_t)c->Rip;
    LONG n = hw_hits < 64 ? hw_hits : 64;
    for (LONG i = 0; i < n; ++i) if (hw_rips[i] == rip) return;
    LONG slot = InterlockedIncrement(&hw_hits) - 1;
    if (slot >= 64) return;
    hw_rips[slot] = rip;
    char where[512];
    describe(where, sizeof(where), rip);
    fprintf(stderr, "HW watch: write to %#llx by %s (after the store), now %08x, thread %lu\n"
            "  rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx r8=%llx r12=%llx r13=%llx r14=%llx r15=%llx\n",
            (unsigned long long)hw_watch, where, *(volatile unsigned *)hw_watch, GetCurrentThreadId(),
            c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi, c->R8, c->R12, c->R13, c->R14, c->R15);
    uintptr_t rbp = c->Rbp;
    for (int depth = 0; depth < 6 && rbp > c->Rsp; ++depth) {
        uintptr_t frame[2];
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (void *)rbp, frame, sizeof(frame), &got) || got != sizeof(frame) || frame[0] <= rbp) break;
        describe(where, sizeof(where), frame[1]);
        fprintf(stderr, "  #%d %s\n", depth, where);
        rbp = frame[0];
    }
    fflush(stderr);
}
static unsigned __stdcall hw_watch_thread(void *unused) {
    (void)unused;
    for (;;) {
        unsigned long long address = 0;
        char mode = 'w';
        FILE *f = fopen(hw_watch_file, "r");
        if (f) {
            int c = fgetc(f);
            if (c == 'r' || c == 'x') mode = (char)c; else if (c != EOF) ungetc(c, f);
            if (fscanf(f, "%llx", &address) != 1) address = 0;
            fclose(f);
        }
        if (address != hw_watch || mode != hw_mode) {
            hw_hits = 0; hw_mode = mode; hw_watch = (uintptr_t)address;
            fprintf(stderr, "HW watch: %#llx (%s)\n", address, mode == 'r' ? "reads and writes" : "writes");
        }
        DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 entry = {.dwSize = sizeof(entry)};
        for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
            if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self) continue;
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, entry.th32ThreadID);
            if (!thread) continue;
            if (SuspendThread(thread) != (DWORD)-1) {
                CONTEXT c = {.ContextFlags = CONTEXT_DEBUG_REGISTERS};
                /* L0; execute (RW=00, LEN=00), write (01) or read/write (11) of 4 bytes */
                DWORD64 dr7 = !hw_watch ? 0 : hw_mode == 'x' ? 1 : 1 | ((hw_mode == 'r' ? 3u : 1u) << 16) | (3u << 18);
                if (GetThreadContext(thread, &c) && (c.Dr0 != hw_watch || (c.Dr7 & 0xf0003) != dr7)) {
                    c.Dr0 = hw_watch;
                    c.Dr7 = (c.Dr7 & ~(DWORD64)0xf0003) | dr7;
                    SetThreadContext(thread, &c);
                }
                ResumeThread(thread);
            }
            CloseHandle(thread);
        }
        CloseHandle(snapshot);
        Sleep(200);
    }
    return 0;
}
static void start_hw_watch(void) {
    if (!(hw_watch_file = getenv("BB_HW_WATCH"))) return;
    CloseHandle((HANDLE)_beginthreadex(NULL, 0, hw_watch_thread, NULL, 0, NULL));
}
static int fatal(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: case EXCEPTION_ILLEGAL_INSTRUCTION: case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO: case EXCEPTION_INT_OVERFLOW: case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_IN_PAGE_ERROR: case EXCEPTION_DATATYPE_MISALIGNMENT: case EXCEPTION_BREAKPOINT:
        return 1;
    default:
        return 0;
    }
}
static LONG CALLBACK vectored_handler(EXCEPTION_POINTERS *e) {
    EXCEPTION_RECORD *r = e->ExceptionRecord;
    if (r->ExceptionCode == EXCEPTION_SINGLE_STEP && hw_watch_file && (e->ContextRecord->Dr6 & 1)) {
        /* A thread may still hold the previous address (or a cleared watch) until the watch
         * thread updates it: report only hits on the current address. */
        if (hw_watch && e->ContextRecord->Dr0 == hw_watch) hw_watch_hit(e->ContextRecord);
        e->ContextRecord->Dr6 = 0;
        /* An execute breakpoint (this thread's DR0 RW bits 00) fires before the instruction: RF
         * lets it run. Decided per thread, which may still hold an older mode. */
        if (!(e->ContextRecord->Dr7 & (3u << 16))) e->ContextRecord->EFlags |= 0x10000;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
        if (gpu_enabled && bbgpu_handle_fault(e, (void *)r->ExceptionInformation[1])) return EXCEPTION_CONTINUE_EXECUTION;
        if (runtime_fault_recover) {
            /* Resume in recover_jump as if called there, below the interrupted frame's red zone. */
            CONTEXT *c = e->ContextRecord;
            c->Rsp = ((c->Rsp - 512) & ~(DWORD64)15) - 8;
            c->Rip = (DWORD64)(uintptr_t)recover_jump;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    /* Host code may handle its own exceptions (drivers probe memory under __try). */
    if (!fatal(r->ExceptionCode) || !guest_address((uintptr_t)e->ContextRecord->Rip)) return EXCEPTION_CONTINUE_SEARCH;
    report_exception(e);
    terminate(128 + 11);
}
static LONG WINAPI unhandled_filter(EXCEPTION_POINTERS *e) {
    report_exception(e);
    terminate(128 + 11);
}
/* link_modules.py rewrote the guest's `mov rax, fs:[0]` into `mov rax, gs:[0]`. GS is the TEB
 * on Windows, and the thread's guest TCB is in a TLS slot (runtime_thread.c). A slot below 64
 * is one load: the displacement becomes TEB.TlsSlots[slot]. A later slot (the DLLs loaded
 * before main took the first 64) needs two loads, through TEB.TlsExpansionSlots: the site
 * jumps to a stub of its own (no call: the guest may keep data below rsp) and back. */
static uint64_t patch_tls_reads(const Segment *segments, uint64_t count) {
    static const unsigned char gs_load[9] = {0x65, 0x48, 0x8b, 0x04, 0x25, 0, 0, 0, 0};
    const uint32_t slot = runtime_win_tls_slot();
    unsigned char **sites = NULL;
    size_t found = 0, capacity = 0;
    for (uint64_t i = 0; i < count; ++i) {
        if (!(segments[i].flags & 1) || segments[i].size < sizeof(gs_load)) continue;
        unsigned char *at = image + segments[i].address, *last = at + segments[i].size - sizeof(gs_load);
        while (at <= last && (at = memchr(at, 0x65, (size_t)(last - at) + 1))) {
            if (memcmp(at, gs_load, sizeof(gs_load))) { ++at; continue; }
            if (found == capacity) {
                capacity = capacity ? capacity * 2 : 4096;
                if (!(sites = realloc(sites, capacity * sizeof(*sites)))) fail("allocation failed");
            }
            sites[found++] = at;
            at += sizeof(gs_load);
        }
    }
    if (slot < 64) {
        uint32_t displacement = 0x1480 + slot * 8;
        for (size_t i = 0; i < found; ++i) memcpy(sites[i] + 5, &displacement, 4);
    } else if (found) {
        enum { STUB = 21 };
        static const unsigned char expansion[9] = {0x65, 0x48, 0x8b, 0x04, 0x25, 0x80, 0x17, 0, 0}; /* mov rax, gs:[0x1780] */
        size_t bytes = round_page(found * STUB);
        unsigned char *stubs = allocate(bytes);
        uint32_t index = (slot - 64) * 8;
        for (size_t i = 0; i < found; ++i) {
            unsigned char *stub = stubs + i * STUB, *site = sites[i];
            int64_t back = (site + 9) - (stub + STUB), there = stub - (site + 5);
            if (back != (int32_t)back || there != (int32_t)there) fail("TLS stub out of jump range");
            int32_t back32 = (int32_t)back, there32 = (int32_t)there;
            memcpy(stub, expansion, 9);
            stub[9] = 0x48; stub[10] = 0x8b; stub[11] = 0x80; memcpy(stub + 12, &index, 4); /* mov rax, [rax + index] */
            stub[16] = 0xe9; memcpy(stub + 17, &back32, 4);                                 /* jmp site + 9 */
            site[0] = 0xe9; memcpy(site + 1, &there32, 4); memset(site + 5, 0x90, 4);         /* jmp stub */
        }
        protect(stubs, bytes, 5);
    }
    free(sites);
    return found;
}
#else
/* Watchdog: dump RIP and the rbp frame chain of every thread (guest offsets
 * when inside the image). Reads use process_vm_readv so bad frames cannot fault. */
static uintptr_t exe_base;
static void write_hex(char *out, uint64_t v) {
    const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) out[i] = digits[(v >> (60 - i * 4)) & 15];
}
static void dump_frames(ucontext_t *uc) {
    char line[] = "  tid=0000000000000000 rip=0000000000000000 image-relative=0000000000000000 host-relative=0000000000000000\n";
    uintptr_t rip=(uintptr_t)uc->uc_mcontext.gregs[REG_RIP], rbp=(uintptr_t)uc->uc_mcontext.gregs[REG_RBP];
    uint64_t tid=(uint64_t)gettid();
    /* First argument register: the lock address when a thread waits on a futex. */
    char arg[]="  tid=0000000000000000 rdi=0000000000000000\n";
    write_hex(arg+6,tid); write_hex(arg+27,(uint64_t)uc->uc_mcontext.gregs[REG_RDI]);
    { ssize_t written_=write(2,arg,sizeof(arg)-1); (void)written_; }
    for (int depth=0; depth<24; ++depth) {
        write_hex(line+6,tid); write_hex(line+27,rip); write_hex(line+59,rip-(uintptr_t)image); write_hex(line+90,rip-exe_base);
        { ssize_t written_=write(2,line,sizeof(line)-1); (void)written_; }
        uintptr_t frame[2];
        struct iovec local={frame,sizeof(frame)}, remote={(void *)rbp,sizeof(frame)};
        if (!rbp || process_vm_readv(getpid(),&local,1,&remote,1,0)!=(ssize_t)sizeof(frame)) break;
        if (frame[0]<=rbp) break;
        rbp=frame[0]; rip=frame[1];
    }
}
static void thread_dump(int sig, siginfo_t *info, void *context) { (void)sig; (void)info; dump_frames(context); }
static void watchdog(int sig, siginfo_t *info, void *context) {
    (void)info;
    const char head[]="STOP: watchdog timeout; thread stacks:\n";
    { ssize_t written_=write(2,head,sizeof(head)-1); (void)written_; }
    dump_frames(context);
    int dir=open("/proc/self/task",O_RDONLY|O_DIRECTORY);
    char buffer[4096];
    long n;
    pid_t self=gettid();
    while (dir>=0 && (n=syscall(SYS_getdents64,dir,buffer,sizeof(buffer)))>0)
        for (long at=0; at<n;) {
            struct { uint64_t ino; int64_t off; unsigned short reclen; unsigned char type; char name[]; } *d=(void *)(buffer+at);
            pid_t tid=(pid_t)strtol(d->name,NULL,10);
            if (tid>0 && tid!=self) { syscall(SYS_tgkill,getpid(),tid,SIGUSR2); usleep(20000); }
            at+=d->reclen;
        }
    usleep(100000);
    _exit(128 + sig);
}
#endif
/* param.sfo lookup: string or integer value of key, 0 when absent. */
static int sfo_value(const char *path, const char *key, char *text, size_t text_size, uint32_t *number) {
    FILE *f=fopen(path,"rb");
    if (!f) return 0;
    unsigned char data[65536];
    size_t n=fread(data,1,sizeof(data),f); fclose(f);
    if (n<20 || memcmp(data,"\0PSF",4)) return 0;
    uint32_t keys, values, count;
    memcpy(&keys,data+8,4); memcpy(&values,data+12,4); memcpy(&count,data+16,4);
    for (uint32_t i=0;i<count && 20+i*16+16<=n;++i) {
        const unsigned char *e=data+20+i*16;
        uint16_t key_offset, format; uint32_t length, offset;
        memcpy(&key_offset,e,2); memcpy(&format,e+2,2); memcpy(&length,e+4,4); memcpy(&offset,e+12,4);
        if (keys+key_offset>=n || values+offset+length>n || strcmp((const char *)data+keys+key_offset,key)) continue;
        if (format==0x0404 && number && length>=4) { memcpy(number,data+values+offset,4); return 1; }
        if (text && text_size) {
            size_t copy=length<text_size-1 ? length : text_size-1;
            memcpy(text,data+values+offset,copy); text[copy]=0;
        }
        return 1;
    }
    return 0;
}
static int mapped(Segment *segments, uint64_t count, uint64_t address, uint64_t bytes) {
    for (uint64_t i = 0; i < count; ++i)
        if (address >= segments[i].address && bytes <= segments[i].size &&
            address - segments[i].address <= segments[i].size - bytes) return 1;
    return 0;
}
/* BBPATCH2 (patches.py): the patches' image base, then byte writes at image offsets, applied
 * after relocation. A write may replace a whole base-relative pointer slot (60/90 FPS++ swap
 * function pointers): the patch holds the address at the patches' base, rebased here. */
static void apply_patches(const char *path, Segment *segments, uint64_t ns, const Reloc *relocs, uint64_t nr) {
    FILE *f=fopen(path,"rb");
    char magic[8];
    if (!f || fread(magic,1,8,f)!=8 || memcmp(magic,"BBPATCH2",8)) fail("invalid patch file");
    uint64_t base=read64(f), count=read64(f), bytes=0, rebased=0;
    unsigned char data[4096];
    for (uint64_t i=0;i<count;++i) {
        uint64_t offset=read64(f), length=read64(f);
        if (!length || length>sizeof(data) || !mapped(segments,ns,offset,length) || fread(data,1,length,f)!=length)
            fail("bad patch entry");
        uint64_t slots[sizeof(data)/8]; size_t nslots=0;
        for (uint64_t r=0;r<nr;++r) {
            if (!(relocs[r].target<offset+length && offset<relocs[r].target+8)) continue;
            uint64_t target=relocs[r].target, value;
            if (relocs[r].kind || target<offset || target+8>offset+length) fail("patch overlaps a relocation");
            memcpy(&value,data+(target-offset),8);
            if (value<base || !mapped(segments,ns,value-base,1)) fail("patch writes a pointer outside the image");
            slots[nslots++]=target;
        }
        memcpy(image+offset,data,length);
        for (size_t s=0;s<nslots;++s) {
            uint64_t value;
            memcpy(&value,image+slots[s],8);
            value=(uint64_t)(uintptr_t)image+(value-base);
            memcpy(image+slots[s],&value,8);
        }
        rebased+=nslots;
        bytes+=length;
    }
    if (fgetc(f)!=EOF) fail("trailing data in patch file");
    fclose(f);
    printf("Patches: %" PRIu64 " writes, %" PRIu64 " bytes applied, %" PRIu64 " pointers rebased\n",count,bytes,rebased);
}
/* Restarts the game through run.sh (the settings menu: a new render resolution is a patch
 * applied at start). Descriptors are closed first so the old GPU device and its memory are
 * released before the new process opens its own. */
void runtime_restart(void) {
    fflush(NULL);
#ifdef _WIN32
    /* scripts/run_game.py sets its own command line here; the new process prepares the
     * patches again. Our GPU device goes away with this process. */
    const char *command = getenv("BB_RESTART_COMMAND");
    puts("Runtime: restarting through the launcher");
    if (!command) { fputs("runtime_restart: BB_RESTART_COMMAND is not set\n", stderr); _exit(1); }
    char line[32768];
    snprintf(line, sizeof(line), "%s", command);
    STARTUPINFOA startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    if (!CreateProcessA(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
        fprintf(stderr, "runtime_restart: CreateProcess failed (%lu)\n", GetLastError());
        _exit(1);
    }
    _exit(0);
#else
    puts("Runtime: restarting through run.sh");
    syscall(SYS_close_range, 3u, ~0u, 0u);
    execlp("bash", "bash", "run.sh", (char *)NULL);
    perror("runtime_restart: exec");
    _exit(1);
#endif
}

#ifdef _WIN32
/* The guest main thread needs a large committed stack (PS4 code does not touch guard pages
 * in order), so the loader runs on a thread of its own; the process main thread waits. */
static int loader_main(int argc, char **argv);
static int main_argc;
static char **main_argv;
static unsigned __stdcall loader_thread(void *unused) { (void)unused; return (unsigned)loader_main(main_argc, main_argv); }
int main(int argc, char **argv) {
    timeBeginPeriod(1); /* 1 ms waits for timed locks and sleeps outside runtime_sleep_ns */
    /* No EcoQoS: Windows 11 otherwise moves the threads of an unfocused window to efficiency
     * cores (hybrid CPUs), and the GPU command thread sets the frame rate. */
    PROCESS_POWER_THROTTLING_STATE throttling = {PROCESS_POWER_THROTTLING_CURRENT_VERSION,
                                                 PROCESS_POWER_THROTTLING_EXECUTION_SPEED, 0};
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));
    AddVectoredExceptionHandler(1, vectored_handler);
    SetUnhandledExceptionFilter(unhandled_filter);
    start_dump_thread();
    start_hw_watch();
    main_argc = argc; main_argv = argv;
    HANDLE thread = (HANDLE)_beginthreadex(NULL, 64u << 20, loader_thread, NULL, 0, NULL);
    if (!thread) fail("cannot start the loader thread");
    WaitForSingleObject(thread, INFINITE);
    DWORD code = 1;
    GetExitCodeThread(thread, &code);
    return (int)code;
}
#define main loader_main
#endif

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
#ifndef _WIN32
    /* Keep host heap objects handed to the guest (thread handles, TLS) in the
       non-PIE brk heap, i.e. below 1 TiB: the guest packs pointers into 40 bits. */
    mallopt(M_ARENA_MAX,1);
    mallopt(M_MMAP_THRESHOLD,32*1024*1024);
#endif
    if (argc == 2 && !strcmp(argv[1], "--vulkan-only")) return vulkan_smoke();
    int cpu_only = 0, strict_imports = 0;
    unsigned timeout_seconds = 10;
    const char *content_profile=NULL, *app0=NULL, *user_dir=NULL, *patch_file=NULL;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--cpu-only")) cpu_only = 1;
        else if (!strcmp(argv[i], "--strict-imports")) strict_imports = 1;
        else if (!strcmp(argv[i], "--content-profile") && i+1<argc) content_profile=argv[++i];
        else if (!strcmp(argv[i], "--app0") && i+1<argc) app0=argv[++i];
        else if (!strcmp(argv[i], "--user") && i+1<argc) user_dir=argv[++i];
        else if (!strcmp(argv[i], "--patches") && i+1<argc) patch_file=argv[++i];
        else if (!strcmp(argv[i], "--timeout") && i+1<argc) timeout_seconds=(unsigned)strtoul(argv[++i],NULL,10);
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); return 1; }
    }
    if (argc < 2) {
        fprintf(stderr, "Usage: %s boot.bin [--cpu-only] [--strict-imports] [--content-profile file] [--app0 dir] [--user dir] [--patches file] [--timeout seconds] | --vulkan-only\n", argv[0]);
        return 1;
    }
    if (content_profile) {
        FILE *profile=fopen(content_profile,"rb");
        unsigned char data[28];
        if (!profile) fail("cannot open content profile");
        if (fread(data,1,sizeof(data),profile)!=sizeof(data) || fgetc(profile)!=EOF || memcmp(data,"BBCONT01",8)) fail("invalid content profile");
        fclose(profile);
        uint32_t values[5];
        for (unsigned i=0;i<5;++i) {
            unsigned char *p=data+8+i*4;
            values[i]=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
        }
        runtime_content_configure(values);
    }
    if (app0) {
        runtime_file_configure(app0, user_dir ? user_dir : "user");
        char sfo[4096], id[16]="";
        snprintf(sfo,sizeof(sfo),"%s/sce_sys/param.sfo",app0);
        if (sfo_value(sfo,"INSTALL_DIR_SAVEDATA",id,sizeof(id),NULL) || sfo_value(sfo,"TITLE_ID",id,sizeof(id),NULL))
            runtime_savedata_configure(id);
    }
    bbgpu_register_kernel();
#ifdef _WIN32
    SYSTEM_INFO system_info; GetSystemInfo(&system_info); page_size = system_info.dwPageSize;
    (void)timeout_seconds; /* no watchdog on Windows */
#else
    page_size = (size_t)sysconf(_SC_PAGESIZE);
    struct sigaction sa = {0}; sa.sa_sigaction = fault; sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGILL, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    Dl_info self_info;
    if (dladdr((void *)main,&self_info)) exe_base=(uintptr_t)self_info.dli_fbase;
    struct sigaction dump = {0}; dump.sa_sigaction = thread_dump; dump.sa_flags = SA_SIGINFO|SA_RESTART;
    sigemptyset(&dump.sa_mask); sigaction(SIGUSR2, &dump, NULL);
    struct sigaction alarm_action = {0}; alarm_action.sa_sigaction = watchdog; alarm_action.sa_flags = SA_SIGINFO;
    sigemptyset(&alarm_action.sa_mask); sigaction(SIGALRM, &alarm_action, NULL);
    alarm(timeout_seconds); /* 0 disables the watchdog */
#endif
    FILE *f = fopen(argv[1], "rb");
    if (!f) fail("cannot open boot file; run prepare.py first");
    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || (memcmp(magic, "BBPROBE1", 8) && memcmp(magic, "BBPROBE2", 8) && memcmp(magic,"BBPROBE3",8) && memcmp(magic,"BBPROBE4",8) && memcmp(magic,"BBPROBE5",8))) fail("bad boot file signature");
    uint64_t size = read64(f), entry = read64(f), ns = read64(f), nr = read64(f);
    import_count = read64(f);
    uint64_t capabilities = memcmp(magic, "BBPROBE1", 8) ? read64(f) : 0;
    if (capabilities & ~UINT64_C(1)) fail("unknown runtime capabilities");
    runtime_start(strict_imports ? 0 : capabilities);
    if (!size || size > 512*1024*1024 || entry >= size || !ns || ns > 64 || nr > 1000000 || import_count > 100000)
        fail("boot file limits exceeded");
    int multi=!memcmp(magic,"BBPROBE5",8);
    int linked=!memcmp(magic,"BBPROBE3",8) || !memcmp(magic,"BBPROBE4",8) || multi;
    int native_libc=linked && !strict_imports && (capabilities&1);
    uint64_t main_tls[4]={0};
    uint64_t nb=0,procparam=0;
    uint64_t *bindings=calloc(import_count ? import_count : 1,sizeof(*bindings));
    uint64_t *binding_kinds=calloc(import_count ? import_count : 1,sizeof(*binding_kinds));
    if (!bindings || !binding_kinds) fail("allocation failed");
    if (multi) {
        /* BBPROBE5: procparam, eboot TLS, module table, bindings (link_modules.py). */
        procparam=read64(f);
        for (int i=0;i<4;++i) main_tls[i]=read64(f);
        module_count=read64(f);
        if (!module_count || module_count>sizeof(modules)/sizeof(*modules)) fail("invalid module count");
        for (uint64_t m=0;m<module_count;++m) {
            LinkedModule *x=&modules[m];
            x->base=read64(f); x->size=read64(f); x->init=read64(f); x->tls_address=read64(f);
            x->tls_memsz=read64(f); x->tls_filesz=read64(f); x->tls_module=read64(f);
        }
        nb=read64(f);
    } else if (linked) {
        LinkedModule *x=&modules[0];
        module_count=1;
        x->base=read64(f); x->size=read64(f); x->init=read64(f);
        x->tls_address=read64(f); x->tls_memsz=read64(f); x->tls_filesz=read64(f); x->tls_module=2; nb=read64(f);
        procparam=read64(f);
        if (!memcmp(magic,"BBPROBE4",8)) for (int i=0;i<4;++i) main_tls[i]=read64(f);
    }
    if (linked) {
        if (main_tls[1]>main_tls[2] || main_tls[2]>1024*1024 || main_tls[0]>size ||
            main_tls[1]>size-main_tls[0] || (main_tls[3] & (main_tls[3]-1)) || main_tls[3]>4096)
            fail("invalid eboot TLS metadata");
        for (uint64_t m=0;m<module_count;++m) {
            LinkedModule *x=&modules[m];
            if (x->base>=size || !x->size || x->size>size-x->base || x->init<x->base || x->init-x->base>=x->size ||
                (x->tls_module && (x->tls_address>=size || x->tls_memsz>size-x->tls_address || x->tls_memsz>1024*1024 ||
                                   x->tls_filesz>x->tls_memsz || x->tls_module<2 || x->tls_module>7)))
                fail("invalid linked module metadata");
        }
        if (nb>import_count) fail("invalid binding count");
        for (uint64_t i=0;i<nb;++i) {
            uint64_t index=read64(f),address=read64(f),kind=read64(f);
            int inside=0;
            for (uint64_t m=0;m<module_count;++m)
                if (address>=modules[m].base && address-modules[m].base<modules[m].size) inside=1;
            if (index>=import_count || !inside || (kind!=1 && kind!=2) || bindings[index]) fail("invalid native binding");
            bindings[index]=address; binding_kinds[index]=kind;
        }
    }
    Segment *segments = calloc(ns, sizeof(*segments));
    Reloc *relocs = calloc(nr ? nr : 1, sizeof(*relocs));
    names = calloc(import_count ? import_count : 1, sizeof(*names));
    if (!segments || !relocs || !names) fail("allocation failed");
    for (uint64_t i = 0; i < ns; ++i) {
        segments[i].address = read64(f);
        segments[i].size = read64(f);
        segments[i].flags = read64(f);
        if (segments[i].address > size || segments[i].size > size - segments[i].address ||
            segments[i].address % page_size || segments[i].flags > 7) fail("bad segment");
    }
    if (fread(names, 128, import_count, f) != import_count) fail("truncated import names");
    for (uint64_t i = 0; i < import_count; ++i)
        if (!memchr(names[i], 0, 128)) fail("unterminated import name");
    for (uint64_t i = 0; i < nr; ++i) {
        relocs[i].target = read64(f);
        relocs[i].kind = read64(f);
        relocs[i].value = read64(f);
        relocs[i].addend = read64(f);
        if (!mapped(segments, ns, relocs[i].target, 8) || relocs[i].kind > 2 ||
            (relocs[i].kind && relocs[i].value >= import_count) ||
            (relocs[i].kind != 2 && relocs[i].addend) || relocs[i].addend >= page_size) fail("bad relocation");
    }
    for (uint64_t m=0;m<module_count;++m)
        if (!mapped(segments,ns,modules[m].init,1) || (modules[m].tls_module && !mapped(segments,ns,modules[m].tls_address,modules[m].tls_memsz)))
            fail("unmapped module metadata");
    if (module_count && !mapped(segments,ns,procparam,64)) fail("unmapped procparam");
    for (uint64_t i=0;i<import_count;++i) {
        if (!bindings[i]) continue;
        if (!mapped(segments,ns,bindings[i],1)) fail("unmapped native export");
        if (binding_kinds[i]==1) {
            int executable=0;
            for (uint64_t s=0;s<ns;++s)
                if ((segments[s].flags&1) && bindings[i]>=segments[s].address &&
                    bindings[i]-segments[s].address<segments[s].size) executable=1;
            if (!executable) fail("native function is not executable");
        }
    }
    image = allocate(round_page(size));
#ifdef _WIN32
    image_size = round_page(size);
#endif
    if (fread(image, 1, size, f) != size || fgetc(f) != EOF) fail("incorrect memory image size");
    fclose(f);
    if (!cpu_only) {
        char title[128]="Bloodborne", serial[16]="UNKNOWN", sfo[4096];
        uint32_t attributes=0;
        snprintf(sfo,sizeof(sfo),"%s/sce_sys/param.sfo",app0 ? app0 : ".");
        sfo_value(sfo,"TITLE",title,sizeof(title),NULL);
        sfo_value(sfo,"TITLE_ID",serial,sizeof(serial),NULL);
        sfo_value(sfo,"ATTRIBUTE",NULL,0,&attributes);
        uint64_t sdk=0;
        if (procparam) memcpy(&sdk,image+procparam+16,8); /* procparam: size, magic, count, sdk_version */
        BbGpuConfig gpu={title,serial,user_dir ? user_dir : "user",(uint32_t)sdk,attributes,1920,1080};
        gpu_enabled=1; /* page tracking starts while the rasterizer registers guest memory */
        if (bbgpu_init(&gpu)) fail("GPU initialization failed");
        printf("GPU: window and Vulkan presenter ready; SDK 0x%08x, %u HLE symbols\n",(unsigned)sdk,bbgpu_symbol_count());
    }
    unsigned char *traps = allocate(round_page((import_count + 1) * 32));
#ifdef _WIN32
    traps_base = traps; traps_size = round_page((import_count + 1) * 32);
#endif
    unsigned char *data_traps = allocate((import_count + 1) * page_size);
    protect(data_traps, (import_count + 1) * page_size, 0);
    for (uint64_t i = 0; i < import_count; ++i) {
        unsigned char *t = traps + i * 32;
        /* SysV: mov edi, index; movabs rax, handler; jmp rax. No fake return values. */
        uint32_t index = (uint32_t)i;
        uintptr_t handler = (uintptr_t)unresolved;
        t[0] = 0x48; t[1] = 0x89; t[2] = 0xfe; /* mov rsi,rdi: preserve arg0 */
        t[3] = 0xbf; memcpy(t + 4, &index, 4);
        t[8] = 0x48; t[9] = 0xb8; memcpy(t + 10, &handler, 8);
        t[18] = 0xff; t[19] = 0xe0;
    }
    for (uint64_t i = 0; i < nr; ++i) {
        uintptr_t value = relocs[i].kind == 2 ? (uintptr_t)(data_traps + page_size * relocs[i].value + relocs[i].addend)
                        : relocs[i].kind == 1 ? (uintptr_t)(traps + 32 * relocs[i].value)
                        : (uintptr_t)image + relocs[i].value;
        if (relocs[i].kind) {
            uintptr_t resolved = runtime_resolve(names[relocs[i].value], relocs[i].kind == 2);
            if (resolved) value = resolved + relocs[i].addend;
            else if (native_libc && bindings[relocs[i].value]) {
                uint64_t address=bindings[relocs[i].value];
                if (binding_kinds[relocs[i].value]!=relocs[i].kind || !mapped(segments,ns,address,relocs[i].addend+1)) fail("native export kind/range mismatch");
                value=(uintptr_t)image+address+relocs[i].addend;
            }
        }
        memcpy(image + relocs[i].target, &value, 8);
    }
    if (patch_file) apply_patches(patch_file, segments, ns, relocs, nr);
#ifdef _WIN32
    printf("Guest thread pointer reads: %" PRIu64 " use TEB TLS slot %u\n", patch_tls_reads(segments, ns), runtime_win_tls_slot());
#endif
    protect(traps, round_page((import_count + 1) * 32), 5);
    protect(image, round_page(size), 0);
    int executable_entry = 0;
    for (uint64_t i = 0; i < ns; ++i) {
        protect(image + segments[i].address, round_page(segments[i].size), (unsigned)segments[i].flags);
        if ((segments[i].flags & 1) && entry >= segments[i].address && entry - segments[i].address < segments[i].size)
            executable_entry = 1;
    }
    if (!executable_entry) fail("entry is not executable");
    printf("Mapped %" PRIu64 " bytes, %" PRIu64 " segments; applied %" PRIu64 " relocations\n", size, ns, nr);
    if (native_libc) {
        for (uint64_t m=0;m<module_count;++m) {
            int init_executable=0;
            for (uint64_t i=0;i<ns;++i)
                if ((segments[i].flags&1) && modules[m].init>=segments[i].address && modules[m].init-segments[i].address<segments[i].size) init_executable=1;
            if (!init_executable) fail("module init is not executable");
            if (modules[m].tls_module)
                runtime_set_module_tls(modules[m].tls_module,image+modules[m].tls_address,modules[m].tls_filesz,modules[m].tls_memsz);
        }
        runtime_set_main_tls(image+main_tls[0],main_tls[1],main_tls[2],main_tls[3]);
        runtime_thread_attach_main();
        runtime_set_procparam(image+procparam);
        /* Dependencies start in link order (libc first), as the PS4 dynamic linker does. */
        for (uint64_t m=0;m<module_count;++m) {
            printf("Starting linked module %" PRIu64 " at image offset 0x%" PRIx64 "; native bindings=%" PRIu64 "\n",m,modules[m].init,nb);
            typedef int (ABI *ModuleInit)(uint64_t,void *,void *);
            int result=((ModuleInit)(image+modules[m].init))(0,NULL,NULL);
            printf("Module %" PRIu64 " initializer returned %d\n",m,result);
            if (result) fail("module initializer failed");
        }
    }
#ifdef _WIN32
    /* Libraries loaded since main (GPU driver, SDL) may have installed their own. */
    SetUnhandledExceptionFilter(unhandled_filter);
#endif
    printf("Entering original x86-64 code at guest offset 0x%" PRIx64 "\n", entry);
    entered_game=1;
    struct { uint64_t argc; const char *argv[2]; } params = {1, {"/app0/eboot.bin", NULL}};
#ifdef _WIN32
    typedef void (ABI *Entry)(void *, void (ABI *)(void));
    ((Entry)(image + entry))(&params, guest_exit);
#else
    /* The guest main thread runs on a stack below 1 TiB like PS4 stacks. */
    enum { MAIN_STACK=8*1024*1024 };
    unsigned char *stack=runtime_low_map(MAIN_STACK,PROT_READ|PROT_WRITE);
    if (!stack) fail("cannot allocate guest main stack");
    enter_on_stack(image+entry,&params,(void *)guest_exit,stack+MAIN_STACK-64);
#endif
    fail("entry unexpectedly returned");
}
