#pragma once

#include <vector>
#include <tuple>
#include <type_traits>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

namespace mp
{
	class Barrier {
	public:
		explicit Barrier(std::mutex* _mutex, std::condition_variable* _cond, size_t _count) :
			mutex(_mutex),
			cond(_cond),
			threshold(_count),
			count(_count),
			generation(0) 
		{
		}

		void wait()
		{
			std::unique_lock<std::mutex> lLock{ *mutex };
			auto gen = generation;
			if (!--count) 
			{
				generation++;
				count = threshold;
				cond->notify_all();
			}
			else 
			{
				cond->wait(lLock, [this, gen] { return gen != generation; });
			}
		}

	private:
		std::mutex* mutex;
		std::condition_variable* cond;
		std::size_t threshold;
		std::size_t count;
		std::size_t generation;
	};

	class OverrideLimitedSize;

	class ThreadPool
	{
		friend class OverrideLimitedSize;
	public:
		ThreadPool(size_t threads = 0);
		template<class F, class... Args>
		auto runParallel(size_t workers, F&& f, Args&&... args)
			-> std::vector<std::future<typename std::result_of<F(size_t, size_t, Barrier*, Args...)>::type>>;
		~ThreadPool();
		size_t size() const { return workers.size(); }
		size_t limitedSize() const { return std::min(size(), _limitedSize); };
		void joinAll();
		
		Barrier getBarrier(size_t count = -1)
		{
			return Barrier{ &barrier_mutex, &barrier_condition, std::min(count, tasks.size()) };
		}

	private:
		std::vector<std::thread> workers;
		std::vector<std::queue<std::function<void(size_t)>>> tasks;

		std::mutex queue_mutex, barrier_mutex;
		std::condition_variable condition, barrier_condition;
		size_t _limitedSize = -1;
		bool stop;
	};

	inline ThreadPool::ThreadPool(size_t threads)
		: stop(false)
	{
		tasks.resize(threads);
		for (size_t i = 0; i < threads; ++i)
		{
			workers.emplace_back([this, i, threads]
			{
				for (;;)
				{
					std::function<void(size_t)> task;
					{
						std::unique_lock<std::mutex> lock(this->queue_mutex);
						this->condition.wait(lock,
							[this, i] { return this->stop || !this->tasks[i].empty(); });
						if (this->stop && this->tasks[i].empty()) return;
						task = std::move(this->tasks[i].front());
						this->tasks[i].pop();
					}
					task(i);
				}
			});
		}
	}

