#ifndef __DB_CONNECTOR_HEADER__
#define __DB_CONNECTOR_HEADER__

namespace MonLib
{
    class CDBConnector
    {
    public:
        enum DB_CONNECTOR
        {
            ERROR_MSG_MAX_LEN = 128,
            QUERY_MAX_LEN = 2048
        };

        CDBConnector(WCHAR *p_DBIP, WCHAR *p_DBUser, WCHAR *p_DBPassword, WCHAR *p_DBName, int DBPort);
        virtual ~CDBConnector(void);

        bool Connect(void);
        bool Disconnect(void);

        bool QuerySelect(WCHAR *p_StringFormat, ...);
        bool QuerySave(WCHAR *p_StringFormat, ...);

        bool ExcuteQuerySelect(void);
        bool ExcuteQuerySave(void);

        MYSQL_ROW FetchRow(void);
        void FreeResult(void);

        int GetLastError(void) { return _LastError; };
        WCHAR *GetLastErrorMsg(void) { return _LastErrorMsg; };

        bool IsConnect(void);
        WCHAR *GetQueryPtr(void);

    private:
        void SaveLastError(void);

    private:
        MYSQL           _MySQL;

        MYSQL           *_p_MySQL;

        MYSQL_RES       *_p_SqlResult;

        WCHAR           _DBIP[IP_V4_MAX_LEN + 1];
        WCHAR           _DBUser[DB_USER_MAX_LEN + 1];
        WCHAR           _DBPassword[DB_PASSWORD_MAX_LEN + 1];
        WCHAR           _DBName[DB_NAME_MAX_LEN + 1];
        int             _DBPort;

        WCHAR           _Query[QUERY_MAX_LEN];
        char            _QueryUTF8[QUERY_MAX_LEN];

        unsigned int    _LastError;
        WCHAR           _LastErrorMsg[ERROR_MSG_MAX_LEN];
    };
}

#endif