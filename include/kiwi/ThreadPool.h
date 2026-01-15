/**
 * @file ThreadPool.h
 * @author bab2min (bab2min@gmail.com)
 * @brief 간단한 C++11 Thread Pool 구현
 * @version 0.22.1
 * @date 2025-11-21
 * 
 * A simple C++11 Thread Pool implementation(https://github.com/progschj/ThreadPool)
 * modified by bab2min to have additional parameter threadId
 * 
 * 멀티스레딩을 위한 작업 큐와 워커 스레드 풀을 제공합니다.
 * 형태소 분석, 단어 추출 등의 병렬 처리에 사용됩니다.
 */

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
		/**
		 * @brief 작업을 병렬로 처리하는 스레드 풀
		 * 
		 * 고정된 수의 워커 스레드를 생성하고,
		 * 작업을 큐에 넣어 스레드들이 병렬로 처리하도록 합니다.
		 */
		class ThreadPool
		{
		public:
			/**
			 * @brief ThreadPool 생성자
			 * @param threads 워커 스레드의 개수 (0이면 스레드 없이 직렬 처리)
			 * @param maxQueued 최대 큐 크기 (0이면 무제한)
			 */
			ThreadPool(size_t threads = 0, size_t maxQueued = 0);
			~ThreadPool();

			/**
			 * @brief 작업을 큐에 추가합니다.
			 * 
			 * 작업 함수는 첫 번째 인자로 스레드 ID를 받습니다.
			 * 
			 * @tparam F 함수 타입
			 * @tparam Args 인자 타입들
			 * @param f 실행할 함수
			 * @param args 함수에 전달할 인자들
			 * @return 작업 결과를 받을 수 있는 future
			 */
			template<class F, class... Args>
			auto enqueue(F&& f, Args&&... args)
				->std::future<typename std::invoke_result<F, size_t, Args...>::type>;

			/**
			 * @brief 스레드 풀의 크기를 반환합니다.
			 * @return 워커 스레드의 개수
			 */
			size_t size() const { return workers.size(); }
			
			/**
			 * @brief 큐에 있는 작업의 개수를 반환합니다.
			 * @return 대기 중인 작업 개수
			 */
			size_t numEnqueued() const { return tasks.size(); }
			
			/**
			 * @brief 모든 작업이 완료될 때까지 기다립니다.
			 */
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
			-> std::future<typename std::invoke_result<F, size_t, Args...>::type>
		{
			using return_type = typename std::invoke_result<F, size_t, Args...>::type;

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
