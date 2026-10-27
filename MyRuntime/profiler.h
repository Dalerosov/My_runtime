#pragma once
#include <thread>
#include <memory>
#include "pch.hpp"

enum class Progress {
    cold,
    queued,
    compiled
};

class Profiler {
public:
    explicit Profiler(const std::shared_ptr<ILProgram>& prog)
        : program(prog) {
    }

    void start() {
        if (worker.joinable()) {
            return; // Already running
        }

        worker = std::jthread([this](std::stop_token stop) {
            work_thread_start(stop);
            });
    }

    void end() {
        if (worker.joinable()) {
            worker.request_stop();
            worker.join();
        }
    }

private:
    void work_thread_start(std::stop_token stop) {
        std::chrono::milliseconds delay{ 10 };
        using clock = std::chrono::steady_clock;
        const auto time_start = clock::now();
        std::vector<Progress> fn_status;
        for (u32 i = 0; i < program->functions.size(); i++)
        {
            fn_status.push_back(Progress::cold);
        }
        while (!stop.stop_requested()) {

            // counter
            for (u32 i = 0; i < program->functions.size(); i++)
            {
                if ((fn_status[i] == Progress::cold) && program->functions[i].call_count >= 1000) {
                    std::cout << "function " << program->functions[i].name << " has been called " << program->functions[i].call_count << " times.\n";
                    fn_status[i] = Progress::queued;
                }
            }
            if (delay.count() == 10 && clock::now() - time_start >= std::chrono::seconds(1)) {
                delay = std::chrono::milliseconds(100);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }
    }

    std::shared_ptr<ILProgram> program;
    std::jthread worker;
};

