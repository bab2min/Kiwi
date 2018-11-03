using System;
using System.Collections.Generic;
using System.Linq;
using System.Management;
using System.Text;
using System.Threading.Tasks;

namespace KiwiGui
{
    public class PlatformCheck
    {
        [System.Runtime.InteropServices.DllImport("kernel32.dll")]
        private static extern long GetEnabledXStateFeatures();

        [System.Runtime.InteropServices.DllImport("kernel32.dll")]
        private static extern Int32 IsProcessorFeaturePresent(Int32 ProcessorFeature);

        public static bool Is64Bit()
        {
            return IntPtr.Size == 8;
        }

        public static string GetBinPath()
        {
            return (Is64Bit() ? "x64" : "x86") + "/tomoto.exe";
        }

        public enum SIMD
        {
            None,
            SSE,
            SSE2,
            SSE3,
            AVX
        }

        public static SIMD GetSIMDAvailable()
        {
            try
            {
                if ((GetEnabledXStateFeatures() & 4) != 0) return SIMD.AVX;
            }
            catch
            {
            }

            try
            {
                if (IsProcessorFeaturePresent(13) != 0) return SIMD.SSE3;
                if (IsProcessorFeaturePresent(10) != 0) return SIMD.SSE2;
                if (IsProcessorFeaturePresent(6) != 0) return SIMD.SSE;
            }
            catch
            {
            }
            return SIMD.None;
        }

        public class SystemInfo
        {
            public string cpuClockSpeed, cpuName, cpuManufacturer, cpuVersion;
            public long physicalMemory;
            public string osName;
        }

        public static SystemInfo GetSystemInfo()
        {
            SystemInfo si = new SystemInfo();
            using (ManagementObjectSearcher win32Proc = new ManagementObjectSearcher("select * from Win32_Processor"),
                win32CompSys = new ManagementObjectSearcher("select * from Win32_ComputerSystem"),
                win32OS = new ManagementObjectSearcher("select * from Win32_OperatingSystem"))
            {
                foreach (ManagementObject obj in win32Proc.Get())
                {
                    si.cpuClockSpeed = obj["MaxClockSpeed"].ToString();
                    si.cpuName = obj["Name"].ToString();
                    si.cpuManufacturer = obj["Manufacturer"].ToString();
                    si.cpuVersion = obj["Version"].ToString();
                }
                foreach (ManagementObject obj in win32CompSys.Get())
                {
                    si.physicalMemory = long.Parse(obj["TotalPhysicalMemory"].ToString());
                }
                foreach (ManagementObject obj in win32OS.Get())
                {
                    si.osName = obj["Caption"].ToString();
                }
            }
            return si;
        }

        public static string GetUniqueComputerID()
        {
            string cpuInfo = string.Empty;
            ManagementClass mc = new ManagementClass("win32_processor");
            ManagementObjectCollection moc = mc.GetInstances();

            foreach (ManagementObject mo in moc)
            {
                cpuInfo = mo.Properties["processorID"].Value.ToString();
                break;
            }

            string drive = "C";
            ManagementObject dsk = new ManagementObject(
                @"win32_logicaldisk.deviceid=""" + drive + @":""");
            dsk.Get();
            string volumeSerial = dsk["VolumeSerialNumber"].ToString();

            return cpuInfo + "_" + volumeSerial;
        }
    }
}
