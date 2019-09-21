#pragma once

#include <iostream>
#include <type_traits>
#include <map>
#include <vector>

namespace kiwi
{
	namespace serializer
	{
		class imstream
		{
		private:
			const char* ptr, *end;
		public:
			imstream(const char* _ptr, size_t len) : ptr(_ptr), end(_ptr + len)
			{
			}

			template<class _Ty>
			const _Ty& read()
			{
				if(end - ptr < sizeof(_Ty)) throw std::ios_base::failure(std::string{ "reading type '" } +typeid(_Ty).name() + "' failed");
				auto p = (_Ty*)ptr;
				ptr += sizeof(_Ty);
				return *p;
			}

			bool read(void* dest, size_t size)
			{
				if (end - ptr < size) return false;
				std::memcpy(dest, ptr, size);
				ptr += size;
				return true;
			}

			void exceptions(int)
			{
				// dummy functions
			}

			const char* get() const
			{
				return ptr;
			}

			bool seek(std::streamoff distance)
			{
				if (end - ptr < distance) return false;
				ptr += distance;
				return true;
			}
		};

		template<class _Ty> inline void writeToBinStream(std::ostream& os, const _Ty& v);
		template<class _Ty> inline _Ty readFromBinStream(std::istream& is);
		template<class _Ty> inline void readFromBinStream(std::istream& is, _Ty& v);
		template<class _Ty> inline _Ty readFromBinStream(imstream& is);
		template<class _Ty> inline void readFromBinStream(imstream& is, _Ty& v);

		template<class _Ty>
		inline typename std::enable_if<!std::is_fundamental<_Ty>::value && !std::is_enum<_Ty>::value>::type writeToBinStreamImpl(std::ostream& os, const _Ty& v)
		{
			static_assert(true, "Only fundamental type can be written!");
		}

		template<class _Ty, class _Istream>
		inline typename std::enable_if<!std::is_fundamental<_Ty>::value && !std::is_enum<_Ty>::value>::type readFromBinStreamImpl(_Istream& is, const _Ty& v)
		{
			static_assert(true, "Only fundamental type can be read!");
		}

		template<class _Ty>
		inline typename std::enable_if<std::is_fundamental<_Ty>::value || std::is_enum<_Ty>::value>::type writeToBinStreamImpl(std::ostream& os, const _Ty& v)
		{
			if (!os.write((const char*)&v, sizeof(_Ty))) throw std::ios_base::failure(std::string{ "writing type '" } +typeid(_Ty).name() + "' failed");
		}

		template<class _Ty, class _Istream>
		inline typename std::enable_if<std::is_fundamental<_Ty>::value || std::is_enum<_Ty>::value>::type readFromBinStreamImpl(_Istream& is, _Ty& v)
		{
			if (!is.read((char*)&v, sizeof(_Ty))) throw std::ios_base::failure(std::string{ "reading type '" } +typeid(_Ty).name() + "' failed");
		}

		inline void writeToBinStreamImpl(std::ostream& os, const k_string& v)
		{
			writeToBinStream<uint32_t>(os, v.size());
			if (!os.write((const char*)&v[0], v.size() * sizeof(k_string::value_type))) throw std::ios_base::failure(std::string{ "writing type '" } +typeid(k_string).name() + "' failed");
		}

		template<class _Istream>
		inline void readFromBinStreamImpl(_Istream& is, k_string& v)
		{
			v.resize(readFromBinStream<uint32_t>(is));
			if (!is.read((char*)&v[0], v.size() * sizeof(k_string::value_type))) throw std::ios_base::failure(std::string{ "reading type '" } +typeid(k_string).name() + "' failed");
		}

		inline void writeToBinStreamImpl(std::ostream& os, const std::u16string& v)
		{
			writeToBinStream<uint32_t>(os, v.size());
			if (!os.write((const char*)&v[0], v.size() * sizeof(char16_t))) throw std::ios_base::failure(std::string{ "writing type '" } +typeid(k_string).name() + "' failed");
		}

		template<class _Istream>
		inline void readFromBinStreamImpl(_Istream& is, std::u16string& v)
		{
			v.resize(readFromBinStream<uint32_t>(is));
			if (!is.read((char*)&v[0], v.size() * sizeof(char16_t))) throw std::ios_base::failure(std::string{ "reading type '" } +typeid(k_string).name() + "' failed");
		}

		template<class _Ty1, class _Ty2>
		inline void writeToBinStreamImpl(std::ostream& os, const typename std::pair<_Ty1, _Ty2>& v)
		{
			writeToBinStream(os, v.first);
			writeToBinStream(os, v.second);
		}

		template<class _Ty1, class _Ty2, class _Istream>
		inline void readFromBinStreamImpl(_Istream& is, typename std::pair<_Ty1, _Ty2>& v)
		{
			v.first = readFromBinStream<_Ty1>(is);
			v.second = readFromBinStream<_Ty2>(is);
		}


		template<class _Ty1, class _Ty2>
		inline void writeToBinStreamImpl(std::ostream& os, const typename std::map<_Ty1, _Ty2>& v)
		{
			writeToBinStream<uint32_t>(os, v.size());
			for (auto& p : v)
			{
				writeToBinStream(os, p);
			}
		}

		template<class _Ty1, class _Ty2, class _Istream>
		inline void readFromBinStreamImpl(_Istream& is, typename std::map<_Ty1, _Ty2>& v)
		{
			size_t len = readFromBinStream<uint32_t>(is);
			v.clear();
			for (size_t i = 0; i < len; ++i)
			{
				v.emplace(readFromBinStream<std::pair<_Ty1, _Ty2>>(is));
			}
		}

		template<class _Ty>
		inline void writeToBinStream(std::ostream& os, const _Ty& v)
		{
			writeToBinStreamImpl(os, v);
		}


		template<class _Ty>
		inline _Ty readFromBinStream(std::istream& is)
		{
			_Ty v;
			readFromBinStreamImpl(is, v);
			return v;
		}

		template<class _Ty>
		inline void readFromBinStream(std::istream& is, _Ty& v)
		{
			readFromBinStreamImpl(is, v);
		}

		template<class _Ty>
		inline _Ty readFromBinStream(imstream& is)
		{
			_Ty v;
			readFromBinStreamImpl(is, v);
			return v;
		}

		template<class _Ty>
		inline void readFromBinStream(imstream& is, _Ty& v)
		{
			readFromBinStreamImpl(is, v);
		}

		uint32_t readVFromBinStream(std::istream& is);
		uint32_t readVFromBinStream(imstream& cs);
		void writeVToBinStream(std::ostream& os, uint32_t v);

		int32_t readSVFromBinStream(std::istream& is);
		int32_t readSVFromBinStream(imstream& is);
		void writeSVToBinStream(std::ostream& os, int32_t v);

		float readNegFixed16(std::istream & is);
		float readNegFixed16(imstream & is);
		void writeNegFixed16(std::ostream & os, float v);
	}
}