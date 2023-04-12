#include "precompile.h"
#include "RealTime.h"
 #include <unistd.h>
#include <sched.h>
#include <iostream>
#include <gtest/gtest.h>
#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                        } while (0)

int g_total_work{0};    // if we dont change something that might be used externally, the compiler might not compile the background work
std::atomic<int> responsetime_counter{0};
std::atomic<int> respond_now{0};

static void SetAffinity(int cpu)
{

    cpu_set_t set{};
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);

    if (sched_setaffinity(getpid(), sizeof(set), &set) == -1)
        errExit("sched_setaffinity");

}

void GetRTSchedulingPrio()
{
    sched_param sp;
    sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if(sched_setscheduler(0, SCHED_FIFO, &sp))
        errExit("sched_setscheduler");
}

void ReleaseRTSchedulingPrio()
{
    sched_param sp;
    sp.sched_priority = sched_get_priority_min(SCHED_OTHER);
    if(sched_setscheduler(0, SCHED_OTHER, &sp))
        errExit("sched_setscheduler");
}


void RealTimeEngine::ThreadWork(int cpu)
{
    using namespace std::chrono;
    using namespace std::chrono_literals;
    SetAffinity(cpu);
    
    if (m_verbose && m_runningCpu.load()==cpu)
        std::cout << "Thread" << cpu << " taking control";

    if (cpu==m_runningCpu)
    {
        GetRTSchedulingPrio();
        m_downtime -= duration_cast<milliseconds>(Clock::now().time_since_epoch()).count();
    }

    m_enteredStateTime[cpu] = Clock::now();
    while (!m_shuttingDown)
    {
        if (m_runningCpu==cpu)
        {
            if (m_verbose)
                m_log << "running on thread " << cpu << "\n";
            m_downtime += duration_cast<milliseconds>(Clock::now().time_since_epoch()).count();
            std::this_thread::sleep_for(50ms);
            m_downtime -= duration_cast<milliseconds>(Clock::now().time_since_epoch()).count();
            if (m_readyCpu.load()!=cpu)
            {
                if (m_verbose)
                    m_log << "conceeding to thread " << m_readyCpu.load() << "\n";
                ReleaseRTSchedulingPrio();
                m_runningCpu.store(m_readyCpu);
                m_enteredStateTime[cpu] = Clock::now();
                m_cpu_switches++;
            }
        }
        else if (Clock::now()-m_enteredStateTime[cpu] > m_switchTime)
        {
            m_enteredStateTime[cpu] = Clock::now();
            GetRTSchedulingPrio();
            m_readyCpu.store(cpu);
            while(m_runningCpu.load()!=cpu)
                /*busy wait*/;
        }
    }

    ReleaseRTSchedulingPrio();
    if (cpu==m_runningCpu)
    {
        m_downtime += duration_cast<milliseconds>(Clock::now().time_since_epoch()).count();
        std::cout << "Log Contents\n====================\n" << m_log.str() << "\n";;
        std::cout << "detectable downtime = " << m_downtime << "\n";
    }
}


TEST(RealTimeEngine, Initial)
{
    using namespace std::chrono_literals;
    using namespace std::chrono;

    RealTimeEngine engine{[](){
        if (respond_now.load() != 0)
        {
            responsetime_counter += steady_clock::now().time_since_epoch().count() - respond_now.load();
            respond_now = 0;
        }
    }};

    // to really test the system, lets run dome normal priority tasks to prove priority
    std::cout << "Allocating background work" << std::endl;
    std::thread background_work[10];
    std::atomic<bool> quit{false};
    for (auto& work:background_work)
        work = std::thread([&quit](){
            for(int i = 0; i < 10000000; ++i)
            {
                g_total_work += i;  // make the compiler actually emit code
                if (quit)
                    break;
            }
        });
    std::cout << "Allocated background work" << std::endl;

    const auto start_time = steady_clock::now();
    auto event_count{0};
    while ((steady_clock::now()-start_time)<10s)
    {
        event_count++;
        respond_now = steady_clock::now().time_since_epoch().count();
    }
    engine.Shutdown();
    engine.Join();
    std::cout << "Average response time = " << (float)responsetime_counter.load()/event_count;
    std::cout <<  "  ("<<responsetime_counter.load()<<"/"<<event_count<<")\n";
    std::cout <<  "Switched CPU " << engine.GetCPUSwitches() << " tomes\n";

    // success is low downtime (total), high responsiveness (total) and multiple context switches
    EXPECT_LE(responsetime_counter.load(), 100);
    EXPECT_LE(engine.GetDowntime(), 100);
    EXPECT_GE(engine.GetCPUSwitches(), 50);

    quit = true;
    for (auto& work:background_work)
        work.join();
}