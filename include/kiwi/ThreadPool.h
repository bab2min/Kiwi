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
			~ThreadPool();

			template<class F, class... Args>
			auto enqueue(F&& f, Args&&... args)
				->std::future<typename std::result_of<F(size_t, Args...)>::type>;

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

		template<class InputIt, class Fn>
		void forEach(ThreadPool* pool, InputIt first, InputIt last, Fn fn)
		{
			if (!pool)
			{
				for (; first != last; ++first)
				{
					fn(0, *first);
				}
			}
			else
			{
				const size_t numItems = std::distance(first, last);
				const size_t numWorkers = std::min(pool->size(), numItems);
				std::vector<std::future<void>> futures;
				futures.reserve(numWorkers);

				for (size_t i = 0; i < numWorkers; ++i)
				{
					InputIt mid = first;
					std::advance(mid, (numItems * (i + 1) / numWorkers) - (numItems * i / numWorkers));
					futures.emplace_back(pool->enqueue([&](size_t tid, InputIt tFirst, InputIt tLast)
					{
						for (; tFirst != tLast; ++tFirst)
						{
							fn(tid, *tFirst);
						}
					}, first, mid));
					first = mid;
				}
				
				for (auto& f : futures)
				{
					f.get();
				}
			}
		}

		template<class Container, class Fn>
		void forEach(ThreadPool* pool, Container& cont, Fn fn)
		{
			forEach(pool, std::begin(cont), std::end(cont), fn);
		}
	}
}
