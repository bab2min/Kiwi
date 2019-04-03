using System;
using System.Runtime.InteropServices;
using System.Text;
using System.IO;
using System.Collections.Generic;

namespace KiwiGui
{
    /*
    typedef int(*kiwi_reader)(int, char*, void*);
    typedef int(*kiwi_readerW)(int, kchar16_t*, void*);

    typedef int(*kiwi_receiver)(int, PKIWIRESULT, void*);

    enum
    {
	    KIWI_LOAD_DEFAULT_DICT = 1
    };

    DECL_DLL int kiwi_version();
    DECL_DLL const char* kiwi_error();

    DECL_DLL PKIWI kiwi_init(const char* modelPath, int numThread, int options);
    DECL_DLL int kiwi_addUserWord(PKIWI handle, const char* word, const char* pos);
    DECL_DLL int kiwi_loadUserDict(PKIWI handle, const char* dictPath);
    DECL_DLL PKIWIWORDS kiwi_extractWords(PKIWI handle, kiwi_reader reader, void* userData, int minCnt, int maxWordLen, float minScore);
    DECL_DLL PKIWIWORDS kiwi_extractFilterWords(PKIWI handle, kiwi_reader reader, void* userData, int minCnt, int maxWordLen, float minScore, float posThreshold);
    DECL_DLL PKIWIWORDS kiwi_extractAddWords(PKIWI handle, kiwi_reader reader, void* userData, int minCnt, int maxWordLen, float minScore, float posThreshold);
    DECL_DLL PKIWIWORDS kiwi_extractWordsW(PKIWI handle, kiwi_readerW reader, void* userData, int minCnt, int maxWordLen, float minScore);
    DECL_DLL PKIWIWORDS kiwi_extractFilterWordsW(PKIWI handle, kiwi_readerW reader, void* userData, int minCnt, int maxWordLen, float minScore, float posThreshold);
    DECL_DLL PKIWIWORDS kiwi_extractAddWordsW(PKIWI handle, kiwi_readerW reader, void* userData, int minCnt, int maxWordLen, float minScore, float posThreshold);
    DECL_DLL int kiwi_prepare(PKIWI handle);
    DECL_DLL PKIWIRESULT kiwi_analyzeW(PKIWI handle, const kchar16_t* text, int topN);
    DECL_DLL PKIWIRESULT kiwi_analyze(PKIWI handle, const char* text, int topN);
    DECL_DLL int kiwi_analyzeMW(PKIWI handle, kiwi_readerW reader, kiwi_receiver receiver, void* userData, int topN);
    DECL_DLL int kiwi_analyzeM(PKIWI handle, kiwi_reader reader, kiwi_receiver receiver, void* userData, int topN);
    DECL_DLL int kiwi_performW(PKIWI handle, kiwi_readerW reader, kiwi_receiver receiver, void* userData, int topN, int minCnt, int maxWordLen, float minScore, float posThreshold);
    DECL_DLL int kiwi_perform(PKIWI handle, kiwi_reader reader, kiwi_receiver receiver, void* userData, int topN, int minCnt, int maxWordLen, float minScore, float posThreshold);
    DECL_DLL int kiwi_clearCache(PKIWI handle);
    DECL_DLL int kiwi_close(PKIWI handle);

    DECL_DLL int kiwiResult_getSize(PKIWIRESULT result);
    DECL_DLL float kiwiResult_getProb(PKIWIRESULT result, int index);
    DECL_DLL int kiwiResult_getWordNum(PKIWIRESULT result, int index);
    DECL_DLL const kchar16_t* kiwiResult_getWordFormW(PKIWIRESULT result, int index, int num);
    DECL_DLL const kchar16_t* kiwiResult_getWordTagW(PKIWIRESULT result, int index, int num);
    DECL_DLL const char* kiwiResult_getWordForm(PKIWIRESULT result, int index, int num);
    DECL_DLL const char* kiwiResult_getWordTag(PKIWIRESULT result, int index, int num);
    DECL_DLL int kiwiResult_getWordPosition(PKIWIRESULT result, int index, int num);
    DECL_DLL int kiwiResult_getWordLength(PKIWIRESULT result, int index, int num);
    DECL_DLL int kiwiResult_close(PKIWIRESULT result);

    DECL_DLL int kiwiWords_getSize(PKIWIWORDS result);
    DECL_DLL const kchar16_t* kiwiWords_getWordFormW(PKIWIWORDS result, int index);
    DECL_DLL const char* kiwiWords_getWordForm(PKIWIWORDS result, int index);
    DECL_DLL float kiwiWords_getScore(PKIWIWORDS result, int index);
    DECL_DLL int kiwiWords_getFreq(PKIWIWORDS result, int index);
    DECL_DLL float kiwiWords_getPosScore(PKIWIWORDS result, int index);
    DECL_DLL int kiwiWords_close(PKIWIWORDS result);
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

    public class KiwiCS
    {

        private const string DLLPATH = "KiwiC.dll";
        static KiwiCS()
        {
            var myPath = new Uri(typeof(KiwiCS).Assembly.CodeBase).LocalPath;
            var myFolder = Path.GetDirectoryName(myPath);

            var is64 = IntPtr.Size == 8;
            var subfolder = is64 ? "\\bin_x64\\" : "\\bin_x86\\";
#if DEBUG
            subfolder = "\\..\\.." + subfolder;
#endif

            LoadLibrary(myFolder + subfolder + DLLPATH);
        }

        [DllImport("kernel32.dll")]
        private static extern IntPtr LoadLibrary(string dllToLoad);

        [DllImport("kernel32.dll", EntryPoint = "CopyMemory", SetLastError = false)]
        private static extern void CopyMemory(IntPtr dest, IntPtr src, uint count);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate int CReader(int id, IntPtr buf, IntPtr userData);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate int CReceiver(int id, IntPtr kiwiResult, IntPtr userData);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwi_version();

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static IntPtr kiwi_error();

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static IntPtr kiwi_init(IntPtr modelPath, int maxCache, int options);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwi_prepare(IntPtr handle);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwi_close(IntPtr handle);
        
        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwi_clearCache(IntPtr handle);

        // analyzing function
        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static IntPtr kiwi_analyzeW(IntPtr handle, IntPtr text, int topN);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static IntPtr kiwi_analyze(IntPtr handle, IntPtr text, int topN);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwi_analyzeMW(IntPtr handle, CReader reader, CReceiver receiver, IntPtr userData, int topN);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwi_analyzeM(IntPtr handle, CReader reader, CReceiver receiver, IntPtr userData, int topN);

        // extraction functions
        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static IntPtr kiwi_extractWordsW(IntPtr handle, CReader reader, IntPtr userData, int minCnt, int maxWordLen, float minScore);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static IntPtr kiwi_extractFilterWordsW(IntPtr handle, CReader reader, IntPtr userData, int minCnt, int maxWordLen, float minScore, float posThreshold);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static IntPtr kiwi_extractAddWordsW(IntPtr handle, CReader reader, IntPtr userData, int minCnt, int maxWordLen, float minScore, float posThreshold);


        // result management functions
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

        // word management functions
        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwiWords_getSize(IntPtr result);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static IntPtr kiwiWords_getWordFormW(IntPtr result, int index);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static float kiwiWords_getScore(IntPtr result, int index);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwiWords_getFreq(IntPtr result, int index);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static float kiwiWords_getPosScore(IntPtr result, int index);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwiWords_close(IntPtr result);

        /*
         * - id: line number of item about to be read. if id == 0, input file should be roll-backed and read from the beginning of the file.
         */
        public delegate string Reader(int id);

