#pragma once

template<size_t objectSize, size_t poolSize>
class KPool
{
public:
	char* poolBuf;
	void** freeList;

	static KPool& getInstance()
	{
		static thread_local KPool inst;
		return inst;
	}

	void* allocate()
	{
		if (!freeList) throw bad_alloc();
		void* p = freeList;
		freeList = (void**)(*freeList);
		return p;
	}

	void deallocate(void* p)
	{
		*((void**)p) = freeList;
		freeList = (void**)p;
	}

private:
	KPool() 
	{
		poolBuf = new char[poolSize * objectSize];
		freeList = (void**)poolBuf;
		auto p = freeList;
		for (size_t i = 0; i < poolSize; i++)
		{
			*p = ((char*)p + objectSize);
			p = (void**)*p;
		}
		*p = nullptr;
	}
	KPool(const KPool&) {}
	~KPool()
	{
		delete[poolSize*objectSize] poolBuf;
	}
};

class KSingleLogger
{
public:
	map<size_t, size_t> totalAlloc;
	map<size_t, size_t> currentAlloc;
	map<size_t, size_t> maxAlloc;
	static KSingleLogger& getInstance()
	{
		static thread_local KSingleLogger inst;
		return inst;
	}
private:
	KSingleLogger() 
	{
	}
	KSingleLogger(const KSingleLogger&) {}
};

template <typename T>
class logger_allocator : public allocator<T>
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
		logger.maxAlloc[bytes] = max(logger.maxAlloc[bytes], logger.currentAlloc[bytes]);
		return allocator<T>::allocate(n, hint);
	}

	void deallocate(pointer p, size_type n)
	{
		//if (n * sizeof(T) > 128) fprintf(stderr, "Dealloc %d bytes (%p).\n", n * sizeof(T), p);
		KSingleLogger::getInstance().currentAlloc[n * sizeof(T)]--;
		return allocator<T>::deallocate(p, n);
	}

	logger_allocator() throw() : allocator<T>() { /*fprintf(stderr, "Hello allocator!\n");*/ }
	logger_allocator(const logger_allocator &a) throw() : allocator<T>(a) { }
	template <class U>
	logger_allocator(const logger_allocator<U> &a) throw() : allocator<T>(a) { }
	~logger_allocator() throw() { }
};

template <typename T>
class pool_allocator : public allocator<T>
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
		return allocator<T>::allocate(n, hint);
	}

	void deallocate(pointer p, size_type n)
	{
		return allocator<T>::deallocate(p, n);
	}

	pool_allocator() throw() : allocator<T>() { }
	pool_allocator(const pool_allocator &a) throw() : allocator<T>(a) { }
	template <class U>
	pool_allocator(const pool_allocator<U> &a) throw() : allocator<T>(a) { }
	~pool_allocator() throw() { }
};

template<typename T>
class spool_allocator : public allocator<T>
{
public:
	typedef size_t size_type;
	typedef value_type* pointer;
	typedef const value_type* const_pointer;

	template<typename _Tp1>
	struct rebind
	{
		typedef spool_allocator<_Tp1> other;
	};

	pointer allocate(size_type n, const void *hint = 0)
	{
		if (n <= 32) return (pointer)KPool<32, 4096>::getInstance().allocate();
		if (n <= 48) return (pointer)KPool<48, 4096>::getInstance().allocate();
		return allocator<value_type>::allocate(n, hint);
	}

	void deallocate(pointer p, size_type n)
	{
		if (n <= 32) return KPool<32, 4096>::getInstance().deallocate(p);
		if (n <= 48) return KPool<48, 4096>::getInstance().deallocate(p);
		return allocator<value_type>::deallocate(p, n);
	}

	spool_allocator() throw() : allocator<value_type>() { /*fprintf(stderr, "Hello allocator!\n");*/ }
	spool_allocator(const spool_allocator &a) throw() : allocator<value_type>(a) { }
	template <class U>
	spool_allocator(const spool_allocator<U> &a) throw() : allocator<value_type>(a) { }
	~spool_allocator() throw() { }
};