using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

namespace KiwiGui
{
    /*
    DECL_DLL int kiwi_version();

    DECL_DLL PKIWI kiwi_init(const char* modelPath, int maxCache);
    DECL_DLL int kiwi_loadUserDict(PKIWI handle, const char* dictPath);
    DECL_DLL int kiwi_prepare(PKIWI handle);
    DECL_DLL PKIWIRESULT kiwi_analyzeW(PKIWI handle, const wchar_t* text, int topN);
    DECL_DLL PKIWIRESULT kiwi_analyze(PKIWI handle, const char* text, int topN);
    DECL_DLL int kiwi_clearCache(PKIWI handle);
    DECL_DLL int kiwi_close(PKIWI handle);

    DECL_DLL int kiwiResult_getSize(PKIWIRESULT result);
    DECL_DLL float kiwiResult_getProb(PKIWIRESULT result, int index);
    DECL_DLL int kiwiResult_getWordNum(PKIWIRESULT result, int index);
    DECL_DLL const wchar_t* kiwiResult_getWordFormW(PKIWIRESULT result, int index, int num);
    DECL_DLL const wchar_t* kiwiResult_getWordTagW(PKIWIRESULT result, int index, int num);
    DECL_DLL const char* kiwiResult_getWordForm(PKIWIRESULT result, int index, int num);
    DECL_DLL const char* kiwiResult_getWordTag(PKIWIRESULT result, int index, int num);
    DECL_DLL int kiwiResult_close(PKIWIRESULT result);
    */

    internal class Utf8String : IDisposable
    {
        IntPtr iPtr;
        public IntPtr IntPtr { get { return iPtr; } }
        public int BufferLength { get { return iBufferSize; } }
        int iBufferSize;
        public Utf8String(string aValue)
        {
            if (aValue == null)
            {
                iPtr = IntPtr.Zero;
            }
            else
            {
                byte[] bytes = Encoding.UTF8.GetBytes(aValue);
                iPtr = Marshal.AllocHGlobal(bytes.Length + 1);
                Marshal.Copy(bytes, 0, iPtr, bytes.Length);
                Marshal.WriteByte(iPtr, bytes.Length, 0);
                iBufferSize = bytes.Length + 1;
            }
        }
        public void Dispose()
        {
            if (iPtr != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(iPtr);
                iPtr = IntPtr.Zero;
            }
        }
    }

    internal class Utf16String : IDisposable
    {
        IntPtr iPtr;
        public IntPtr IntPtr { get { return iPtr; } }
        public int BufferLength { get { return iBufferSize; } }
        int iBufferSize;
        public Utf16String(string aValue)
        {
            if (aValue == null)
            {
                iPtr = IntPtr.Zero;
            }
            else
            {
                byte[] bytes = new UnicodeEncoding().GetBytes(aValue);
                iPtr = Marshal.AllocHGlobal(bytes.Length + 2);
                Marshal.Copy(bytes, 0, iPtr, bytes.Length);
                Marshal.WriteByte(iPtr, bytes.Length, 0);
                Marshal.WriteByte(iPtr, bytes.Length +1, 0);
                iBufferSize = bytes.Length + 2;
            }
        }
        public void Dispose()
        {
            if (iPtr != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(iPtr);
                iPtr = IntPtr.Zero;
            }
        }
    }

    class KiwiCS
    {
        private const string DLLPATH = "KiwiC.dll";
        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwi_version();

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static IntPtr kiwi_error();

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static IntPtr kiwi_init(IntPtr modelPath, int maxCache);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwi_prepare(IntPtr handle);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwi_close(IntPtr handle);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static IntPtr kiwi_analyzeW(IntPtr handle, IntPtr text, int topN);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static IntPtr kiwi_analyze(IntPtr handle, IntPtr text, int topN);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwi_clearCache(IntPtr handle);


        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwiResult_getSize(IntPtr result);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static float kiwiResult_getProb(IntPtr result, int index);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwiResult_getWordNum(IntPtr result, int index);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static IntPtr kiwiResult_getWordFormW(IntPtr result, int index, int num);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static IntPtr kiwiResult_getWordTagW(IntPtr result, int index, int num);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static IntPtr kiwiResult_getWordForm(IntPtr result, int index, int num);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static IntPtr kiwiResult_getWordTag(IntPtr result, int index, int num);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwiResult_close(IntPtr result);

        protected IntPtr inst;

        public struct Result
        {
            public Tuple<string, string>[] morphs;
            public float prob;
        }

        public static int Version()
        {
            return kiwi_version();
        }

        public KiwiCS(string modelPath, int cacheSize)
        {
            inst = kiwi_init(new Utf8String(modelPath).IntPtr, cacheSize);
            if(inst == IntPtr.Zero)
            {
                throw new System.Exception(Marshal.PtrToStringAnsi(kiwi_error()));
            }
            kiwi_prepare(inst);
        }

        public Result[] analyze(string text, int topN)
        {
            IntPtr res = kiwi_analyzeW(inst, new Utf16String(text).IntPtr, topN);
            int resCount = kiwiResult_getSize(res);
            Result[] ret = new Result[resCount];
            for (int i = 0; i < resCount; ++i)
            {
                int num = kiwiResult_getWordNum(res, i);
                ret[i].morphs = new Tuple<string, string>[num];
                for(int j = 0; j < num; ++j)
                {
                    ret[i].morphs[j] = Tuple.Create(
                        Marshal.PtrToStringUni(kiwiResult_getWordFormW(res, i, j)),
                        Marshal.PtrToStringUni(kiwiResult_getWordTagW(res, i, j))
                    );
                }
                ret[i].prob = kiwiResult_getProb(res, i);
            }
            kiwiResult_close(res);
            return ret;
        }

        ~KiwiCS()
        {
            if(inst != IntPtr.Zero) kiwi_close(inst);
        }
    }
}
