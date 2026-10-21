// Writes a short crash report to the log on Windows.
//
// Release builds are stripped, so the report gives each stack frame as an
// offset into the exe. The unstripped exe from the same build turns those
// offsets back into function names (llvm-symbolizer --obj=... 0xOFFSET).

#ifdef _WIN32

#include <cstdio>
#include <windows.h>

namespace {
uintptr_t exe_base = 0;
uintptr_t exe_end = 0;
volatile LONG reported = 0;

bool in_exe(uintptr_t address) {
    return address >= exe_base && address < exe_end;
}

LONG CALLBACK crash_handler(EXCEPTION_POINTERS* info) {
    DWORD code = info->ExceptionRecord->ExceptionCode;
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_PRIV_INSTRUCTION:
            break;
        default:
            return EXCEPTION_CONTINUE_SEARCH;
    }
    // Faults inside drivers or system DLLs are often handled there; only
    // report the ones in our own code.
    uintptr_t fault = (uintptr_t)info->ExceptionRecord->ExceptionAddress;
    if (!in_exe(fault) || InterlockedExchange(&reported, 1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    fprintf(stderr, "\n[crash] exception %08lX at exe+0x%llX", code, (unsigned long long)(fault - exe_base));
    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
        fprintf(stderr, " (%s address %016llX)",
            info->ExceptionRecord->ExceptionInformation[0] == 1 ? "writing" : "reading",
            (unsigned long long)info->ExceptionRecord->ExceptionInformation[1]);
    }
    fprintf(stderr, " on thread %lu\n[crash] stack:", GetCurrentThreadId());

    // Walk the stack with the unwind tables in the exe.
    CONTEXT context = *info->ContextRecord;
    for (int frame = 0; frame < 32 && context.Rip != 0; frame++) {
        if (in_exe(context.Rip)) {
            fprintf(stderr, " 0x%llX", (unsigned long long)(context.Rip - exe_base));
        } else {
            fprintf(stderr, " ?%016llX", (unsigned long long)context.Rip);
        }
        DWORD64 image_base = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
        if (function == nullptr) {
            // Leaf function: the return address is on top of the stack.
            context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
            context.Rsp += 8;
        } else {
            void* handler_data = nullptr;
            DWORD64 establisher_frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context,
                &handler_data, &establisher_frame, nullptr);
        }
    }
    fprintf(stderr, "\n");
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
}

void install_crash_log() {
    HMODULE exe = GetModuleHandleW(nullptr);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(exe);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(exe) + dos->e_lfanew);
    exe_base = reinterpret_cast<uintptr_t>(exe);
    exe_end = exe_base + nt->OptionalHeader.SizeOfImage;
    AddVectoredExceptionHandler(1, crash_handler);
}

#else

void install_crash_log() {}

#endif
