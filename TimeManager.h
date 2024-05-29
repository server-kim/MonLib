#ifndef __TIME_MANAGER_HEADER__
#define __TIME_MANAGER_HEADER__

//--------------------------------------
// 컨텐츠용으로 사용하는 Time Class
//--------------------------------------

namespace MonLib
{
    class TimeManager
    {
    private:
        enum TIME_MANAGER_DEFINE
        {
            TIME_MANAGER_UPDATE_TICK = 10
        };

        DWORD _TickCount;
        ULONGLONG _TickCount64;

        //static TimeManager *_TimeManager;

        bool _Shutdown;
        HANDLE _TimeManagerThread;
        unsigned int _TimeManagerThreadId;

        TimeManager(void)
        {
            timeBeginPeriod(1);
            _Shutdown = false;
            _TickCount = GetTickCount();
            _TickCount64 = GetTickCount64();

            _TimeManagerThread = (HANDLE)_beginthreadex(NULL, 0, TimeManagerThread, this, 0, &_TimeManagerThreadId);
            if (INVALID_HANDLE_VALUE == _TimeManagerThread)
                CrashDump::Crash();
        }

        static unsigned _stdcall TimeManagerThread(LPVOID lpParam)
        {
            return ((TimeManager *)lpParam)->TimeManagerThread_update();
        }
        unsigned TimeManagerThread_update(void)
        {
            while (!_Shutdown)
            {
                UpdateFrameTime();
                Sleep(TIME_MANAGER_UPDATE_TICK);
            }
            return 0;
        }

        //static unsigned _stdcall TimeManagerThread(LPVOID lpParam)
        //{
        //    TimeManager *p_TimeManager = (TimeManager *)lpParam;
        //    while (1)
        //    {
        //        p_TimeManager->UpdateFrameTime();
        //        Sleep(TIME_MANAGER_UPDATE_TICK);
        //    }
        //    return 0;
        //}

    public:
        
        virtual ~TimeManager(void)
        {
            //_Shutdown = true;
            //if (WaitForSingleObject(_TimeManagerThread, 5000) != WAIT_OBJECT_0)
            //{
            //    TerminateThread(_TimeManagerThread, 0);
            //}
            TerminateThread(_TimeManagerThread, 0);
            timeEndPeriod(1);
        }

        //static TimeManager *GetInstance(void)
        //{
        //    //if (nullptr == _TimeManager)
        //    //    _TimeManager = new TimeManager;
        //    return _TimeManager;
        //}

        static TimeManager* GetInstance(void)
        {
            static TimeManager _TimeManager;
            return &_TimeManager;
        }

        void UpdateFrameTime(void)
        {
            _TickCount = GetTickCount();
            _TickCount64 = GetTickCount64();
        }

        DWORD GetTickTime(void)
        {
            return _TickCount;
        }
        ULONGLONG GetTickTime64(void)
        {
            return _TickCount64;
        }
    };
}

#endif