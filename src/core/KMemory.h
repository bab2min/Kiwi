#pragma once

namespace kiwi
{
	template<size_t objectSize, size_t poolSize, size_t type = 0>
	class KPool
	{
	public:
		std::vector<void*> poolBuf;
		void** freeList;
		std::mutex lock;
		static KPool& getInstance()
		{
			thread_local KPool inst;
			return inst;
		}

		void* allocate()
		{
			//assert(_CrtCheckMemory());
			//std::lock_guard<std::mutex> lg(lock);
			if (!freeList)
			{
				//fprintf(stderr, "increasing pool<%zd, %zd, %zd> : at %s(%d) (Total : %g MB)\n", objectSize, poolSize, type, __FILE__, __LINE__, poolBuf.size() / 1024.f / 1024.f * objectSize * poolSize);
				initPool();
			}
			void* p = freeList;
			freeList = (void**)(*freeList);
			//fprintf(stderr, "allocate %x\n", p);
			return p;
		}

		void deallocate(void* p)
		{
			//if (!isPtrMine(p)) throw std::bad_alloc();
			//std::lock_guard<std::mutex> lg(lock);
			//fprintf(stderr, "deallocate %p\n", p);
			*((void**)p) = freeList;
			freeList = (void**)p;
			//assert(_CrtCheckMemory());
		}

	private:

		bool isPtrMine(void* ptr)
		{
			for (auto p : poolBuf)
			{
				if (ptr >= p && ptr < (char*)p + poolSize * objectSize) return true;
			}
			return false;
		}

		void initPool()
		{
			//fprintf(stderr, "KPool<%d, %d, %d> allocation\n", objectSize, poolSize, type);
			poolBuf.emplace_back(malloc(poolSize * objectSize));
			//memset(poolBuf.back(), 0, poolSize * objectSize);
			freeList = (void**)poolBuf.back();
			auto p = freeList;
			for (size_t i = 1; i < poolSize; i++)
			{
				*p = ((char*)p + objectSize);
				p = (void**)*p;
			}
			*p = nullptr;
			//usedPool += 1;
		}

		KPool()
		{
			poolBuf.reserve(16);
			initPool();
		}

		KPool(const KPool&) {}

		~KPool()
		{
			for (auto&& buf : poolBuf) free(buf);
		}
	};

	class KSingleLogger
	{
	public:
		std::map<size_t, size_t> totalAlloc;
		std::map<size_t, size_t> currentAlloc;
		std::map<size_t, size_t> maxAlloc;
		static KSingleLogger& getInstance()
		{
			thread_local KSingleLogger inst;
			return inst;
		}
	private:
		KSingleLogger()
		{
		}
		KSingleLogger(const KSingleLogger&) {}
	};

	template <typename T>
	class logger_allocator : public std::allocator<T>
	{
	public:
		typedef size_t size_type;
		typedef T* pointer;
		typedef const T* const_pointer;

		template<typename _Tp1>
		struct rebind
		{
			typedef logger_allocator<_Tp1> other;
		};

		pointer allocate(size_type n, const void *hint = 0)
		{
			//fprintf(stderr, "Alloc %d bytes. (%zd)\n", n * sizeof(T), KSingleLogger::getInstance().counter++);
			size_t bytes = n * sizeof(T);
			auto& logger = KSingleLogger::getInstance();
			logger.totalAlloc[bytes]++;
			logger.currentAlloc[bytes]++;
			logger.maxAlloc[bytes] = std::max(logger.maxAlloc[bytes], logger.currentAlloc[bytes]);
			return std::allocator<T>::allocate(n, hint);
		}

		void deallocate(pointer p, size_type n)
		{
			//if (n * sizeof(T) > 128) fprintf(stderr, "Dealloc %d bytes (%p).\n", n * sizeof(T), p);
			KSingleLogger::getInstance().currentAlloc[n * sizeof(T)]--;
			return std::allocator<T>::deallocate(p, n);
		}

		logger_allocator() throw() : std::allocator<T>() { /*fprintf(stderr, "Hello allocator!\n");*/ }
		logger_allocator(const logger_allocator &a) throw() : std::allocator<T>(a) { }
		template <class U>
		logger_allocator(const logger_allocator<U> &a) throw() : std::allocator<T>(a) { }
		~logger_allocator() throw() { }
	};

	template <typename T>
	class pool_allocator : public std::allocator<T>
	{
	public:
		typedef size_t size_type;
		typedef T* pointer;
		typedef const T* const_pointer;

		template<typename _Tp1>
		struct rebind
		{
			typedef pool_allocator<_Tp1> other;
		};

