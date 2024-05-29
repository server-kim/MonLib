#include "LibHeader.h"

// 프로파일러 클래스 만들어서 정리하기.
// 디파인 없애고 enum으로 통일.

#define PROFILE_CHECK

#ifdef PROFILE_CHECK
#define PRO_BEGIN(x) ProfileBegin(x)
#define PRO_END(x) ProfileEnd(x)
#else
#define PRO_BEGIN(x)
#define PRO_END(x)
#endif

namespace MonLib
{
    enum ProfilingDefine : __int64
    {
        INT_64_MAX = 0x7fffffffffffffff,
        PROFILE_SAMPLE_MAX = 50,
        PROFILE_THREAD_MAX = 200
    };

    typedef struct
    {
        long Flag;
        WCHAR Name[17];

        LARGE_INTEGER StartTime;

        __int64 TotalTime;
        __int64 Min[2];
        __int64 Max[2];

        __int64 Call;
    } PROFILE_SAMPLE;

    typedef struct
    {
        DWORD ThreadID;
        PROFILE_SAMPLE ProfileSample[PROFILE_SAMPLE_MAX];
    } PROFILE_THREAD_SAMPLE;

    bool ProfileOn = true;
    //bool ProfileOn = false;

    bool Initial = false;

    PROFILE_THREAD_SAMPLE ProfileThread[PROFILE_THREAD_MAX];
    CRITICAL_SECTION ProfileLock;

    LARGE_INTEGER Frequency;
    double MicroFrequency;

    PROFILE_SAMPLE *GetSample(WCHAR *Name);
    int GetThreadSampleIndex(void);

    void ProfileInitial(void)
    {
        if (false == ProfileOn)
            return;

        Initial = true;
        memset(ProfileThread, 0, sizeof(PROFILE_THREAD_SAMPLE) * PROFILE_THREAD_MAX);

        QueryPerformanceFrequency(&Frequency);
        MicroFrequency = (double)Frequency.QuadPart / (double)1000000;
        InitializeCriticalSection(&ProfileLock);

        int Cnt;
        int CntSecond;
        for (Cnt = 0; Cnt < PROFILE_THREAD_MAX; ++Cnt)
        {
            for (CntSecond = 0; Cnt < PROFILE_SAMPLE_MAX; ++Cnt)
            {
                ProfileThread[Cnt].ProfileSample[CntSecond].Min[0] = INT_64_MAX;
                ProfileThread[Cnt].ProfileSample[CntSecond].Min[1] = INT_64_MAX;
            }
        }
    }

    void ProfileBegin(WCHAR *Name)
    {
        if (false == ProfileOn)
            return;

        PROFILE_SAMPLE *p_Sample = GetSample(Name);

        if (p_Sample->StartTime.QuadPart != 0)
        {
            throw 1;
        }

        QueryPerformanceCounter(&p_Sample->StartTime);
    }
    void ProfileEnd(WCHAR *Name)
    {
        if (false == ProfileOn)
            return;

        PROFILE_SAMPLE *p_Sample = GetSample(Name);

        if (p_Sample->StartTime.QuadPart == 0)
        {
            throw 1;
        }

        LARGE_INTEGER EndTime;
        QueryPerformanceCounter(&EndTime);

        __int64 SampleTime = EndTime.QuadPart - p_Sample->StartTime.QuadPart;

        p_Sample->Call++;
        p_Sample->TotalTime += SampleTime;

        if (p_Sample->Max[0] < SampleTime)
        {
            p_Sample->Max[1] = p_Sample->Max[0];
            p_Sample->Max[0] = SampleTime;
        }
        else if (p_Sample->Max[1] < SampleTime)
        {
            p_Sample->Max[1] = SampleTime;
        }

        if (SampleTime < p_Sample->Min[0])
        {
            p_Sample->Min[1] = p_Sample->Min[0];
            p_Sample->Min[0] = SampleTime;
            //if (INT_64_MAX == p_Sample->Min[1])
            //{
            //    p_Sample->Min[1] = SampleTime;
            //}
        }
        else if (SampleTime < p_Sample->Min[1])
        {
            p_Sample->Min[1] = SampleTime;
        }

        p_Sample->StartTime.QuadPart = 0;
    }

