// Debug CRT assertions must fail unattended tests instead of opening a dialog.
#ifdef _DEBUG
#include <crtdbg.h>
#include <stdio.h>
#include <stdlib.h>

static int __cdecl reportAssertion(int reportType, char* message, int* returnValue)
{
    (void)returnValue;
    if (reportType == _CRT_ASSERT) {
        fputs(message, stderr);
        fflush(stderr);
        _Exit(3);
    }
    return 0;
}

static void __cdecl initializeAssertionReporting(void)
{
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, reportAssertion);
}

#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) void (__cdecl* const liteTestAssertionInitializer)(void) =
    initializeAssertionReporting;
#endif