		pointer allocate(size_type n, const void *hint = 0)
		{
			if (n * sizeof(T) <= 16) return (pointer)KPool<16, 4096>::getInstance().allocate();
			if (n * sizeof(T) <= 64) return (pointer)KPool<64, 1024>::getInstance().allocate();
			if (n * sizeof(T) <= 256) return (pointer)KPool<256, 256>::getInstance().allocate();
			if (n * sizeof(T) <= 1024) return (pointer)KPool<1024, 64>::getInstance().allocate();
			if (n * sizeof(T) <= 4096) return (pointer)KPool<4096, 16>::getInstance().allocate();
			if (n * sizeof(T) <= 16384) return (pointer)KPool<16384, 8>::getInstance().allocate();
			if (n * sizeof(T) <= 32768) return (pointer)KPool<32768, 8>::getInstance().allocate();
			if (n * sizeof(T) <= 65536) return (pointer)KPool<65536, 8>::getInstance().allocate();
			//fprintf(stderr, "Alloc %d bytes.\n", n * sizeof(value_type));
			return std::allocator<T>::allocate(n, hint);
		}

		void deallocate(pointer p, size_type n)
		{
			if (n * sizeof(T) <= 16) return KPool<16, 4096>::getInstance().deallocate(p);
			if (n * sizeof(T) <= 64) return KPool<64, 1024>::getInstance().deallocate(p);
			if (n * sizeof(T) <= 256) return KPool<256, 256>::getInstance().deallocate(p);
			if (n * sizeof(T) <= 1024) return KPool<1024, 64>::getInstance().deallocate(p);
			if (n * sizeof(T) <= 4096) return KPool<4096, 16>::getInstance().deallocate(p);
			if (n * sizeof(T) <= 16384) return KPool<16384, 8>::getInstance().deallocate(p);
			if (n * sizeof(T) <= 32768) return KPool<32768, 8>::getInstance().deallocate(p);
			if (n * sizeof(T) <= 65536) return KPool<65536, 8>::getInstance().deallocate(p);
			//fprintf(stderr, "Dealloc %d bytes (%p).\n", n * sizeof(value_type), p);
			return std::allocator<T>::deallocate(p, n);
		}

		pool_allocator() throw() : std::allocator<T>() { }
		pool_allocator(const pool_allocator &a) throw() : std::allocator<T>(a) { }
		template <class U>
		pool_allocator(const pool_allocator<U> &a) throw() : std::allocator<T>(a) { }
		~pool_allocator() throw() { }
	};


	template<typename T>
	class spool_allocator : public std::allocator<T>
	{
	public:
		typedef size_t size_type;
		typedef T* pointer;
		typedef const T* const_pointer;

		template<typename _Tp1>
		struct rebind
		{
			typedef spool_allocator<_Tp1> other;
		};

		pointer allocate(size_type n, const void *hint = 0)
		{
			if (n * sizeof(T) <= 16) return (pointer)KPool<16, 4000>::getInstance().allocate();
			if (n * sizeof(T) <= 32) return (pointer)KPool<32, 2000>::getInstance().allocate();
			if (n * sizeof(T) <= 48) return (pointer)KPool<48, 1000>::getInstance().allocate();
			return std::allocator<T>::allocate(n, hint);
		}

		void deallocate(pointer p, size_type n)
		{
			if (n * sizeof(T) <= 16) return KPool<16, 4000>::getInstance().deallocate(p);
			if (n * sizeof(T) <= 32) return KPool<32, 2000>::getInstance().deallocate(p);
			if (n * sizeof(T) <= 48) return KPool<48, 1000>::getInstance().deallocate(p);
			return std::allocator<T>::deallocate(p, n);
		}

		spool_allocator() throw() : std::allocator<T>() { /*fprintf(stderr, "Hello allocator!\n");*/ }
		spool_allocator(const spool_allocator &a) throw() : std::allocator<T>(a) { }
		template <class U>
		spool_allocator(const spool_allocator<U> &a) throw() : std::allocator<T>(a) { }
		~spool_allocator() throw() { }
	};
}

template<size_t objectSize, size_t poolSize, size_t type>
void* operator new(size_t byte, kiwi::KPool<objectSize, poolSize, type>& pool)
{
	//fprintf(stderr, "new %d bytes.\n", byte);
	/*
	auto&& logger = KSingleLogger::getInstance();
	logger.totalAlloc[byte]++;
	logger.currentAlloc[byte]++;
	logger.maxAlloc[byte] = max(logger.maxAlloc[byte], logger.currentAlloc[byte]);
	return malloc(byte);
	/*/
	return pool.allocate();
	//*/
}

template<size_t objectSize, size_t poolSize, size_t type>
void operator delete(void* ptr, kiwi::KPool<objectSize, poolSize, type>& pool)
{
	//fprintf(stderr, "delete %d bytes.\n", objectSize);
	/*
	KSingleLogger::getInstance().currentAlloc[objectSize]--;
	return free(ptr);
	/*/
	pool.deallocate(ptr);
	//*/
}

#ifdef CUSTOM_ALLOC
#define NEW_IN_POOL(T) new(KPool<sizeof(T), 1024, 4>::getInstance()) T
#define DELETE_IN_POOL(T, ptr) do{ptr->~T();operator delete((void*)ptr, KPool<sizeof(T), 1024, 4>::getInstance());}while(0)
#else
#define NEW_IN_POOL(T) new T
#define DELETE_IN_POOL(T, ptr) delete ptr
#endif