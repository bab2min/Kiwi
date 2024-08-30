#pragma once
#include <string>
#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
namespace kiwi
{
	namespace utils
	{
		namespace detail
		{
			class HandleGuard
			{
				HANDLE handle = nullptr;
			public:
				HandleGuard(HANDLE _handle = nullptr) : handle(_handle)
				{
				}

				HandleGuard(const HandleGuard&) = delete;
				HandleGuard& operator =(const HandleGuard&) = delete;

				HandleGuard(HandleGuard&& o) noexcept
				{
					std::swap(handle, o.handle);
				}

				HandleGuard& operator=(HandleGuard&& o) noexcept
				{
					std::swap(handle, o.handle);
					return *this;
				}

				~HandleGuard()
				{
					if (handle && handle != INVALID_HANDLE_VALUE)
					{
						CloseHandle(handle);
						handle = nullptr;
					}
				}

				operator HANDLE() const
				{
					return handle;
				}
			};
		}

		class MMap
		{
			const char* view = nullptr;
			uint64_t len = 0;
			detail::HandleGuard hFile, hFileMap;
		public:
			MMap(const std::string& filepath)
			{
				hFile = CreateFileW((const wchar_t*)utf8To16(filepath).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, nullptr);
				if (hFile == INVALID_HANDLE_VALUE) throw std::ios_base::failure("Cannot open '" + filepath + "'");
				hFileMap = CreateFileMapping(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
				if (hFileMap == nullptr) throw std::ios_base::failure("Cannot open '" + filepath + "' Code:" + std::to_string(GetLastError()));
				view = (const char*)MapViewOfFile(hFileMap, FILE_MAP_READ, 0, 0, 0);
				if (!view) throw std::ios_base::failure("Cannot MapViewOfFile() Code:" + std::to_string(GetLastError()));
				DWORD high;
				len = GetFileSize(hFile, &high);
				len |= (uint64_t)high << 32;
			}

			MMap(const MMap&) = delete;
			MMap& operator=(const MMap&) = delete;

			MMap(MMap&& o) noexcept
				: view{ o.view }, len{ o.len }
			{
				o.view = nullptr;
				std::swap(hFile, o.hFile);
				std::swap(hFileMap, o.hFileMap);
			}

			MMap& operator=(MMap&& o) noexcept
			{
				std::swap(view, o.view);
				std::swap(len, o.len);
				std::swap(hFile, o.hFile);
				std::swap(hFileMap, o.hFileMap);
				return *this;
			}

			~MMap()
			{
				if (hFileMap)
				{
					UnmapViewOfFile(view);
					view = nullptr;
				}
			}

			const char* get() const { return view; }
			size_t size() const { return len; }
		};
	}
}
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

namespace kiwi
{
	namespace utils
	{
		namespace detail
		{
			class FDGuard
			{
				int fd = 0;
			public:
				FDGuard(int _fd = 0) : fd(_fd)
				{
				}

				FDGuard(const FDGuard&) = delete;
				FDGuard& operator =(const FDGuard&) = delete;

				FDGuard(FDGuard&& o)
				{
					std::swap(fd, o.fd);
				}

				FDGuard& operator=(FDGuard&& o)
				{
					std::swap(fd, o.fd);
					return *this;
				}

				~FDGuard()
				{
					if (fd && fd != -1)
					{
						close(fd);
						fd = 0;
					}
				}

				operator int() const
				{
					return fd;
				}
			};
		}

		class MMap
		{
			const char* view = nullptr;
			size_t len = 0;
			detail::FDGuard fd;
		public:
			MMap(const std::string& filepath)
			{
				fd = open(filepath.c_str(), O_RDONLY);
				if (fd == -1) throw std::ios_base::failure("Cannot open '" + filepath + "'");
				struct stat sb;
				if (fstat(fd, &sb) < 0) throw std::ios_base::failure("Cannot open '" + filepath + "'");
				len = sb.st_size;
				view = (const char*)mmap(nullptr, len, PROT_READ, MAP_PRIVATE, fd, 0);
				if (view == MAP_FAILED) throw std::ios_base::failure("Mapping failed");
			}

			MMap(const MMap&) = delete;
			MMap& operator=(const MMap&) = delete;

			MMap(MMap&& o) : len{ o.len }, fd{ std::move(o.fd) }
			{
				std::swap(view, o.view);
			}

