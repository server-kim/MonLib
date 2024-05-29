#include "LibHeader.h"

// 본 클래스는 철저히 수동적이기 때문에 사용자측에서 세심한 에러처리가 필요하다.
// 아니면 래핑 클래스를 두어 사용하는 것도 좋다.

namespace MonLib
{
    CDBConnector::CDBConnector(WCHAR *p_DBIP, WCHAR *p_DBUser, WCHAR *p_DBPassword, WCHAR *p_DBName, int DBPort)
    {
        if (nullptr == p_DBIP || nullptr == p_DBUser || nullptr == p_DBPassword || nullptr == p_DBName)
            CrashDump::Crash();

        HRESULT hResult;

        //mysql_init(&_MySQL);

        _p_MySQL = nullptr;
        _p_SqlResult = nullptr;

        hResult = StringCchCopyW(_DBIP, IP_V4_MAX_LEN, p_DBIP);
        if (hResult != S_OK)
            CrashDump::Crash();
        hResult = StringCchCopyW(_DBUser, DB_USER_MAX_LEN, p_DBUser);
        if (hResult != S_OK)
            CrashDump::Crash();
        hResult = StringCchCopyW(_DBPassword, DB_PASSWORD_MAX_LEN, p_DBPassword);
        if (hResult != S_OK)
            CrashDump::Crash();
        hResult = StringCchCopyW(_DBName, DB_NAME_MAX_LEN, p_DBName);
        if (hResult != S_OK)
            CrashDump::Crash();
        _DBPort = DBPort;

        wmemset(_Query, 0, QUERY_MAX_LEN);
        memset(_QueryUTF8, 0, QUERY_MAX_LEN);

        _LastError = 0;
        wmemset(_LastErrorMsg, 0, ERROR_MSG_MAX_LEN);
    }
    CDBConnector::~CDBConnector(void)
    {
        Disconnect();
    }

    bool CDBConnector::Connect(void)
    {
        if (_p_MySQL != nullptr)
            Disconnect();           // 이미 접속이 끊겼다고 판단할 때 호출하므로 기존 커넥트를 해제한다.

        char DBIP[IP_V4_MAX_LEN + 1];
        char DBUser[64];
        char DBPassword[64];
        char DBName[64];

        if (false == UTF16toUTF8(_DBIP, DBIP, IP_V4_MAX_LEN + 1))
            return false;
        if (false == UTF16toUTF8(_DBUser, DBUser, DB_USER_MAX_LEN + 1))
            return false;
        if (false == UTF16toUTF8(_DBPassword, DBPassword, DB_PASSWORD_MAX_LEN + 1))
            return false;
        if (false == UTF16toUTF8(_DBName, DBName, DB_NAME_MAX_LEN + 1))
            return false;

        mysql_init(&_MySQL);

        _p_MySQL = mysql_real_connect(&_MySQL, DBIP, DBUser, DBPassword, DBName, _DBPort, (char *)NULL, 0);
        if (NULL == _p_MySQL)
        {
            //fprintf(stderr, "[DB Connect Error]%s\n", mysql_error(&_MySQL));
            SaveLastError();
            return false;
        }

        // 한글 설정
        mysql_set_character_set(_p_MySQL, "utf8");

        return true;
    }
    bool CDBConnector::Disconnect(void)
    {
        if (nullptr == _p_MySQL)
            return false;

        mysql_close(_p_MySQL);
        _p_MySQL = nullptr;

        return true;
    }

