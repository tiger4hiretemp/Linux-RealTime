#pragma once
#include <thread>
#include <chrono>
#include <atomic>
#include <functional>
#include <sstream>

class RealTimeEngine
{
    using Clock = std::chrono::steady_clock;
    std::function<void()> m_work;
    std::chrono::time_point<Clock> m_enteredStateTime[2];
    std::chrono::milliseconds m_switchTime{std::chrono::milliseconds(100)};
    std::atomic<int>   m_readyCpu{0};
    std::atomic<int>   m_runningCpu{0};
    std::atomic<std::uint64_t> m_downtime{0};
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
    auto GetDowntime() const {return m_downtime.load();}
    auto GetCPUSwitches() const {return m_cpu_switches;}
};
