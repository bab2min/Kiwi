using System;
using System.Runtime.InteropServices;
using System.Text;
using System.IO;
using System.Collections.Generic;

namespace KiwiGui
{
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
            var subfolder = "\\bin_" + (is64 ? "x64\\" : "x86\\");
#if DEBUG
            myFolder += "\\..\\..";
#endif

            LoadLibrary(myFolder + subfolder + DLLPATH);
        }

        public class KiwiException : Exception
        {
            public KiwiException()
            {
            }

            public KiwiException(string message) : base(message)
            {
            }

            public KiwiException(string message, Exception inner) : base(message, inner)
            {
            }
        }

        [DllImport("kernel32.dll")]
        private static extern IntPtr LoadLibrary(string dllToLoad);

        [DllImport("kernel32.dll", EntryPoint = "CopyMemory", SetLastError = false)]
        private static extern void CopyMemory(IntPtr dest, IntPtr src, uint count);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int CReader(int id, IntPtr buf, IntPtr userData);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int CReceiver(int id, IntPtr kiwiResult, IntPtr userData);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern int kiwi_version();

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr kiwi_error();

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr kiwi_init(IntPtr modelPath, int maxCache, int options);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern int kiwi_prepare(IntPtr handle);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern int kiwi_close(IntPtr handle);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern int kiwi_addUserWord(IntPtr handle, IntPtr word, IntPtr pos);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern int kiwi_loadUserDict(IntPtr handle, IntPtr dictPath);

        // analyzing function
        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr kiwi_analyzeW(IntPtr handle, IntPtr text, int topN);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr kiwi_analyze(IntPtr handle, IntPtr text, int topN);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern int kiwi_analyzeMW(IntPtr handle, CReader reader, CReceiver receiver, IntPtr userData, int topN);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern int kiwi_analyzeM(IntPtr handle, CReader reader, CReceiver receiver, IntPtr userData, int topN);

        // extraction functions
        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr kiwi_extractWordsW(IntPtr handle, CReader reader, IntPtr userData, int minCnt, int maxWordLen, float minScore);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr kiwi_extractFilterWordsW(IntPtr handle, CReader reader, IntPtr userData, int minCnt, int maxWordLen, float minScore, float posThreshold);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr kiwi_extractAddWordsW(IntPtr handle, CReader reader, IntPtr userData, int minCnt, int maxWordLen, float minScore, float posThreshold);


        // result management functions
        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern int kiwiResult_getSize(IntPtr result);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern float kiwiResult_getProb(IntPtr result, int index);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern int kiwiResult_getWordNum(IntPtr result, int index);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr kiwiResult_getWordFormW(IntPtr result, int index, int num);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr kiwiResult_getWordTagW(IntPtr result, int index, int num);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr kiwiResult_getWordForm(IntPtr result, int index, int num);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr kiwiResult_getWordTag(IntPtr result, int index, int num);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwiResult_getWordPosition(IntPtr result, int index, int num);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        extern public static int kiwiResult_getWordLength(IntPtr result, int index, int num);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern int kiwiResult_close(IntPtr result);

        // word management functions
        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern int kiwiWords_getSize(IntPtr result);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr kiwiWords_getWordFormW(IntPtr result, int index);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern float kiwiWords_getScore(IntPtr result, int index);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern int kiwiWords_getFreq(IntPtr result, int index);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern float kiwiWords_getPosScore(IntPtr result, int index);

        [DllImport(DLLPATH, CallingConvention = CallingConvention.Cdecl)]
        private static extern int kiwiWords_close(IntPtr result);

        /*
         * - id: line number of item about to be read. if id == 0, input file should be roll-backed and read from the beginning of the file.
         * if there is no line to be read, it should return null or ""
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
            public Tuple<string, string, int, int>[] morphs;
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
            if (resCount < 0) throw new KiwiException(Marshal.PtrToStringAnsi(kiwi_error()));
            Result[] ret = new Result[resCount];
            for (int i = 0; i < resCount; ++i)
            {
                int num = kiwiResult_getWordNum(kiwiresult, i);
                ret[i].morphs = new Tuple<string, string, int, int>[num];
                for (int j = 0; j < num; ++j)
                {
                    ret[i].morphs[j] = Tuple.Create(
                        Marshal.PtrToStringUni(kiwiResult_getWordFormW(kiwiresult, i, j)),
                        Marshal.PtrToStringUni(kiwiResult_getWordTagW(kiwiresult, i, j)),
                        kiwiResult_getWordPosition(kiwiresult, i, j),
                        kiwiResult_getWordLength(kiwiresult, i, j)
                    );
                }
                ret[i].prob = kiwiResult_getProb(kiwiresult, i);
            }
            return ret;
        }

        private static ExtractedWord[] ToExtractedWord(IntPtr kiwiresult)
        {
            int resCount = kiwiWords_getSize(kiwiresult);
            if (resCount < 0) throw new KiwiException(Marshal.PtrToStringAnsi(kiwi_error()));
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
                return ki.readResult[id]?.Length ?? 0;
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
         * - options: 0 for not loading default dictionary, KIWI_LOAD_DEFAULT_DICT for loading default dictionary
         */
        public KiwiCS(string modelPath, int numThread, int options = KIWI_LOAD_DEFAULT_DICT)
        {
            if(numThread < 0) throw new KiwiException("numThread must > 0");
            inst = kiwi_init(new Utf8String(modelPath).IntPtr, numThread, options);
            if(inst == IntPtr.Zero) throw new KiwiException(Marshal.PtrToStringAnsi(kiwi_error()));
        }

        /*
         * 'prepare' should be called before using 'analyze' method.
         * after 'prepare' is called, 'addUserWord', 'loadUserDictionary' and 'extractAddWords' methods are disabled.
         */
        public int prepare()
        {
            return kiwi_prepare(inst);
        }

        public Result[] analyze(string text, int topN)
        {
            IntPtr res = kiwi_analyzeW(inst, new Utf16String(text).IntPtr, topN);
            if (inst == IntPtr.Zero) throw new KiwiException(Marshal.PtrToStringAnsi(kiwi_error()));
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
            if (inst == IntPtr.Zero) throw new KiwiException(Marshal.PtrToStringAnsi(kiwi_error()));
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

        /*
        * 'addUserWord', 'loadUserDictionary' and 'extractAddWords' methods only can be called before prepare being called.
        */
        public ExtractedWord[] extractAddWords(Reader reader, int minCnt = 5, int maxWordLen = 10, float minScore = 0.1f, float posThreshold = -3)
        {
            GCHandle handle = GCHandle.Alloc(this);
            this.reader = reader;
            readResult = new Dictionary<int, string>();
            IntPtr ret = kiwi_extractAddWordsW(inst, readerInst, (IntPtr)handle, minCnt, maxWordLen, minScore, posThreshold);
            handle.Free();
            if (inst == IntPtr.Zero) throw new KiwiException(Marshal.PtrToStringAnsi(kiwi_error()));
            ExtractedWord[] words = ToExtractedWord(ret);
            kiwiWords_close(ret);
            return words;
        }

        public int addUserWord(string word, string pos)
        {
            int ret = kiwi_addUserWord(inst, new Utf8String(word).IntPtr, new Utf8String(pos).IntPtr);
            if (ret < 0) throw new KiwiException(Marshal.PtrToStringAnsi(kiwi_error()));
            return ret;
        }

        public int loadUserDictionary(string dictPath)
        {
            int ret = kiwi_loadUserDict(inst, new Utf8String(dictPath).IntPtr);
            if (ret < 0) throw new KiwiException(Marshal.PtrToStringAnsi(kiwi_error()));
            return ret;
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
            if (ret < 0) throw new KiwiException(Marshal.PtrToStringAnsi(kiwi_error()));
            return ret;
        }

        ~KiwiCS()
        {
            if (inst != IntPtr.Zero)
            {
                if(kiwi_close(inst) < 0) throw new KiwiException(Marshal.PtrToStringAnsi(kiwi_error()));
            }
        }
    }
}
