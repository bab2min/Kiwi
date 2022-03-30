#pragma once

/*
A simple C++11 Thread Pool implementation(https://github.com/progschj/ThreadPool)
modified by bab2min to have additional parameter threadId
*/

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

namespace kiwi
{
	namespace utils
	{
		class ThreadPool
		{
		public:
			ThreadPool(size_t threads = 0, size_t maxQueued = 0);
			template<class F, class... Args>
			auto enqueue(F&& f, Args&&... args)
				->std::future<typename std::result_of<F(size_t, Args...)>::type>;
			~ThreadPool();
			size_t size() const { return workers.size(); }
			size_t numEnqueued() const { return tasks.size(); }
			void joinAll();
		private:
			std::vector<std::thread> workers;
			std::queue<std::function<void(size_t)>> tasks;

			std::mutex queue_mutex;
			std::condition_variable condition, inputCnd;
			bool stop;
			size_t maxQueued;
		};

		inline ThreadPool::ThreadPool(size_t threads, size_t _maxQueued)
			: stop(false), maxQueued(_maxQueued)
		{
			for (size_t i = 0; i < threads; ++i)
				workers.emplace_back([this, i]
			{
				for (;;)
				{
					std::function<void(size_t)> task;
					{
						std::unique_lock<std::mutex> lock(this->queue_mutex);
						this->condition.wait(lock,
							[this] { return this->stop || !this->tasks.empty(); });
						if (this->stop && this->tasks.empty()) return;
						task = std::move(this->tasks.front());
						this->tasks.pop();
						if (this->maxQueued) this->inputCnd.notify_all();
					}
					task(i);
				}
			});
		}

		template<class F, class... Args>
		auto ThreadPool::enqueue(F&& f, Args&&... args)
			-> std::future<typename std::result_of<F(size_t, Args...)>::type>
		{
			using return_type = typename std::result_of<F(size_t, Args...)>::type;

			auto task = std::make_shared< std::packaged_task<return_type(size_t)> >(
				std::bind(std::forward<F>(f), std::placeholders::_1, std::forward<Args>(args)...));

			std::future<return_type> res = task->get_future();
			{
				std::unique_lock<std::mutex> lock(queue_mutex);

				// don't allow enqueueing after stopping the pool
				if (stop) throw std::runtime_error("enqueue on stopped ThreadPool");
				if (maxQueued && tasks.size() >= maxQueued)
				{
					inputCnd.wait(lock, [&]() { return tasks.size() < maxQueued; });
				}
				tasks.emplace([task](size_t id) { (*task)(id); });
			}
			condition.notify_one();
			return res;
		}

		inline void ThreadPool::joinAll()
		{
			if (stop) return;

			{
				std::unique_lock<std::mutex> lock(queue_mutex);
				stop = true;
			}
			condition.notify_all();
			for (std::thread& worker : workers)
				worker.join();
		}

		inline ThreadPool::~ThreadPool()
		{
			joinAll();
		}
	}
}
