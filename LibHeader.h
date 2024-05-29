#ifndef __LIB_HEADER__
#define __LIB_HEADER__

//#pragma once

// system 헤더

// c lib
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")       // getTickCount

#include <stdio.h>
#include <tchar.h>

#include <WinSock2.h>
#include <MSWSock.h>        // acceptex
#include <WS2tcpip.h>
#include <process.h>

#include <time.h>
#include <new>
#include <conio.h>
#include <mstcpip.h>
#include <mmsystem.h>       // getTickCount

// dump
#pragma comment (lib, "psapi")
#pragma comment (lib, "dbghelp")

#include <stdlib.h>
#include <DbgHelp.h>
#include <Psapi.h>
#include <crtdbg.h>

// DB
//#pragma comment(lib, "mysql/lib/vs12/mysqlclient.lib")
#pragma comment(lib, "../../../mylib/MonLib/mysql/lib/vs12/mysqlclient.lib")

#include <my_global.h>
#include <mysql.h>
#include <errmsg.h>

// log
#include <strsafe.h>

// pdh (system monitoring) - agent
#include <Pdh.h>
#include <PdhMsg.h>     // PDH_STATUS -> PDH_MORE_DATA -> define
#pragma comment(lib,"Pdh.lib")

#include <IPHlpApi.h>   // GetAdaptersAddresses
#pragma comment(lib, "iphlpapi.lib")

// stl
#include <map>
#include <list>

// define 정의 헤더
#include "Common.h"
#include "MMOServerDefine.h"

// 프로카데미 정의 헤더
#include "CommonProtocol.h"
//#include "DBTypeEnum.h"

// 사용자 정의 헤더
#include "CrashDump.h"

#include "CJPS.h"
#include "MyUtil.h"
#include "CMAP.h"

#include "TimeManager.h"

#include "Parser.h"
#include "WParser.h"
#include "LockfreeMemoryPool.h"
#include "MemoryPoolTLS.h"
#include "StreamingQueue.h"
#include "LockfreeQueue.h"
#include "LockfreeStack.h"
#include "Packet.h"
#include "Profiling.h"

#include "CDBConnector.h"

#include "SystemLog.h"

#include "CUDPModule.h"

#include "CLanServer.h"
#include "CLanClient.h"
#include "CNetServer.h"
#include "CNetServerEx.h"
#include "CNetServerEx2.h"
#include "CNetServerEx3.h"
#include "SocketPoolEx.h"
#include "CNetServerEx4.h"
#include "CNetClient.h"
//#include "CChatServer.h"

#include "CMMOServer.h"
//#include "CGameServer.h"

#define SEND_WSA_BUF_MAX 200

#define NewClientID(Index)                      ((InterlockedIncrement64(&_SessionKeyIndex) & 0x00ffffffffffffff) | (Index << 48))       // 새로운 클라이언트 아이디 만드는 매크로

#define ClientID_Index(ClientID)                ((ClientID >> 48) & 0xffff)               // 세션 인덱스 뽑는 매크로
#define ClientID_Unique(ClientID)               (ClientID & 0xffffffffffff)               // 세션 고유번호 뽑는 매크로

#endif