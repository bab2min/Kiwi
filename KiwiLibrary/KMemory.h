#pragma once

template<size_t objectSize>
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
		if (!freeList) return nullptr;
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
		poolBuf = new char[1024*32*objectSize];
		freeList = (void**)poolBuf;
		auto p = freeList;
		for (size_t i = 0; i < 1024 * 1024; i += objectSize)
		{
			*p = ((char*)p + objectSize);
			p = (void**)*p;
		}
		*p = nullptr;
	}
	KPool(const KPool&) {}
	~KPool()
	{
		delete[1024*32*objectSize] poolBuf;
	}
};

template<typename T> struct TypeIsChar { static const bool value = false; };
template<> struct TypeIsChar<char> { static const bool value = true; };

template <typename T>
class log_allocator : public allocator<T>
{
public:
	typedef size_t size_type;
	typedef T* pointer;
	typedef const T* const_pointer;

	template<typename _Tp1>
	struct rebind
	{
		typedef log_allocator<_Tp1> other;
	};

	pointer allocate(size_type n, const void *hint = 0)
	{
		//fprintf(stderr, "Alloc %d bytes.\n", n * sizeof(T));
		return allocator<T>::allocate(n, hint);
	}

	void deallocate(pointer p, size_type n)
	{
		//fprintf(stderr, "Dealloc %d bytes (%p).\n", n * sizeof(T), p);
		return allocator<T>::deallocate(p, n);
	}

	log_allocator() throw() : allocator<T>() { /*fprintf(stderr, "Hello allocator!\n");*/ }
	log_allocator(const log_allocator &a) throw() : allocator<T>(a) { }
	template <class U>
	log_allocator(const log_allocator<U> &a) throw() : allocator<T>(a) { }
	~log_allocator() throw() { }
};

template <>
class log_allocator<char> : public allocator<char>
{
public:
	typedef size_t size_type;
	typedef char* pointer;
	typedef const char* const_pointer;

	template<typename _Tp1>
	struct rebind
	{
		typedef log_allocator<_Tp1> other;
	};

	pointer allocate(size_type n, const void *hint = 0)
	{
		//fprintf(stderr, "Alloc %d bytes.\n", n * sizeof(char));
		if (n == 16) return (char*)KPool<16>::getInstance().allocate();
		if (n == 32) return (char*)KPool<32>::getInstance().allocate();
		return allocator<char>::allocate(n, hint);
	}

	void deallocate(pointer p, size_type n)
	{
		//fprintf(stderr, "Dealloc %d bytes (%p).\n", n * sizeof(char), p);
		if (n == 16) return KPool<16>::getInstance().deallocate(p);
		if (n == 32) return KPool<32>::getInstance().deallocate(p);
		return allocator<char>::deallocate(p, n);
	}

	log_allocator() throw() : allocator<char>() { /*fprintf(stderr, "Hello allocator!\n");*/ }
	log_allocator(const log_allocator &a) throw() : allocator<char>(a) { }
	template <class U>
	log_allocator(const log_allocator<U> &a) throw() : allocator<char>(a) { }
	~log_allocator() throw() { }
};