	template<class F, class... Args>
	auto ThreadPool::runParallel(size_t workers, F&& f, Args&&... args)
		-> std::vector<std::future<typename std::result_of<F(size_t, size_t, Barrier*, Args...)>::type>>
	{
		using return_type = typename std::result_of<F(size_t, size_t, Barrier*, Args...)>::type;
		std::vector<std::future<return_type>> ret;
		{
			auto b = std::make_shared<Barrier>(getBarrier(workers));
			std::unique_lock<std::mutex> lock(queue_mutex);
			for (size_t i = 0; i < workers && i < tasks.size(); ++i)
			{
				auto task = std::make_shared< std::packaged_task<return_type(size_t, size_t, Barrier*)> >(
					std::bind(std::forward<F>(f), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::forward<Args>(args)...));
				if (stop) throw std::runtime_error("enqueue on stopped ThreadPool");
				ret.emplace_back(task->get_future());
				tasks[i].emplace([&, task, workers, b](size_t id) { (*task)(id, std::min(workers, tasks.size()), b.get()); });
			}
		}
		condition.notify_all();
		return ret;
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

	inline size_t getPoolSize(ThreadPool* pool)
	{
		if (!pool) return 1;
		return pool->size();
	}

	namespace detail
	{
		template<class Ty>
		struct ValueHolder
		{
			using type = Ty;

			Ty value;

			template<class V>
			ValueHolder(V&& v) : value(std::forward<V>(v)) {}

			operator Ty() const { return value; }
		};
	}

	struct MaximumWorkers : public detail::ValueHolder<size_t>
	{
		using type = size_t;
		using runParallelArg = int;
		using detail::ValueHolder<size_t>::ValueHolder;
	};

	struct ParallelCond : public detail::ValueHolder<bool>
	{
		using type = bool;
		using runParallelArg = int;
		using detail::ValueHolder<bool>::ValueHolder;
	};

	template<class Ty>
	struct ParallelFinal : public detail::ValueHolder<Ty>
	{
		using type = Ty;
		using runParallelArg = int;
		using detail::ValueHolder<Ty>::ValueHolder;

		void operator()()
		{
			this->value();
		}
	};

	template<class Ty>
	ParallelFinal<typename std::remove_reference<Ty>::type> parallelFinal(Ty&& v)
	{
		return ParallelFinal<typename std::remove_reference<Ty>::type>{std::forward<Ty>(v)};
	}

	namespace detail
	{
		template <class T> struct IsRunParallelArg
		{
		private:
			struct __two { char dummy[2]; };
			template <class U> static __two test(...);
			template <class U> static char  test(typename U::runParallelArg* = nullptr);
		public:
			static const bool value = sizeof(test<T>(0)) == 1;
		};

		template<class T, class Tuple>
		struct FindType;

		template<class T>
		struct FindType<T, std::tuple<>>
		{
			using type = void;
			static constexpr int value = -1;
		};

		template<class T, class U, class ...Ts>
		struct FindType<T, std::tuple<U, Ts...>>
		{
			using type = typename std::conditional<
				std::is_same<T, typename std::remove_reference<U>::type>::value,
				U,
				typename FindType<T, std::tuple<Ts...>>::type
			>::type;
			static constexpr int value = std::is_same<T, typename std::remove_reference<U>::type>::value ? 0 : (1 + FindType<T, std::tuple<Ts...>>::value);
		};

		template<template<class> class T, class Tuple>
		struct FindTType;

		template<template<class> class T>
		struct FindTType<T, std::tuple<>>
		{
			using type = void;
			static constexpr int value = -1;
		};

		template<template<class> class T, class U, class ...Ts>
		struct FindTType<T, std::tuple<U, Ts...>>
		{
			using type = typename std::conditional<
				std::is_same<T<typename std::remove_reference<U>::type::type>, typename std::remove_reference<U>::type>::value, 
				U, 
				typename FindTType<T, std::tuple<Ts...>>::type
			>::type;
			static constexpr int value = std::is_same<T<typename std::remove_reference<U>::type::type>, typename std::remove_reference<U>::type>::value ? 0 : (1 + FindTType<T, std::tuple<Ts...>>::value);
		};

		template<class Tuple, template<class> class Pred>
		struct AllOfType;

		template<template<class> class Pred>
		struct AllOfType<std::tuple<>, Pred> : std::true_type {};

		template<class U, class ...Ts, template<class> class Pred>
		struct AllOfType<std::tuple<U, Ts...>, Pred> : std::integral_constant<bool,
			AllOfType<std::tuple<Ts...>, Pred>::value&& Pred<U>::value
		> {};

		struct NoOp
		{
			void operator()()
			{
			}
		};

		template<class Ty, class Ty2, class ...Args,
			typename std::enable_if<!std::is_same<typename FindType<Ty, std::tuple<Args...>>::type, void>::value, int>::type = 0
		>
		Ty extractValueFrom(Ty2&& def, const std::tuple<Args...>& args)
		{
			return std::get<FindType<Ty, std::tuple<Args...>>::value>(args);
		}

		template<class Ty, class Ty2, class ...Args,
			typename std::enable_if<std::is_same<typename FindType<Ty, std::tuple<Args...>>::type, void>::value, int>::type = 0
		>
		Ty extractValueFrom(Ty2&& def, const std::tuple<Args...>& args)
		{
			return std::forward<Ty2>(def);
		}

		template<template<class> class Ty, class Ty2, class ...Args,
			typename std::enable_if<!std::is_same<typename FindTType<Ty, std::tuple<Args...>>::type, void>::value, int>::type = 0
		>
		typename std::remove_reference<typename FindTType<Ty, std::tuple<Args...>>::type>::type extractValueFrom(Ty2&& def, const std::tuple<Args...>& args)
		{
			return std::get<FindTType<Ty, std::tuple<Args...>>::value>(args);
		}

		template<template<class> class Ty, class Ty2, class ...Args,
			typename std::enable_if<std::is_same<typename FindTType<Ty, std::tuple<Args...>>::type, void>::value, int>::type = 0
		>
		Ty2&& extractValueFrom(Ty2&& def, const std::tuple<Args...>& args)
		{
			return std::forward<Ty2>(def);
		}
	}

	template<class Fn, class ...Args,
		typename std::enable_if<!std::is_same<typename std::result_of<Fn(size_t, size_t, Barrier*)>::type, void>::value, int>::type = 0>
	inline auto runParallel(ThreadPool* pool, Fn&& func, Args&&... args) -> std::vector<decltype(func(0, 0, nullptr))>
	{
		static_assert(detail::AllOfType<std::tuple<Args...>, detail::IsRunParallelArg>::value, "`runParallel` receives arguments of wrong type.");
		auto argTuple = std::forward_as_tuple(std::forward<Args>(args)...);
		size_t maximumWorkers = detail::extractValueFrom<MaximumWorkers>((size_t)-1, argTuple);
		bool parallelCond = detail::extractValueFrom<ParallelCond>(true, argTuple);
		auto parallelFinalFn = detail::extractValueFrom<ParallelFinal>(detail::NoOp{}, argTuple);

		std::vector<decltype(func(0, 0, nullptr))> ret;
		if (!pool || !parallelCond || maximumWorkers == 1)
		{
			ret.emplace_back(func(0, 1, nullptr));
		}
		else
		{
			for (auto& f : pool->runParallel(std::min(maximumWorkers, pool->limitedSize()), func))
			{
				ret.emplace_back(f.get());
			}
			parallelFinalFn();
		}
		return ret;
	}

	template<class Fn, class ...Args,
		typename std::enable_if<std::is_same<typename std::result_of<Fn(size_t, size_t, Barrier*)>::type, void>::value, int>::type = 0>
		inline void runParallel(ThreadPool* pool, Fn&& func, Args&&... args)
	{
		static_assert(detail::AllOfType<std::tuple<Args...>, detail::IsRunParallelArg>::value, "`runParallel` receives arguments of wrong type.");
		auto argTuple = std::forward_as_tuple(std::forward<Args>(args)...);
		size_t maximumWorkers = detail::extractValueFrom<MaximumWorkers>((size_t)-1, argTuple);
		bool parallelCond = detail::extractValueFrom<ParallelCond>(true, argTuple);
		auto parallelFinalFn = detail::extractValueFrom<ParallelFinal>(detail::NoOp{}, argTuple);

		if (!pool || !parallelCond || maximumWorkers == 1)
		{
			func(0, 1, nullptr);
		}
		else
		{
			for (auto& f : pool->runParallel(std::min(maximumWorkers, pool->limitedSize()), func))
			{
				f.get();
			}
			parallelFinalFn();
		}
	}

	inline void barrier(Barrier* b)
	{
		if (b) b->wait();
	}

	template<class Fn, class ...Args,
		typename std::enable_if<std::is_same<typename std::result_of<Fn(size_t, size_t, ptrdiff_t, ptrdiff_t, ptrdiff_t, Barrier*)>::type, void>::value, int>::type = 0>
		inline void forParallel(ThreadPool* pool, ptrdiff_t start, ptrdiff_t stop, ptrdiff_t step, Fn&& func, Args&&... args)
	{
		static_assert(detail::AllOfType<std::tuple<Args...>, detail::IsRunParallelArg>::value, "`forParallel` receives arguments of wrong type.");
		auto argTuple = std::forward_as_tuple(std::forward<Args>(args)...);
		size_t maximumWorkers = detail::extractValueFrom<MaximumWorkers>((size_t)-1, argTuple);
		bool parallelCond = detail::extractValueFrom<ParallelCond>(true, argTuple);
		auto parallelFinalFn = detail::extractValueFrom<ParallelFinal>(detail::NoOp{}, argTuple);

		if (!pool || !parallelCond || maximumWorkers == 1)
		{
			func(0, 1, start, stop, step, nullptr);
		}
		else
		{
			for (auto& f : pool->runParallel(std::min(maximumWorkers, pool->limitedSize()), [&](ptrdiff_t tid, ptrdiff_t numThreads, Barrier* barrier)
				{
					ptrdiff_t pstart = start + ((stop - start) * tid / numThreads) / step * step;
					ptrdiff_t pstop = start + ((stop - start) * (tid + 1) / numThreads) / step * step;
					if (tid + 1 == numThreads) pstop = stop;
					func(tid, numThreads, pstart, pstop, step, nullptr);
				})
			)
			{
				f.get();
			}
			parallelFinalFn();
		}
	}

	class OverrideLimitedSize
	{
		size_t prevSize;
		ThreadPool* pool;
	public:
		OverrideLimitedSize(ThreadPool* _pool, size_t newSize)
			: pool{ _pool }, prevSize{ pool ? pool->limitedSize() : -1 }
		{
			if (pool) pool->_limitedSize = newSize;
		}

		~OverrideLimitedSize()
		{
			if (pool) pool->_limitedSize = prevSize;
		}
	};

	template<class Mutex>
	class OptionalLockGuard
	{
		Mutex* mutex;

	public:
		OptionalLockGuard(Mutex* _mutex = nullptr) : mutex{ _mutex }
		{
			if (mutex) mutex->lock();
		}

		~OptionalLockGuard()
		{
			if (mutex) mutex->unlock();
		}

		OptionalLockGuard(const OptionalLockGuard&) = delete;
		OptionalLockGuard& operator=(const OptionalLockGuard&) = delete;
	};
}