    bool CDBConnector::QuerySelect(WCHAR *p_StringFormat, ...)
    {
        if (nullptr == p_StringFormat)
        {
            CrashDump::Crash();
            return false;
        }

        //int Retry = 5;
        //int Ret;

        HRESULT hResult;
        va_list va;
        va_start(va, p_StringFormat);
        hResult = StringCchVPrintf(_Query, QUERY_MAX_LEN, p_StringFormat, va);
        va_end(va);

        if (hResult != S_OK)
        {
            CrashDump::Crash();
            return false;
        }

        return ExcuteQuerySelect();

        //if (false == UTF16toUTF8(_Query, _QueryUTF8, QUERY_MAX_LEN))
        //{
        //    // 쿼리를 못날리는 상황이므로 뻑내야 할 듯 하다.
        //    CrashDump::Crash();
        //    return false;
        //}
        //
        //if (nullptr == _p_MySQL)
        //    Connect();
        //
        //while (1)
        //{
        //    if (--Retry < 0)
        //    {
        //        CrashDump::Crash();
        //        return false;
        //    }
        //
        //    if (nullptr == _p_MySQL)
        //        Connect();
        //
        //    Ret = mysql_query(_p_MySQL, _QueryUTF8);
        //    if (Ret != 0)
        //    {
        //        _LastError = mysql_errno(&_MySQL);
        //
        //        // 커넥션이 끊겼을 때 재시도 처리.
        //        if (_LastError == CR_SOCKET_CREATE_ERROR ||
        //            _LastError == CR_CONNECTION_ERROR ||
        //            _LastError == CR_CONN_HOST_ERROR ||
        //            _LastError == CR_SERVER_GONE_ERROR ||
        //            _LastError == CR_TCP_CONNECTION ||
        //            _LastError == CR_SERVER_HANDSHAKE_ERR ||
        //            _LastError == CR_SERVER_LOST ||
        //            _LastError == CR_INVALID_CONN_HANDLE)
        //        {
        //            // 로그 찍어준다.
        //            SYSLOG(L"DB Connector", LOG_SYSTEM, L"DB_Disconnect # QuerySelect [ErrorCode:%u]", _LastError);
        //            Disconnect();
        //            Connect();
        //            continue;
        //        }
        //
        //        // 만약 여기까지 오면 에러코드 및 쿼리 전부를 로그로 찍어줘야 한다.(문법 에러일 가능성 높음.)
        //        // 크래시를 내는건 선택사항.
        //        // QuerySave에서는 무조건 죽여야 한다. 절대 진행하면 안됨 !!!
        //        SYSLOG(L"DB Connector", LOG_SYSTEM, L"DB_Error # QuerySelect [ErrorCode:%u][Query:%s]", _LastError, _Query);
        //
        //        CrashDump::Crash();
        //        return false;
        //    }
        //
        //    // debug로 쿼리 로그 찍어줄 것.
        //
        //    //FreeResult();
        //    _p_SqlResult = mysql_use_result(_p_MySQL);
        //    break;
        //}
        //
        //return true;
    }
    bool CDBConnector::QuerySave(WCHAR *p_StringFormat, ...)
    {
        if (nullptr == p_StringFormat)
        {
            CrashDump::Crash();
            return false;
        }

        //int Retry = 5;
        //int Ret;

        HRESULT hResult;
        va_list va;
        va_start(va, p_StringFormat);
        hResult = StringCchVPrintf(_Query, QUERY_MAX_LEN, p_StringFormat, va);
        va_end(va);

        if (hResult != S_OK)
        {
            CrashDump::Crash();
            return false;
        }

        return ExcuteQuerySave();

        //if (false == UTF16toUTF8(_Query, _QueryUTF8, QUERY_MAX_LEN))
        //{
        //    // 쿼리를 못날리는 상황이므로 뻑내야 할 듯 하다.
        //    CrashDump::Crash();
        //    return false;
        //}
        //
        //if (nullptr == _p_MySQL)
        //    Connect();
        //
        //while (1)
        //{
        //    if (--Retry < 0)
        //    {
        //        CrashDump::Crash();
        //        return false;
        //    }
        //
        //    if (nullptr == _p_MySQL)
        //        Connect();
        //
        //    Ret = mysql_query(_p_MySQL, _QueryUTF8);
        //    if (Ret != 0)
        //    {
        //        _LastError = mysql_errno(&_MySQL);
        //
        //        // 커넥션이 끊겼을 때 재시도 처리.
        //        if (_LastError == CR_SOCKET_CREATE_ERROR ||
        //            _LastError == CR_CONNECTION_ERROR ||
        //            _LastError == CR_CONN_HOST_ERROR ||
        //            _LastError == CR_SERVER_GONE_ERROR ||
        //            _LastError == CR_TCP_CONNECTION ||
        //            _LastError == CR_SERVER_HANDSHAKE_ERR ||
        //            _LastError == CR_SERVER_LOST ||
        //            _LastError == CR_INVALID_CONN_HANDLE)
        //        {
        //            // 로그 찍어준다.
        //            SYSLOG(L"DB Connector", LOG_SYSTEM, L"DB_Disconnect # QuerySave [ErrorCode:%u]", _LastError);
        //            Disconnect();
        //            Connect();
        //            continue;
        //        }
        //
        //        // 만약 여기까지 오면 에러코드 및 쿼리 전부를 로그로 찍어줘야 한다.(문법 에러일 가능성 높음.)
        //        // 크래시를 내는건 선택사항.
        //        // QuerySave에서는 무조건 죽여야 한다. 절대 진행하면 안됨 !!!
        //        SYSLOG(L"DB Connector", LOG_SYSTEM, L"DB_Error # QuerySave [ErrorCode:%u][Query:%s]", _LastError, _Query);
        //
        //        CrashDump::Crash();
        //        return false;
        //    }
        //
        //    // debug로 쿼리 로그 찍어줄 것.
        //
        //    //FreeResult();
        //    //_p_SqlResult = mysql_use_result(_p_MySQL);
        //    break;
        //}
        //
        //return true;
    }

