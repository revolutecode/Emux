#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Emux::Parallel
{

inline unsigned DefaultJobs()
{
    unsigned jobs = std::thread::hardware_concurrency();
    return jobs == 0 ? 1u : jobs;
}

// Executa body(index) para cada índice usando até "jobs" threads.
// A primeira exceção é guardada e relançada depois do join.
inline void For(
    std::size_t count,
    unsigned jobs,
    const std::function<void(std::size_t)>& body
)
{
    if (count == 0)
    {
        return;
    }

    const unsigned workers = std::max(
        1u,
        std::min<unsigned>(jobs, static_cast<unsigned>(count))
    );

    if (workers == 1)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            body(i);
        }
        return;
    }

    std::atomic<std::size_t> next{0};
    std::exception_ptr error;
    std::mutex errorMutex;
    std::vector<std::thread> threads;
    threads.reserve(workers);

    for (unsigned t = 0; t < workers; ++t)
    {
        threads.emplace_back([&]()
        {
            for (;;)
            {
                const std::size_t i = next.fetch_add(1);
                if (i >= count)
                {
                    return;
                }

                try
                {
                    body(i);
                }
                catch (...)
                {
                    std::lock_guard<std::mutex> lock(errorMutex);
                    if (!error)
                    {
                        error = std::current_exception();
                    }
                    return;
                }
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    if (error)
    {
        std::rethrow_exception(error);
    }
}

}
