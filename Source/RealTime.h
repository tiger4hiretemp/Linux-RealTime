#pragma once
#include <thread>
#include <chrono>
#include <atomic>
#include <functional>
#include <sstream>
#include <assert.h>

struct Timer
{
    bool started{false};
    std::chrono::time_point<std::chrono::steady_clock> m_start;
    std::int64_t m_total{0};
    std::int64_t m_worst{0};
    void Start() { 
        assert(!started);
        m_start = std::chrono::steady_clock::now(); 
        started = true;
    }
    void Stop() {
        assert(started);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-m_start).count();
        m_total += elapsed;
        m_worst = std::max(m_worst, elapsed); 
        started = false;
    }
    bool IsStopped() const {return !started;}
    auto GetTotal() const { assert(!started); return m_total;}
    auto GetWorst() const { assert(!started); return m_worst;}
};

class RealTimeEngine
{
    using Clock = std::chrono::steady_clock;
    std::function<void()> m_work;
    std::chrono::time_point<Clock> m_enteredStateTime[2];
    std::chrono::milliseconds m_switchTime{std::chrono::milliseconds(100)};
    std::atomic<int>   m_readyCpu{0};
    std::atomic<int>   m_runningCpu{0};
    Timer m_downtime[2];
    std::atomic<bool>  m_shuttingDown{false};
    std::ostringstream m_log;
    int m_cpu_switches{0};
    bool m_verbose{false};
    std::thread m_threads[2] = {std::thread{[this](){ThreadWork(0);}},
                                std::thread{[this](){ThreadWork(1);}}};
    void ThreadWork(int);
public:
    template<class Fn>
    RealTimeEngine(Fn&&fn)
        : m_work(fn)
    {
    }
    void SetVerbose(bool onOff) {m_verbose=onOff;}
    void Shutdown() {m_shuttingDown = true; }
    void Join() {
        if (m_threads[0].joinable()) m_threads[0].join();
        if (m_threads[1].joinable()) m_threads[1].join();
    }
    auto GetDowntime(int cpu) const {return m_downtime[cpu].GetTotal();}
    auto GetDowntimeMax(int cpu) const {return m_downtime[cpu].GetWorst();}
    auto GetDowntime() const {return m_downtime[0].GetTotal()+m_downtime[1].GetTotal();}
    auto GetCPUSwitches() const {return m_cpu_switches;}
};
