// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <string>

#include "common/scm_rev.h"
#include "common/scope_exit.h"

// Since we have no idea what the state of the application is at the point
// the crash handler is called, we're going to have to use reasonably
// low-level methods to display things to the user.
//
// Keep it as simple as possible.
//
// An advisable strategy is to get things onto the console first before
// trying to do fancy GUI stuff. TODO(merry): An even better strategy would
// be to not do any processing at all and punt to a watchdog process, because
// ideally one cannot assume memory allocation works (what if the crash was
// in the allocator?) or stack allocate (what if rsp isn't in a valid state?).

#ifdef _WIN32

#pragma comment(lib, "dbghelp.lib")

#include <windows.h> // Must include this first.
#include <dbghelp.h>

#include "common/string_util.h"

namespace CrashHandler {

static LONG WINAPI MyUnhandledExceptionFilter(_EXCEPTION_POINTERS*);
static std::string GetStackTrace(CONTEXT& c);

void Register() {
    SetUnhandledExceptionFilter(MyUnhandledExceptionFilter);
}

/**
 * This function is called by the operating system when an unhandled exception occurs.
 * This includes things like debug breakpoints when not connected to a debugger.
 * @param ep The exception pointer containing exception information.
 * @return See Microsoft's documentation on SetUnhandledExceptionFilter for possible return values.
 */
static LONG WINAPI MyUnhandledExceptionFilter(_EXCEPTION_POINTERS* ep) {
    std::string stack_trace = GetStackTrace(*ep->ContextRecord);

    std::string detail =
        std::string("Version: ") + Common::g_scm_branch + ":" + Common::g_scm_desc + "\n"
        "Commit: " + Common::g_scm_rev + "\n"
        "Stack Trace:\n" + stack_trace;

    // Ensure we have a log of everything in the console first.
    fprintf(stderr, "Unhandled Exception:\n%s", detail.c_str());
    fflush(stderr);

    std::string title = "Citra: Caught Unhandled Exception";

    std::string message = std::string("Press Ctrl+C to copy text\n"
        "Please also take a copy of the console window\n\n") + detail;

    // QMessageBox is not guaranteed to work here since:
    // 1. We're not guaranteed to be in the GUI thread
    // 2. Qt is not guaranteed to be in a valid state
    // We need to use something lower-level.
    MessageBoxA(nullptr, message.c_str(), title.c_str(), MB_ICONSTOP);
    FatalAppExitA(0, "Terminating application");

    return EXCEPTION_CONTINUE_SEARCH;
}

/**
 * This function walks the stack of the current thread using StackWalk64.
 * @param ctx The context record that contains the information on the stack of interest.
 * @return A string containing a human-readable stack trace.
 */
static std::string GetStackTrace(CONTEXT& ctx) {
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    // This function generates a single line of the stack trace.
    // `return_address` is a return address found on the stack.
    auto get_symbol_info = [&process](DWORD64 return_address) -> std::string {
        constexpr size_t SYMBOL_NAME_SIZE = 512; // arbitrary value

        // Allocate space for symbol info.
        IMAGEHLP_SYMBOL64* symbol = static_cast<IMAGEHLP_SYMBOL64*>(calloc(sizeof(IMAGEHLP_SYMBOL64) + SYMBOL_NAME_SIZE * sizeof(char), 1));
        symbol->MaxNameLength = SYMBOL_NAME_SIZE;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        SCOPE_EXIT({ free(symbol); });

        // Actually get symbol info.
        DWORD64 symbol_displacement; // The offset of return_address from the start of the function.
        SymGetSymFromAddr64(process, return_address, &symbol_displacement, symbol);

        // Get undecorated name.
        char undecorated_name[SYMBOL_NAME_SIZE + 1];
        UnDecorateSymbolName(symbol->Name, &undecorated_name[0], SYMBOL_NAME_SIZE, UNDNAME_COMPLETE);

        // Get source code line information.
        DWORD line_displacement = 0; // The offset of return_address from the first instruction of this line.
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
        return Common::StringFromFormat("[%llx] %s+0x%llx (%s:%i)\n", return_address, undecorated_name, symbol_displacement, file_name.c_str(), line.LineNumber);
    };

    // Initialise symbols
    if (SymInitialize(process, nullptr, TRUE) == FALSE) {
        fprintf(stderr, "Failed to get symbols. Continuing anyway...\n");
    }
    SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    SCOPE_EXIT({ SymCleanup(process); });

    // Extract stack information from context
    STACKFRAME64 sframe;
    sframe.AddrPC.Offset = ctx.Rip;
    sframe.AddrPC.Mode = AddrModeFlat;
    sframe.AddrStack.Offset = ctx.Rsp;
    sframe.AddrStack.Mode = AddrModeFlat;
    sframe.AddrFrame.Offset = ctx.Rbp;
    sframe.AddrFrame.Mode = AddrModeFlat;

    // Return value
    std::string stack_trace;

    // Walk the stack
    do {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &sframe, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            stack_trace += "Last StackWalk64 failed\n";
            return stack_trace;
        }

        if (sframe.AddrPC.Offset != 0) {
            stack_trace += get_symbol_info(sframe.AddrPC.Offset);
        } else {
            stack_trace += "No Symbols: rip == 0\n";
        }
    } while (sframe.AddrReturn.Offset != 0);