    /*
    출력 양식

    ThreadID |           Name  |     Average  |        Min   |        Max   |      Call |
    --------------------------------------------------------------------------------------
    2576  | GQCS IOComplete |     6.8928㎲ |     1.0596㎲ |  1800.9980㎲ |    378481
    2576  |    CompleteRecv |    11.6624㎲ |     3.5321㎲ |  1800.6448㎲ |    167529
    2576  |  CompleteRecv 1 |     0.0651㎲ |     0.3532㎲ |     1.4128㎲ |    556173
    2576  |  CompleteRecv 2 |     0.2950㎲ |     0.3532㎲ |    15.1879㎲ |    388644
    2576  |     PacketAlloc |     0.1507㎲ |     0.3532㎲ |    15.1879㎲ |    777288
    2576  |  CompleteRecv 3 |     0.0775㎲ |     0.0000㎲ |     2.4724㎲ |    388644
    2576  |          OnRecv |     3.2027㎲ |     1.0596㎲ |  1833.4930㎲ |    388644
    2576  |      SendPacket |     2.7163㎲ |     0.7064㎲ |  1832.7866㎲ |    388644
    2576  |        SendPost |     2.1590㎲ |     0.3532㎲ |  1832.0802㎲ |    599596
    2576  |      SendPost 1 |     0.1220㎲ |     0.3532㎲ |     8.8302㎲ |    599596
    2576  |      SendPost 2 |     0.0803㎲ |     0.0000㎲ |     3.1789㎲ |    184459
    2576  |      SendPost 3 |     5.8831㎲ |     1.4128㎲ |  1831.0205㎲ |    184459
    2576  |      PacketFree |     0.1281㎲ |     0.3532㎲ |    45.9168㎲ |   1211070
    2576  |        RecvPost |     1.9490㎲ |     1.0596㎲ |   322.4772㎲ |    167529
    2576  |    CompleteSend |     2.7709㎲ |     0.7064㎲ |   428.4390㎲ |    210952
    */

    void ProfileDataOutText(void)
    {
        if (false == ProfileOn)
            return;

        FILE *p_File;
        int FileError = _wfopen_s(&p_File, L"Profiler.txt", L"w, ccs=UTF-8");
        if (FileError != 0)
            return;

        fwprintf_s(p_File, L"  ThreadID |           Name  |     Average  |        Min   |        Max   |      Call |\n");
        fwprintf_s(p_File, L"--------------------------------------------------------------------------------------\n");

        int ThreadCnt;
        int SampleCnt;
        for (ThreadCnt = 0; ThreadCnt < PROFILE_THREAD_MAX; ++ThreadCnt)
        {
            if (0 == ProfileThread[ThreadCnt].ThreadID)
                continue;

            for (SampleCnt = 0; SampleCnt < PROFILE_SAMPLE_MAX; ++SampleCnt)
            {
                if (0 == ProfileThread[ThreadCnt].ProfileSample[SampleCnt].Flag)
                    continue;

                fwprintf_s(p_File, L"%9u  |%16s |%11.4f㎲ |%11.4f㎲ |%11.4f㎲ |%10lld\n",
                    ProfileThread[ThreadCnt].ThreadID, ProfileThread[ThreadCnt].ProfileSample[SampleCnt].Name,
                    (double)ProfileThread[ThreadCnt].ProfileSample[SampleCnt].TotalTime / (double)ProfileThread[ThreadCnt].ProfileSample[SampleCnt].Call / MicroFrequency,
                    (double)ProfileThread[ThreadCnt].ProfileSample[SampleCnt].Min[1] / MicroFrequency, (double)ProfileThread[ThreadCnt].ProfileSample[SampleCnt].Max[1] / MicroFrequency,
                    ProfileThread[ThreadCnt].ProfileSample[SampleCnt].Call);
            }
            fwprintf_s(p_File, L"--------------------------------------------------------------------------------------\n");
        }

        fclose(p_File);
    }

    PROFILE_SAMPLE *GetSample(WCHAR *Name)
    {
        int Index = GetThreadSampleIndex();

        int Cnt;
        for (Cnt = 0; Cnt < PROFILE_SAMPLE_MAX; ++Cnt)
        {
            if (ProfileThread[Index].ProfileSample[Cnt].Flag != 0)
            {
                if (0 == wcscmp(Name, ProfileThread[Index].ProfileSample[Cnt].Name))
                    return &ProfileThread[Index].ProfileSample[Cnt];
            }
        }

        for (Cnt = 0; Cnt < PROFILE_SAMPLE_MAX; ++Cnt)
        {
            if (0 == ProfileThread[Index].ProfileSample[Cnt].Flag)
            {
                ProfileThread[Index].ProfileSample[Cnt].Flag = 1;
                wcscpy_s(ProfileThread[Index].ProfileSample[Cnt].Name, Name);
                return &ProfileThread[Index].ProfileSample[Cnt];
            }
        }

        // 못찾으면 예외 던짐.
        throw 0;
    }
    int GetThreadSampleIndex(void)
    {
        DWORD ThreadId = GetCurrentThreadId();

        int Cnt;
        for (Cnt = 0; Cnt < PROFILE_THREAD_MAX; ++Cnt)
        {
            if (ThreadId == ProfileThread[Cnt].ThreadID)
                return Cnt;
        }

        EnterCriticalSection(&ProfileLock);
        for (Cnt = 0; Cnt < PROFILE_THREAD_MAX; ++Cnt)
        {
            if (0 == ProfileThread[Cnt].ThreadID)
            {
                ProfileThread[Cnt].ThreadID = ThreadId;
                LeaveCriticalSection(&ProfileLock);
                return Cnt;
            }
        }
        LeaveCriticalSection(&ProfileLock);

        // 못찾으면 예외 던짐.
        throw 0;
    }
}