    bool CDBConnector::ExcuteQuerySelect(void)
    {
        if (nullptr == _Query)
        {
            CrashDump::Crash();
            return false;
        }

        int Retry = 5;
        int Ret;

        if (false == UTF16toUTF8(_Query, _QueryUTF8, QUERY_MAX_LEN))
        {
            // 쿼리를 못날리는 상황이므로 뻑내야 할 듯 하다.
            CrashDump::Crash();
            return false;
        }

        if (nullptr == _p_MySQL)
            Connect();

        while (1)
        {
            if (--Retry < 0)
            {
                CrashDump::Crash();
                return false;
            }

            if (nullptr == _p_MySQL)
                Connect();

            Ret = mysql_query(_p_MySQL, _QueryUTF8);
            if (Ret != 0)
            {
                _LastError = mysql_errno(&_MySQL);

                // 커넥션이 끊겼을 때 재시도 처리.
                if (_LastError == CR_SOCKET_CREATE_ERROR ||
                    _LastError == CR_CONNECTION_ERROR ||
                    _LastError == CR_CONN_HOST_ERROR ||
                    _LastError == CR_SERVER_GONE_ERROR ||
                    _LastError == CR_TCP_CONNECTION ||
                    _LastError == CR_SERVER_HANDSHAKE_ERR ||
                    _LastError == CR_SERVER_LOST ||
                    _LastError == CR_INVALID_CONN_HANDLE)
                {
                    // 로그 찍어준다.
                    SYSLOG(L"DB Connector", LOG_SYSTEM, L"DB_Disconnect # QuerySelect [ErrorCode:%u]", _LastError);
                    Disconnect();
                    Connect();
                    continue;
                }

                // 만약 여기까지 오면 에러코드 및 쿼리 전부를 로그로 찍어줘야 한다.(문법 에러일 가능성 높음.)
                // 크래시를 내는건 선택사항.
                // QuerySave에서는 무조건 죽여야 한다. 절대 진행하면 안됨 !!!
                SYSLOG(L"DB Connector", LOG_SYSTEM, L"DB_Error # QuerySelect [ErrorCode:%u][Query:%s]", _LastError, _Query);

                CrashDump::Crash();
                return false;
            }

            // debug로 쿼리 로그 찍어줄 것.

            //FreeResult();
            _p_SqlResult = mysql_use_result(_p_MySQL);
            break;
        }

        return true;
    }
    bool CDBConnector::ExcuteQuerySave(void)
    {
        if (nullptr == _Query)
        {
            CrashDump::Crash();
            return false;
        }

        int Retry = 5;
        int Ret;

        if (false == UTF16toUTF8(_Query, _QueryUTF8, QUERY_MAX_LEN))
        {
            // 쿼리를 못날리는 상황이므로 뻑내야 할 듯 하다.
            CrashDump::Crash();
            return false;
        }

        if (nullptr == _p_MySQL)
            Connect();

        while (1)
        {
            if (--Retry < 0)
            {
                CrashDump::Crash();
                return false;
            }

            if (nullptr == _p_MySQL)
                Connect();

            Ret = mysql_query(_p_MySQL, _QueryUTF8);
            if (Ret != 0)
            {
                _LastError = mysql_errno(&_MySQL);

                // 커넥션이 끊겼을 때 재시도 처리.
                if (_LastError == CR_SOCKET_CREATE_ERROR ||
                    _LastError == CR_CONNECTION_ERROR ||
                    _LastError == CR_CONN_HOST_ERROR ||
                    _LastError == CR_SERVER_GONE_ERROR ||
                    _LastError == CR_TCP_CONNECTION ||
                    _LastError == CR_SERVER_HANDSHAKE_ERR ||
                    _LastError == CR_SERVER_LOST ||
                    _LastError == CR_INVALID_CONN_HANDLE)
                {
                    // 로그 찍어준다.
                    SYSLOG(L"DB Connector", LOG_SYSTEM, L"DB_Disconnect # QuerySave [ErrorCode:%u]", _LastError);
                    Disconnect();
                    Connect();
                    continue;
                }

                // 만약 여기까지 오면 에러코드 및 쿼리 전부를 로그로 찍어줘야 한다.(문법 에러일 가능성 높음.)
                // 크래시를 내는건 선택사항.
                // QuerySave에서는 무조건 죽여야 한다. 절대 진행하면 안됨 !!!
                SYSLOG(L"DB Connector", LOG_SYSTEM, L"DB_Error # QuerySave [ErrorCode:%u][Query:%s]", _LastError, _Query);

                CrashDump::Crash();
                return false;
            }

            // debug로 쿼리 로그 찍어줄 것.

            //FreeResult();
            //_p_SqlResult = mysql_use_result(_p_MySQL);
            break;
        }

        return true;
    }

    MYSQL_ROW CDBConnector::FetchRow(void)
    {
        if (_p_MySQL != nullptr && _p_SqlResult != nullptr)
        {
            return mysql_fetch_row(_p_SqlResult);

            //MYSQL_ROW Row;
            //if ((Row = mysql_fetch_row(_p_SqlResult)) != NULL)
            //{
            //    return Row;
            //}
            //return nullptr;
        }
        return nullptr;
    }
    void CDBConnector::FreeResult(void)
    {
        if (_p_SqlResult != nullptr)
            mysql_free_result(_p_SqlResult);
        _p_SqlResult = nullptr;
    }

    void CDBConnector::SaveLastError(void)
    {
        _LastError = mysql_errno(&_MySQL);
        if (false == UTF8toUTF16(mysql_error(&_MySQL), _LastErrorMsg, ERROR_MSG_MAX_LEN))
        {
            // 에러처리 어떻게?
        }
    }

    bool CDBConnector::IsConnect(void)
    {
        if (_p_MySQL != nullptr)
            return true;
        return false;
    }

    WCHAR *CDBConnector::GetQueryPtr(void)
    {
        return _Query;
    }
}