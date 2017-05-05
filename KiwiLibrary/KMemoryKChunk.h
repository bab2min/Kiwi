#pragma once

template <>
class pool_allocator<KChunk> : public allocator<KChunk>
{
public:
	typedef size_t size_type;
	typedef value_type* pointer;
	typedef const value_type* const_pointer;

	template<typename _Tp1>
	struct rebind
	{
		typedef pool_allocator<_Tp1> other;
	};

	pointer allocate(size_type n, const void *hint = 0)
	{
		if (n * sizeof(value_type) <= 8) return (pointer)KPool<8, 256>::getInstance().allocate();
		if (n * sizeof(value_type) <= 16) return (pointer)KPool<16, 512>::getInstance().allocate();
		if (n * sizeof(value_type) <= 32) return (pointer)KPool<32, 1024>::getInstance().allocate();
		if (n * sizeof(value_type) <= 48) return (pointer)KPool<48, 4096>::getInstance().allocate();
		//fprintf(stderr, "Alloc %d bytes.\n", n * sizeof(value_type));
		return allocator<value_type>::allocate(n, hint);
	}

	void deallocate(pointer p, size_type n)
	{
		//fprintf(stderr, "Dealloc %d bytes (%p).\n", n * sizeof(value_type), p);
		if (n * sizeof(value_type) <= 8) return KPool<8, 256>::getInstance().deallocate(p);
		if (n * sizeof(value_type) <= 16) return KPool<16, 512>::getInstance().deallocate(p);
		if (n * sizeof(value_type) <= 32) return KPool<32, 1024>::getInstance().deallocate(p);
		if (n * sizeof(value_type) <= 48) return KPool<48, 4096>::getInstance().deallocate(p);
		return allocator<value_type>::deallocate(p, n);
	}

	pool_allocator() throw() : allocator<value_type>() { /*fprintf(stderr, "Hello allocator!\n");*/ }
	pool_allocator(const pool_allocator &a) throw() : allocator<value_type>(a) { }
	template <class U>
	pool_allocator(const pool_allocator<U> &a) throw() : allocator<value_type>(a) { }
	~pool_allocator() throw() { }
};