			MMap& operator=(MMap&& o)
			{
				std::swap(view, o.view);
				std::swap(len, o.len);
				std::swap(fd, o.fd);
				return *this;
			}

			~MMap()
			{
				if (view)
				{
					munmap((void*)view, len);
				}
			}

			const char* get() const { return view; }
			size_t size() const { return len; }
		};
	}
}
#endif

#include <iostream>
#include <cstring>

namespace kiwi
{
	namespace utils
	{
		class MemoryOwner
		{
			std::unique_ptr<char[]> _ptr;
			size_t _size = 0;

		public:
			MemoryOwner() = default;
			MemoryOwner(size_t tot_size)
				: _ptr{ new char[tot_size] }, _size{ tot_size }
			{
			}

			void* get() const { return _ptr.get(); }
			size_t size() const { return _size; }
		};

		class MemoryObject
		{
			struct Concept
			{
				virtual ~Concept() {};
				virtual const void* get() const = 0;
				virtual size_t size() const = 0;
			};

			template<class Ty>
			struct Model : Concept
			{
			private:
				Ty obj;
			public:
				Model(const Ty& t) : obj{ t } {}
				Model(Ty&& t) : obj{ std::move(t) } {}

				virtual const void* get() const { return obj.get(); }
				virtual size_t size() const { return obj.size(); }
			};

			std::shared_ptr<const Concept> obj;

		public:
			template<class Ty>
			MemoryObject(const Ty& _obj) : obj{ std::make_shared<Model<Ty>>(std::move(_obj)) } {}

			template<class Ty>
			MemoryObject(Ty&& _obj) : obj{ std::make_shared<Model<typename std::remove_reference<Ty>::type>>(std::forward<Ty>(_obj)) } {}

			MemoryObject(const MemoryObject&) = default;
			MemoryObject(MemoryObject&&) noexcept = default;

			const void* get() const { return obj->get(); }
			size_t size() const { return obj->size(); }
		};

		template<bool read, bool write>
		struct membuf : public std::streambuf
		{
			membuf(char* base, std::ptrdiff_t n)
			{
				if (read)
				{
					this->setg(base, base, base + n);
				}

				if (write)
				{
					this->setp(base, base + n);
				}
			}

			pos_type seekpos(pos_type sp, std::ios_base::openmode which) override {
				return seekoff(sp - pos_type(off_type(0)), std::ios_base::beg, which);
			}

			pos_type seekoff(off_type off,
				std::ios_base::seekdir dir,
				std::ios_base::openmode which = std::ios_base::in
			) override {
				if (which & std::ios_base::in)
				{
					if (dir == std::ios_base::cur)
						gbump(off);
					else if (dir == std::ios_base::end)
						setg(eback(), egptr() + off, egptr());
					else if (dir == std::ios_base::beg)
						setg(eback(), eback() + off, egptr());
				}
				if (which & std::ios_base::out)
				{
					if (dir == std::ios_base::cur)
						pbump(off);
					else if (dir == std::ios_base::end)
						setp(epptr() + off, epptr());
					else if (dir == std::ios_base::beg)
						setp(pbase() + off, epptr());
					
					if (!(which & std::ios_base::in))
					{
						return pptr() - pbase();
					}
				}
				return gptr() - eback();
			}

			const char* curptr() const
			{
				return this->gptr();
			}
		};

		class imstream : public std::istream
		{
			membuf<true, false> buf;
		public:
			imstream(const char* base, std::ptrdiff_t n)
				: std::istream(&buf), buf((char*)base, n)
			{
			}

			template<class Ty>
			imstream(const Ty& m) : imstream(m.get(), m.size())
			{
			}

			const char* curptr() const
			{
				return buf.curptr();
			}
		};

		class omstream : public std::ostream
		{
			membuf<false, true> buf;
		public:
			omstream(char* base, std::ptrdiff_t n)
				: std::ostream(&buf), buf((char*)base, n)
			{
			}

			template<class Ty>
			omstream(const Ty& m) : omstream(m.get(), m.size())
			{
			}
		};

		template<typename Ty>
		Ty read(std::istream& istr)
		{
			Ty ret;
			if (!istr.read((char*)&ret, sizeof(Ty)))
			{
				throw std::ios_base::failure(std::string{ "reading type '" } + typeid(Ty).name() + "' failed");
			}
			return ret;
		}
	}
}
