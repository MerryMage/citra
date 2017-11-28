// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/crash_handler.h"

#ifdef _WIN32

#include <windows.h>
// windows.h must be included first.
#include <dbghelp.h>

#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <boost/optional.hpp>
#include "common/scope_exit.h"
#include "common/string_util.h"

namespace Common {

static bool unhandled_exception_called;
static std::jmp_buf unhandled_exception_jmp_buf;
static std::vector<std::string> unhandled_exception_stack_trace;
static boost::optional<std::string> minidump_filename;

static void* ctx_buffer = nullptr;
static PCONTEXT ctx = nullptr;

static void Initialize();
static LONG WINAPI UnhandledExceptionFilter(_EXCEPTION_POINTERS*);
static void GetStackTrace(CONTEXT& c);
static void CreateMiniDump(const std::string& filename, _EXCEPTION_POINTERS*);

void CrashHandler(std::function<void()> try_,
                  std::function<void(const Common::CrashInformation&)> catch_,
                  boost::optional<std::string> filename) {
    unhandled_exception_called = false;
    minidump_filename = filename;

    if (!ctx) {
        Initialize();
    }

    // Accessing non-volatile local variables in setjmp's scope is indeterminate.
    volatile LPTOP_LEVEL_EXCEPTION_FILTER previous_filter;
    previous_filter = SetUnhandledExceptionFilter(UnhandledExceptionFilter);

    if (setjmp(unhandled_exception_jmp_buf) == 0) {
        try_();
    } else {
        catch_({unhandled_exception_stack_trace, minidump_filename});
    }

    SetUnhandledExceptionFilter(previous_filter);
}

static void Initialize() {
    DWORD ctx_size = 0;
    InitializeContext(nullptr, CONTEXT_ALL, nullptr, &ctx_size);
    ctx_buffer = std::malloc(ctx_size);
    InitializeContext(ctx_buffer, CONTEXT_ALL, &ctx, &ctx_size);
}

/**
 * This function is called by the operating system when an unhandled exception occurs.
 * This includes things like debug breakpoints when not connected to a debugger.
 * @param ep The exception pointer containing exception information.
 * @return See Microsoft's documentation on SetUnhandledExceptionFilter for possible return values.
 */
static LONG WINAPI UnhandledExceptionFilter(_EXCEPTION_POINTERS* ep) {
    // Prevent re-entry.
    if (unhandled_exception_called)
        return EXCEPTION_CONTINUE_SEARCH;
    unhandled_exception_called = true;

    if (ctx) {
        // A copy is required as getting the stack trace modifies the context.
        CopyContext(ctx, CONTEXT_ALL, ep->ContextRecord);
        GetStackTrace(*ctx);

        // Ensure we have a log of everything in the console.
        std::fprintf(stderr, "Unhandled Exception:\n");
        for (const auto& line : unhandled_exception_stack_trace) {
            std::fprintf(stderr, "%s\n", line.c_str());
        }
        std::fflush(stderr);
    } else {
        unhandled_exception_stack_trace = {"Unable to get stack trace"};
    }

    if (minidump_filename) {
        CreateMiniDump(*minidump_filename, ep);
    }

    std::longjmp(unhandled_exception_jmp_buf, 1);

    return EXCEPTION_CONTINUE_SEARCH;
}

/**
 * This function walks the stack of the current thread using StackWalk64.
 * @param ctx The context record that contains the information on the stack of interest.
 * @return A string containing a human-readable stack trace.
 */
static void GetStackTrace(CONTEXT& ctx) {
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    // This function generates a single line of the stack trace.
    // `return_address` is a return address found on the stack.
    auto get_symbol_info = [&process](DWORD64 return_address) -> std::string {
        constexpr size_t SYMBOL_NAME_SIZE = 512; // arbitrary value

        // Allocate space for symbol info.
        IMAGEHLP_SYMBOL64* symbol = static_cast<IMAGEHLP_SYMBOL64*>(
            std::calloc(sizeof(IMAGEHLP_SYMBOL64) + SYMBOL_NAME_SIZE * sizeof(char), 1));
        symbol->MaxNameLength = SYMBOL_NAME_SIZE;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        SCOPE_EXIT({ std::free(symbol); });

        // Actually get symbol info.
        DWORD64 symbol_displacement; // Offset of return_address from function entry point.
        SymGetSymFromAddr64(process, return_address, &symbol_displacement, symbol);

        // Get undecorated name.
        char undecorated_name[SYMBOL_NAME_SIZE + 1];
        UnDecorateSymbolName(symbol->Name, &undecorated_name[0], SYMBOL_NAME_SIZE,
                             UNDNAME_COMPLETE);

        // Get source code line information.
        DWORD line_displacement = 0; // Offset of return_address from first instruction of line.
        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        SymGetLineFromAddr64(process, return_address, &line_displacement, &line);

        // Remove unnecessary path information before the "\\src\\" directory.
        std::string file_name = "(null)";
        if (line.FileName) {
            file_name = line.FileName;
            size_t found = file_name.find("\\src\\");
            if (found != std::string::npos)
                file_name = file_name.substr(found + 1);
        }

        // Format string
        return Common::StringFromFormat("[%llx] %s+0x%llx (%s:%i)", return_address,
                                        undecorated_name, symbol_displacement, file_name.c_str(),
                                        line.LineNumber);
    };

    // NOTE: SymFunctionTableAccess64 doesn't work with the non-standard stack frames our JIT
    // produces. Thus we elect to not use StackWalk64, but instead manually use the Rtl* functions.

    // Initialise symbols
    if (SymInitialize(process, nullptr, TRUE) == FALSE) {
        std::fprintf(stderr, "Failed to get symbols. Continuing anyway...\n");
    }
    SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    SCOPE_EXIT({ SymCleanup(process); });

    // Walk the stack
    unhandled_exception_stack_trace = {};
    while (ctx.Rip != 0) {
        unhandled_exception_stack_trace.emplace_back(get_symbol_info(ctx.Rip));

        DWORD64 image_base;
        PRUNTIME_FUNCTION runtime_function = RtlLookupFunctionEntry(ctx.Rip, &image_base, nullptr);

        if (!runtime_function) {
            // This is likely a leaf function. Adjust the stack appropriately.
            if (!ctx.Rsp) {
                unhandled_exception_stack_trace.emplace_back("Invalid rsp");
                return;
            }
            ctx.Rip = *(reinterpret_cast<u64*>(ctx.Rsp));
            ctx.Rsp += 8;
            continue;
        }

        PVOID handler_data;
        ULONG64 establisher_frame;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, ctx.Rip, runtime_function, &ctx,
                         &handler_data, &establisher_frame, nullptr);
    }
}

