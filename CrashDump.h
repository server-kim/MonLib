#ifndef __CRASH_DUMP_HEADER__
#define __CRASH_DUMP_HEADER__

namespace MonLib
{
    class CrashDump
    {
    public:
        CrashDump(void)
        {
            _DumpCount = 0;

            _invalid_parameter_handler OldHandler;
            _invalid_parameter_handler NewHandler;

            // 인자값 유효성
            NewHandler = myInvalidParameterHandler;
            OldHandler = _set_invalid_parameter_handler(NewHandler);

            // CRT 에러
            _CrtSetReportMode(_CRT_WARN, 0);
            _CrtSetReportMode(_CRT_ASSERT, 0);
            _CrtSetReportMode(_CRT_ERROR, 0);

            _CrtSetReportHook(_custom_Report_hook);

            // 순수 가상함수
            _set_purecall_handler(myPurecallHandler);

            SetHandlerDump();
        }

        static void Crash(void)
        {
            int *p = nullptr;
            *p = 0;
        }

        static LONG WINAPI MyExceptionFilter(__in PEXCEPTION_POINTERS p_ExceptionPointer)
        {
            int WorkingMemory = 0;
            SYSTEMTIME st_NowTime;

            long DumpCount = InterlockedIncrement(&_DumpCount);

            // 현재 프로세스의 메모리 사용량 얻어오기
            HANDLE hProcess = 0;
            PROCESS_MEMORY_COUNTERS pmc;

            hProcess = GetCurrentProcess();
            if (NULL == hProcess)
                return 0;

            if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
            {
                WorkingMemory = (int)(pmc.WorkingSetSize / 1024 / 1024);
            }
            CloseHandle(hProcess);

            // 현재 날짜와 시간을 알아와서 파일이름 생성
            WCHAR filename[MAX_PATH];

            GetLocalTime(&st_NowTime);
            wsprintf(filename, L"Dump_%d%02d%02d_%02d.%02d.%02d_%d_%dMB.dmp",
                st_NowTime.wYear, st_NowTime.wMonth, st_NowTime.wDay, st_NowTime.wHour, st_NowTime.wMinute, st_NowTime.wSecond, DumpCount, WorkingMemory);

            wprintf(L"\n\n\n!!! Crash Error !!!  %d.%d.%d / %d:%d:%d \n", st_NowTime.wYear, st_NowTime.wMonth, st_NowTime.wDay, st_NowTime.wHour, st_NowTime.wMinute, st_NowTime.wSecond);
            wprintf(L"Now Save dump file...\n");

            HANDLE hDumpFile = ::CreateFile(filename,
                GENERIC_WRITE,
                FILE_SHARE_WRITE,
                NULL,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, NULL);

            if (hDumpFile != INVALID_HANDLE_VALUE)
            {
                _MINIDUMP_EXCEPTION_INFORMATION MinidumpExceptionInfo;

                MinidumpExceptionInfo.ThreadId = GetCurrentThreadId();
                MinidumpExceptionInfo.ExceptionPointers = p_ExceptionPointer;
                MinidumpExceptionInfo.ClientPointers = TRUE;

                MiniDumpWriteDump(GetCurrentProcess(),
                    GetCurrentProcessId(),
                    hDumpFile,
                    MiniDumpWithFullMemory,
                    &MinidumpExceptionInfo,
                    NULL,
                    NULL);

                CloseHandle(hDumpFile);

                wprintf(L"CrashDump Save Finish !");
            }

            return EXCEPTION_EXECUTE_HANDLER;
        }

        static void SetHandlerDump()
        {
            SetUnhandledExceptionFilter(MyExceptionFilter);
        }

        static void myInvalidParameterHandler(const wchar_t *expression, const wchar_t *function, const wchar_t *file, unsigned int line, uintptr_t pReserved)
        {
            Crash();
        }

        static int _custom_Report_hook(int reporttype, char *message, int *returnvalue)
        {
            Crash();
            return true;
        }

        static void myPurecallHandler(void)
        {
            Crash();
        }

        static long _DumpCount;
    };
}

#endif