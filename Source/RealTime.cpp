#include "precompile.h"
#include "RealTime.h"
 #include <unistd.h>
#include <sched.h>
#include <iostream>
#include <math.h>
#include <gtest/gtest.h>
#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                        } while (0)

int g_total_work{0};    // if we dont change something that might be used externally, the compiler might not compile the background work
std::atomic<int> responsetime_counter{0};
std::atomic<long> responsetime_max{0};
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
    sp.sched_priority = 99;
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
        std::cout << "Thread " << cpu << " taking control";

    if (cpu==m_runningCpu.load())
    {
        GetRTSchedulingPrio();
        m_downtime[cpu].Start();;
    }

    m_enteredStateTime[cpu] = Clock::now();
    while (!m_shuttingDown)
    {
        if (m_runningCpu==cpu)
        {
            if (m_verbose)
                m_log << "running on thread " << cpu << "\n";
            m_downtime[cpu].Stop();
            m_work();
            m_downtime[cpu].Start();;
            if (m_readyCpu.load()!=cpu)
            {
                m_downtime[cpu].Stop();
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
            m_downtime[cpu].Start();
        }
        else
            std::this_thread::yield();
    }
    if (cpu==m_runningCpu)
        m_downtime[cpu].Stop();

    ReleaseRTSchedulingPrio();
    if (cpu==m_runningCpu)
    {
        std::cout << "Log Contents\n====================\n" << m_log.str() << "\n";;
    }
    assert(m_downtime[0].IsStopped());
    assert(m_downtime[0].IsStopped());
    assert(m_downtime[1].IsStopped());
}


TEST(RealTimeEngine, Initial)
{
    using namespace std::chrono_literals;
    using namespace std::chrono;
    using Clock = std::chrono::steady_clock;

    RealTimeEngine engine{[](){
        if (respond_now.load(std::memory_order_acquire) != 0)
        {
            const auto elapsed = duration_cast<milliseconds>(Clock::now().time_since_epoch()).count() - respond_now.load();
            responsetime_counter += elapsed;
            responsetime_max = std::max(responsetime_max.load(), elapsed);
            respond_now.store(0, std::memory_order_release);
        }
    }};

    // to really test the system, lets run dome normal priority tasks to prove priority
    std::cout << "Allocating background work" << std::endl;
    std::thread background_work[10];
    std::atomic<bool> quit{false};
    for (auto& work:background_work)
        work = std::thread([&quit](){
            while(true)
            {
                g_total_work += std::sin(.5f);  // make the compiler actually emit code
                if (quit)
                    break;
            }
        });
    std::cout << "Allocated background work" << std::endl;

    const auto start_time = steady_clock::now();
    auto event_count{0};
    while ((steady_clock::now()-start_time)<10s)
    {
        if (respond_now.load(std::memory_order_acquire)==0)
        {
            event_count++;
            respond_now.store(duration_cast<milliseconds>(Clock::now().time_since_epoch()).count(), std::memory_order_release);
        }
    }
    engine.Shutdown();
    engine.Join();
    std::cout << "Average response time = " << (float)responsetime_counter.load()/event_count << "ms";
    std::cout <<  "  ("<<responsetime_counter.load()<<"/"<<event_count<<")\n";
    std::cout << "Worst response time = " << (float)responsetime_max.load() << "ms\n";
    std::cout << "Switched CPU " << engine.GetCPUSwitches() << " times\n";
    std::cout << "Downtime 1 " << engine.GetDowntime(0) << " us\n";
    std::cout << "Downtime 2 " << engine.GetDowntime(1) << " us\n";
    std::cout << "Worst Downtime 1 " << engine.GetDowntimeMax(0) << " us\n";
    std::cout << "Worst Downtime 1 " << engine.GetDowntimeMax(0) << " us\n";

    // success is low downtime (total), high responsiveness (total) and multiple context switches
    EXPECT_LE(responsetime_counter.load()/event_count, 1);
    EXPECT_LE(engine.GetDowntime(), 100);
    EXPECT_GE(engine.GetCPUSwitches(), 50);

    quit = true;
    for (auto& work:background_work)
        work.join();
}