        /*
         * - id: line number of item read.
         * - res: analyzed result of input item numbered 'id'
         */
        public delegate int Receiver(int id, Result[] res);

        private IntPtr inst;
        private Dictionary<int, string> readResult;
        private Reader reader;
        private Receiver receiver;

        public struct Result
        {
            public Tuple<string, string>[] morphs;
            public float prob;
        }

        public struct ExtractedWord
        {
            public string word;
            public float score, posScore;
            public int freq;
        }

        private static Result[] ToResult(IntPtr kiwiresult)
        {
            int resCount = kiwiResult_getSize(kiwiresult);
            Result[] ret = new Result[resCount];
            for (int i = 0; i < resCount; ++i)
            {
                int num = kiwiResult_getWordNum(kiwiresult, i);
                ret[i].morphs = new Tuple<string, string>[num];
                for (int j = 0; j < num; ++j)
                {
                    ret[i].morphs[j] = Tuple.Create(
                        Marshal.PtrToStringUni(kiwiResult_getWordFormW(kiwiresult, i, j)),
                        Marshal.PtrToStringUni(kiwiResult_getWordTagW(kiwiresult, i, j))
                    );
                }
                ret[i].prob = kiwiResult_getProb(kiwiresult, i);
            }
            return ret;
        }

        private static ExtractedWord[] ToExtractedWord(IntPtr kiwiresult)
        {
            int resCount = kiwiWords_getSize(kiwiresult);
            ExtractedWord[] ret = new ExtractedWord[resCount];
            for (int i = 0; i < resCount; ++i)
            {
                ret[i].word = Marshal.PtrToStringUni(kiwiWords_getWordFormW(kiwiresult, i));
                ret[i].score = kiwiWords_getScore(kiwiresult, i);
                ret[i].posScore = kiwiWords_getPosScore(kiwiresult, i);
                ret[i].freq = kiwiWords_getFreq(kiwiresult, i);
            }
            return ret;
        }