/**
 * This function is called by the operating system when a minidump is made.
 * Microsoft's documentation on MiniDumpWriteDump for more information.
 */
static BOOL CALLBACK MiniDumpCallback(PVOID, const PMINIDUMP_CALLBACK_INPUT input,
                                      PMINIDUMP_CALLBACK_OUTPUT output) {
    if (!input || !output)
        return FALSE;

    switch (input->CallbackType) {
    case IncludeModuleCallback:
    case IncludeThreadCallback:
    case ThreadCallback:
    case ThreadExCallback:
    case MemoryCallback:
        return TRUE;

    case ModuleCallback:
        if (!(output->ModuleWriteFlags & ModuleReferencedByMemory)) {
            // Exclude module from minidump if not referenced by memory
            output->ModuleWriteFlags &= ~ModuleWriteModule;
        }
        return TRUE;

    case CancelCallback:
    default:
        return FALSE;
    }
}

/**
 * Create a minidump.
 * @param filename The location where the minidump will be created.
 * @param ep The exception pointer containing exception information. This is required so that
 *           minidump records for the current thread has the correct stack information at the
 *           exception point.
 */
static void CreateMiniDump(const std::string& filename, _EXCEPTION_POINTERS* ep) {
    HANDLE file = CreateFileA(filename.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == nullptr || file == INVALID_HANDLE_VALUE)
        return;

    MINIDUMP_EXCEPTION_INFORMATION ei;
    ei.ThreadId = GetCurrentThreadId();
    ei.ExceptionPointers = ep;
    ei.ClientPointers = FALSE;

    MINIDUMP_CALLBACK_INFORMATION ci;
    ci.CallbackRoutine = MiniDumpCallback;
    ci.CallbackParam = 0;

    // One may want to add to this if minidumps were found to provide insufficient information.
    MINIDUMP_TYPE t = static_cast<MINIDUMP_TYPE>(
        MiniDumpNormal | MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory |
        MiniDumpWithThreadInfo | MiniDumpWithFullMemoryInfo | MiniDumpIgnoreInaccessibleMemory);

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, t, &ei, 0, &ci);

    CloseHandle(file);
}

} // namespace Common

#else

namespace Common {

void CrashHandler(std::function<void()> try_,
                  std::function<void(const Common::CrashInformation&)> catch_,
                  boost::optional<std::string> filename) {
    // Crash handler unimplemented for this platform
    try_();
}

} // namespace Common

#endif
