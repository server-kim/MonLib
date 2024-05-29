#include "LibHeader.h"

// 헤더파일 정리하기.

namespace MonLib
{
    SystemLog *g_SystemLOG = SystemLog::GetInstance();

    const WCHAR *LOG_DEBUG_STR = L"DEBUG";
    const WCHAR *LOG_WARNING_STR = L"WARNG";
    const WCHAR *LOG_ERROR_STR = L"ERROR";
    const WCHAR *LOG_SYSTEM_STR = L"SYSTM";

    // LogDB
    char *DBHost = "127.0.0.1";
    short DBPort = 3306;
    char *DBUser = "testuser";
    char *DBPass = "test1234";
    char *DBName = "LogTest";

    SystemLog::SystemLog(BYTE LogOpt, LOG_LEVEL_Define LogLevel)
    {
        _LogNo = 0;
        //_srwLock

        _SaveLogOption = LogOpt;
        _SaveLogLevel = LogLevel;
        //wsprintf(_SaveDirectory, L"C:\\TestSave\\");
        wsprintf(_SaveDirectory, L"");      // 현재 폴더

        InitializeCriticalSection(&_LogFile_cs);

        InitializeSRWLock(&_SystemLog_srwlock);
    }
    SystemLog::~SystemLog(void)
    {

    }

    void SystemLog::LogConsole(WCHAR *p_Message)
    {
        wprintf(L"%s", p_Message);
    }
    void SystemLog::LogFile(WCHAR *p_Message, WCHAR *p_Type)
    {
        int Cnt;
        int FileError;
        FILE *p_File;
        WCHAR FileName[1000];
        SYSTEMTIME st_NowTime;

        // 파일이름 예시 : 201509_Battle.txt
        GetLocalTime(&st_NowTime);
        wsprintf(FileName, L"%s%d%02d_%s.txt", _SaveDirectory, st_NowTime.wYear, st_NowTime.wMonth, p_Type);

        EnterCriticalSection(&_LogFile_cs);
        //FileError = 0;
        for (Cnt = 0; Cnt < 5; ++Cnt)
        {
            FileError = _wfopen_s(&p_File, FileName, L"a, ccs=UNICODE");
            if (0 == FileError)
            {
                //fwrite(p_Message, wcslen(p_Message) * sizeof(WCHAR), 1, p_File);
                fputws(p_Message, p_File);
                fclose(p_File);
                break;
            }
        }
        LeaveCriticalSection(&_LogFile_cs);
    }
    void SystemLog::LogDB(WCHAR *p_Message)
    {
        MYSQL *p_Conn = NULL;
        MYSQL Conn;
        int Result;
        unsigned int ErrorNo;

        char UTF8MessageBuff[MESSAGE_BUFF_SIZE * 2];
        char QueryBuff[QUERY_BUFF_SIZE];
        char TableName[QUERY_TABLE_NAME_SIZE];

        SYSTEMTIME st_NowTime;

        mysql_init(&Conn);
        p_Conn = mysql_real_connect(&Conn, DBHost, DBUser, DBPass, DBName, DBPort, (char *)NULL, 0);
        if (NULL == p_Conn)
        {
            wprintf(L"LOG_DB_Connect_Error\n");
            return;
        }
        mysql_set_character_set(p_Conn, "UTF-8");

        // 테이블 이름 예시 : System_Log_201510
        GetLocalTime(&st_NowTime);
        sprintf_s(TableName, "SystemLog_%d%02d", st_NowTime.wYear, st_NowTime.wMonth);

        UTF16toUTF8(p_Message, UTF8MessageBuff, QUERY_BUFF_SIZE);
        StringCchPrintfA(QueryBuff, QUERY_BUFF_SIZE, "INSERT INTO %s (date, AccountNo, Action, Message) VALUES (now(), %d, %d, '%s');", TableName, 1, 2, UTF8MessageBuff);
        //sprintf_s(QueryBuff, "INSERT INTO %s (date, AccountNo, Action, Message) VALUES (now(), %d, %d, %s);", TableName, 1, 2, QueryBuff);

        Result = mysql_query(p_Conn, QueryBuff);
        if (Result != 0)
        {
            ErrorNo = mysql_errno(p_Conn);
            if (1146 == ErrorNo)
            {
                char CreateTableBuff[QUERY_BUFF_SIZE];
                StringCchPrintfA(CreateTableBuff, QUERY_BUFF_SIZE, "CREATE TABLE %s LIKE SystemLog_template;", TableName);
                //sprintf_s(TableName, "CREATE TABLE %s LIKE SystemLog_template;", TableName);
                mysql_query(p_Conn, CreateTableBuff);
                mysql_query(p_Conn, QueryBuff);
            }
            else
            {
                printf("LOG_DB_Query_Error : %s\n", mysql_error(p_Conn));
            }
        }
        mysql_close(p_Conn);
    }
    void SystemLog::LogWeb(WCHAR *p_Message)
    {

    }

