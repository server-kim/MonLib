#ifndef __SYSTEM_LOG_HEADER__
#define __SYSTEM_LOG_HEADER__

#define SYSLOG(type, level, formatstring, ...) MonLib::g_SystemLOG->Log(type, level, formatstring, ##__VA_ARGS__)
#define SYSLOG_HEX(type, level, logstring, byte, len) MonLib::g_SystemLOG->LogHex(type, level, logstring, byte, len)
#define SYSLOG_SESSIONKEY(type, level, logstring, byte) MonLib::g_SystemLOG->LogSessionKey(type, level, logstring, byte)
#define SYSLOG_LEVEL(level) MonLib::g_SystemLOG->SetLogLevel(level)
#define SYSLOG_DIRECTORY(Dir) MonLib::g_SystemLOG->SetLogDirectory(Dir)

namespace MonLib
{
    enum LOG_OPT_Define
    {
        LOG_CONSOLE     = 0x01,
        LOG_FILE        = 0x02,
        LOG_DB          = 0x04,
        LOG_WEB         = 0x08
    };

    enum LOG_LEVEL_Define
    {
        LOG_LEVEL_DEFAULT = 0,
        LOG_DEBUG,
        LOG_WARNING,
        LOG_ERROR,
        LOG_SYSTEM
    };

    enum LOG_BUFF_Define
    {
        ARG_BUFF_SIZE = 500,
        MESSAGE_BUFF_SIZE = 600,
        QUERY_BUFF_SIZE = 1000,
        QUERY_TABLE_NAME_SIZE = 20
    };

    class SystemLog
    {
    public:
    private:
        SystemLog(BYTE LogOpt, LOG_LEVEL_Define LogLevel);
        ~SystemLog(void);

        void LogConsole(WCHAR *p_Message);
        void LogFile(WCHAR *p_Message, WCHAR *p_Type);
        void LogDB(WCHAR *p_Message);
        void LogWeb(WCHAR *p_Message);
    public:
        static SystemLog *GetInstance(BYTE LogOpt = LOG_CONSOLE | LOG_FILE, LOG_LEVEL_Define LogLevel = LOG_DEBUG)
        {
            static SystemLog Log(LogOpt, LogLevel);
            return &Log;
        }

        void LogSet(BYTE LogOpt, LOG_LEVEL_Define LogLevel)
        {
            _SaveLogOption = LogOpt;
            _SaveLogLevel = LogLevel;
        }
        void SetLogOption(BYTE LogOpt)
        {
            _SaveLogOption = LogOpt;
        }
        void SetLogLevel(LOG_LEVEL_Define LogLevel)
        {
            if (LOG_LEVEL_DEFAULT < LogLevel && LOG_SYSTEM > LogLevel)
                _SaveLogLevel = LogLevel;
        }

        void SetLogDirectory(WCHAR *p_Directory)
        {
            _wmkdir(p_Directory);
            wsprintf(_SaveDirectory, L"%s\\", p_Directory);
        }

        void Log(WCHAR *p_Type, LOG_LEVEL_Define LogLevel, WCHAR *p_StringFormat, ...);
        void LogHex(WCHAR *p_Type, LOG_LEVEL_Define LogLevel, WCHAR *p_Log, BYTE *p_Byte, int ByteLen);
        void LogSessionKey(WCHAR *p_Type, LOG_LEVEL_Define LogLevel, WCHAR *p_Log, BYTE *p_SessionKey);
    private:
        unsigned long _LogNo;
        //SRWLOCK _srwLock;

        BYTE _SaveLogOption;
        LOG_LEVEL_Define _SaveLogLevel;
        WCHAR _SaveDirectory[MAX_PATH];

        // LogFile
        CRITICAL_SECTION _LogFile_cs;

        // lock
        SRWLOCK _SystemLog_srwlock;
    };

    extern SystemLog *g_SystemLOG;
}

#endif