    return stack_trace;
}

} // namespace CrashHandler

#else

#include <cstdio>
#include <cstdlib>
#include <execinfo.h>
#include <signal.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <objc/message.h>
#include <objc/runtime.h>
#endif

namespace CrashHandler {

static void SignalHandler(int signal);
static const char* GetSignalName(int signal);
static std::string GetStackTrace();

#ifdef __APPLE__
static void OSXMessageBox(const std::string& title, const std::string& message);
#endif

void Register() {
    // signal is supported on both OS X and Linux.
    signal(SIGABRT, SignalHandler);
    signal(SIGSEGV, SignalHandler);
    signal(SIGILL, SignalHandler);
    signal(SIGFPE, SignalHandler);
    signal(SIGBUS, SignalHandler);
    signal(SIGTRAP, SignalHandler);
}

static void SignalHandler(int signal) {
    // Just in case recursion is possible.
    static size_t times_called = 0;
    times_called++;
    if (times_called > 1) {
        return;
    }

    fprintf(stderr, "Oops. Now in CrashHandler::SignalHandler:\n");
    fprintf(stderr, "Caught signal %i (%s)\n", signal, GetSignalName(signal));
    fprintf(stderr, "Git Branch: %s (%s)\n", Common::g_scm_branch, Common::g_scm_desc);
    fprintf(stderr, "Git Commit: %s\n", Common::g_scm_rev);
    fprintf(stderr, "Stack Trace:\n");

    std::string stack_trace = GetStackTrace();
    fflush(stderr);

#ifdef __APPLE__
    std::string title = "Caught Signal " + std::to_string(signal) + " (" + GetSignalName(signal) + ")";

    std::string message =
        std::string("Version: ") + Common::g_scm_branch + "-" + Common::g_scm_desc + "\n" +
        "Commit: " + Common::g_scm_rev + "\n" +
        "Stack Trace:\n" + stack_trace;

    OSXMessageBox(title, message);
#endif

    exit(EXIT_FAILURE);
}

static const char* GetSignalName(int signal) {
    switch (signal) {
    case SIGABRT:
        return "SIGABRT";
    case SIGSEGV:
        return "SIGSEGV";
    case SIGILL:
        return "SIGILL";
    case SIGFPE:
        return "SIGFPE";
    case SIGBUS:
        return "SIGBUS";
    case SIGTRAP:
        return "SIGTRAP";
    }

    return "unknown signal";
}

static std::string GetStackTrace() {
    constexpr size_t MAX_NUM_FRAMES = 64;
    void* return_addresses[MAX_NUM_FRAMES + 1];

    size_t num_frames = backtrace(return_addresses, MAX_NUM_FRAMES);
    if (num_frames == 0)
        return "";

    char** symbols = backtrace_symbols(return_addresses, num_frames);

    std::string ret;
    for (size_t i = 0; i < num_frames; i++) {
        fprintf(stderr, "%s\n", symbols[i]);
        ret += symbols[i];
        ret += "\n";
    }
    return ret;
}

#ifdef __APPLE__
static void OSXMessageBox(const std::string& title, const std::string& message) {
    // This implementation leaks memory, but at this point we don't really care.
    // Casting objc_msgSend is recommended to ensure correct semantics (it doesn't use vararg semantics as declared).
    CFStringRef cftitle = CFStringCreateWithCString(nullptr, title.c_str(), kCFStringEncodingMacRoman);
    CFStringRef cfmessage = CFStringCreateWithCString(nullptr, message.c_str(), kCFStringEncodingMacRoman);
    id alert = reinterpret_cast<id(*)(Class, SEL)>(objc_msgSend)(objc_getClass("NSAlert"), sel_registerName("alloc"));
    alert = reinterpret_cast<id(*)(id, SEL)>(objc_msgSend)(alert, sel_registerName("init"));
    reinterpret_cast<void(*)(id, SEL, int)>(objc_msgSend)(alert, sel_getUid("setAlertStyle:"), 1);
    reinterpret_cast<void(*)(id, SEL, CFStringRef)>(objc_msgSend)(alert, sel_getUid("setMessageText:"), cftitle);
    reinterpret_cast<void(*)(id, SEL, CFStringRef)>(objc_msgSend)(alert, sel_getUid("setInformativeText:"), cfmessage);
    reinterpret_cast<int(*)(id, SEL)>(objc_msgSend)(alert, sel_getUid("runModal"));
}
#endif

} // namespace CrashHandler

#endif