        private static CReader readerInst = (int id, IntPtr buf, IntPtr userData) =>
        {
            GCHandle handle = (GCHandle)userData;
            KiwiCS ki = handle.Target as KiwiCS;
            if(!ki.readResult.ContainsKey(id))
            {
                ki.readResult.Clear();
                ki.readResult[id] = ki.reader(id);
            }
            if(buf == IntPtr.Zero)
            {
                return ki.readResult[id].Length;
            }
            CopyMemory(buf, new Utf16String(ki.readResult[id]).IntPtr, (uint)ki.readResult[id].Length * 2);
            return 0;
        };

        private static CReceiver receiverInst = (int id, IntPtr kiwiResult, IntPtr userData) =>
        {
            GCHandle handle = (GCHandle)userData;
            KiwiCS ki = handle.Target as KiwiCS;
            return ki.receiver(id, ToResult(kiwiResult));
        };

        
        public static int Version()
        {
            return kiwi_version();
        }

        public const int KIWI_LOAD_DEFAULT_DICT = 1;

        /*
         * - modelPath: folder path where model files are located
         * - numThread: the number of threads Kiwi will use. 1 for single thread processing, 0 for all threads available
         * - options: 0 for not loading default dictionary, 1 for loading default dictionary
         */
        public KiwiCS(string modelPath, int numThread, int options = KIWI_LOAD_DEFAULT_DICT)
        {
            inst = kiwi_init(new Utf8String(modelPath).IntPtr, numThread, options);
            if(inst == IntPtr.Zero)
            {
                throw new System.Exception(Marshal.PtrToStringAnsi(kiwi_error()));
            }
            kiwi_prepare(inst);
        }

        public Result[] analyze(string text, int topN)
        {
            IntPtr res = kiwi_analyzeW(inst, new Utf16String(text).IntPtr, topN);
            Result[] ret = ToResult(res);
            kiwiResult_close(res);
            return ret;
        }

        /*
         * - reader: a callback function that provides input texts.
         * - minCnt:
         * - maxWordLen:
         * - minScore:
         */
        public ExtractedWord[] extractWords(Reader reader, int minCnt = 5, int maxWordLen = 10, float minScore = 0.1f)
        {
            GCHandle handle = GCHandle.Alloc(this);
            this.reader = reader;
            readResult = new Dictionary<int, string>();
            IntPtr ret = kiwi_extractWordsW(inst, readerInst, (IntPtr)handle, minCnt, maxWordLen, minScore);
            handle.Free();
            ExtractedWord[] words = ToExtractedWord(ret);
            kiwiWords_close(ret);
            return words;
        }

        public ExtractedWord[] extractFilterWords(Reader reader, int minCnt = 5, int maxWordLen = 10, float minScore = 0.1f, float posThreshold = -3)
        {
            GCHandle handle = GCHandle.Alloc(this);
            this.reader = reader;
            readResult = new Dictionary<int, string>();
            IntPtr ret = kiwi_extractFilterWordsW(inst, readerInst, (IntPtr)handle, minCnt, maxWordLen, minScore, posThreshold);
            handle.Free();
            ExtractedWord[] words = ToExtractedWord(ret);
            kiwiWords_close(ret);
            return words;
        }

        public ExtractedWord[] extractAddWords(Reader reader, int minCnt = 5, int maxWordLen = 10, float minScore = 0.1f, float posThreshold = -3)
        {
            GCHandle handle = GCHandle.Alloc(this);
            this.reader = reader;
            readResult = new Dictionary<int, string>();
            IntPtr ret = kiwi_extractAddWordsW(inst, readerInst, (IntPtr)handle, minCnt, maxWordLen, minScore, posThreshold);
            handle.Free();
            ExtractedWord[] words = ToExtractedWord(ret);
            kiwiWords_close(ret);
            return words;
        }

        /*
         * - reader: a callback function that provides input texts.
         * - receiver: a callback function that consumes analyzed results.
         */
        public int analyze(Reader reader, Receiver receiver, int topN)
        {
            GCHandle handle = GCHandle.Alloc(this);
            this.reader = reader;
            this.receiver = receiver;
            readResult = new Dictionary<int, string>();
            int ret = kiwi_analyzeMW(inst, readerInst, receiverInst, (IntPtr)handle, topN);
            handle.Free();
            return ret;
        }

        ~KiwiCS()
        {
            if(inst != IntPtr.Zero) kiwi_close(inst);
        }
    }
}