    void SystemLog::Log(WCHAR *p_Type, LOG_LEVEL_Define LogLevel, WCHAR *p_StringFormat, ...)
    {
        if (LogLevel < _SaveLogLevel)
            return;

        AcquireSRWLockExclusive(&_SystemLog_srwlock);

        HRESULT hResult;
        const WCHAR *p_LogLevelStr;
        WCHAR ArgBuff[ARG_BUFF_SIZE];
        WCHAR Message[MESSAGE_BUFF_SIZE];

        va_list va;
        va_start(va, p_StringFormat);
        hResult = StringCchVPrintf(ArgBuff, ARG_BUFF_SIZE, p_StringFormat, va);
        //hResult = StringCchPrintf(ArgBuff, 500, L"[%s] [%d-%02d-%02d %02d:%02d:%02d / %s] %s\n",
        //    p_Type, st_NowTime.wYear, st_NowTime.wMonth, st_NowTime.wDay, st_NowTime.wHour, st_NowTime.wMinute, st_NowTime.wSecond, p_LogLevelStr, va_arg(va, WCHAR *));
        va_end(va);

        if (TRUE == FAILED(hResult))
        {
            memcpy_s(ArgBuff, ARG_BUFF_SIZE, p_StringFormat, 400);      // 포맷만이라도 로그를 남기자.
        }

        p_LogLevelStr = nullptr;
        switch (LogLevel)
        {
        case LOG_DEBUG:
            p_LogLevelStr = LOG_DEBUG_STR;
            break;
        case LOG_WARNING:
            p_LogLevelStr = LOG_WARNING_STR;
            break;
        case LOG_ERROR:
            p_LogLevelStr = LOG_ERROR_STR;
            break;
        case LOG_SYSTEM:
            p_LogLevelStr = LOG_SYSTEM_STR;
            break;
        default:
            p_LogLevelStr = nullptr;       // 일단 뻗는다.
            break;
        }

        SYSTEMTIME st_NowTime;
        GetLocalTime(&st_NowTime);

        unsigned long LogNo = InterlockedIncrement(&_LogNo);

        if (LOG_CONSOLE & _SaveLogOption)
        {
            //wprintf(L"CONSOLE LOG !!\n");
            hResult = StringCchPrintf(Message, MESSAGE_BUFF_SIZE, L"[%s] [%d-%02d-%02d %02d:%02d:%02d / %s / %09d] %s\n",
                p_Type, st_NowTime.wYear, st_NowTime.wMonth, st_NowTime.wDay, st_NowTime.wHour, st_NowTime.wMinute, st_NowTime.wSecond, p_LogLevelStr, LogNo, ArgBuff);
            LogConsole(Message);
        }
        if (LOG_FILE & _SaveLogOption)
        {
            //wprintf(L"FILE LOG !!\n");
            hResult = StringCchPrintf(Message, MESSAGE_BUFF_SIZE, L"[%s] [%d-%02d-%02d %02d:%02d:%02d / %s / %09d] %s\n",
                p_Type, st_NowTime.wYear, st_NowTime.wMonth, st_NowTime.wDay, st_NowTime.wHour, st_NowTime.wMinute, st_NowTime.wSecond, p_LogLevelStr, LogNo, ArgBuff);
            LogFile(Message, p_Type);
        }
        if (LOG_DB & _SaveLogOption)
        {
            //wprintf(L"DB LOG !!\n");
            hResult = StringCchPrintf(Message, MESSAGE_BUFF_SIZE, L"[%s] [%d-%02d-%02d %02d:%02d:%02d / %s / %09d] %s\n",
                p_Type, st_NowTime.wYear, st_NowTime.wMonth, st_NowTime.wDay, st_NowTime.wHour, st_NowTime.wMinute, st_NowTime.wSecond, p_LogLevelStr, LogNo, ArgBuff);
            LogDB(Message);
        }
        if (LOG_WEB & _SaveLogOption)
        {
            wprintf(L"WEB LOG !!\n");
            hResult = StringCchPrintf(Message, MESSAGE_BUFF_SIZE, L"[%s] [%d-%02d-%02d %02d:%02d:%02d / %s / %09d] %s\n",
                p_Type, st_NowTime.wYear, st_NowTime.wMonth, st_NowTime.wDay, st_NowTime.wHour, st_NowTime.wMinute, st_NowTime.wSecond, p_LogLevelStr, LogNo, ArgBuff);
            LogWeb(Message);
        }

        ReleaseSRWLockExclusive(&_SystemLog_srwlock);
    }
    void SystemLog::LogHex(WCHAR *p_Type, LOG_LEVEL_Define LogLevel, WCHAR *p_Log, BYTE *p_Byte, int ByteLen)
    {
        if (LogLevel < _SaveLogLevel)
            return;
    }
    void SystemLog::LogSessionKey(WCHAR *p_Type, LOG_LEVEL_Define LogLevel, WCHAR *p_Log, BYTE *p_SessionKey)
    {
        if (LogLevel < _SaveLogLevel)
            return;
    }
}