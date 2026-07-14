using System;
using System.IO;
using System.Linq;
using LibreHardwareMonitor.Hardware;

namespace ConkyTemp
{
    class Program
    {
        static void Main()
        {
            Environment.CurrentDirectory = AppDomain.CurrentDomain.BaseDirectory;

            string appData = Environment.GetFolderPath(
                Environment.SpecialFolder.CommonApplicationData);
            string tempFile = Path.Combine(appData, "Conky", "temp.dat");
            string gpuFile = Path.Combine(appData, "Conky", "gpu.dat");
            string debugFile = Path.Combine(appData, "Conky", "lhm_debug.txt");
            Directory.CreateDirectory(Path.GetDirectoryName(tempFile));

            var computer = new Computer
            {
                IsCpuEnabled = true,
                IsGpuEnabled = true
            };
            computer.Open();

            while (true)
            {
                try
                {
                    // ---- CPU temperature ----
                    string result = "-1";
                    foreach (var hardware in computer.Hardware)
                    {
                        hardware.Update();
                        foreach (var sensor in hardware.Sensors)
                        {
                            if (sensor.SensorType == SensorType.Temperature && sensor.Value.HasValue)
                            {
                                double v = sensor.Value.Value;
                                if (v > 0)
                                {
                                    result = string.Format("{0:F0}", v);
                                    goto write_cpu;
                                }
                            }
                        }
                        foreach (var sub in hardware.SubHardware)
                        {
                            sub.Update();
                            foreach (var sensor in sub.Sensors)
                            {
                                if (sensor.SensorType == SensorType.Temperature && sensor.Value.HasValue)
                                {
                                    double v = sensor.Value.Value;
                                    if (v > 0)
                                    {
                                        result = string.Format("{0:F0}", v);
                                        goto write_cpu;
                                    }
                                }
                            }
                        }
                    }
                    write_cpu:
                    File.WriteAllText(tempFile, result + "\n");

                    // ---- GPU data ----
                    var gpuLines = new System.Collections.Generic.List<string>();
                    var debugLines = new System.Collections.Generic.List<string>();
                    foreach (var hardware in computer.Hardware)
                    {
                        hardware.Update();

                        if (hardware.HardwareType == HardwareType.GpuNvidia ||
                            hardware.HardwareType == HardwareType.GpuAmd ||
                            hardware.HardwareType == HardwareType.GpuIntel)
                        {
                            int id = gpuLines.Count;
                            double temp = 0;
                            double util = 0;
                            ulong memUsed = 0;
                            ulong memTotal = 0;
                            double fan = 0;
                            string name = hardware.Name.Replace("|", "/");

                            debugLines.Add("=== GPU " + id + ": " + hardware.HardwareType + " - " + name + " ===");

                            // Collect ALL sensors at the top level
                            foreach (var sensor in hardware.Sensors)
                            {
                                if (!sensor.Value.HasValue) continue;
                                debugLines.Add(string.Format("  Sensor: {0} | {1} = {2}",
                                    sensor.SensorType, sensor.Name, sensor.Value));

                                switch (sensor.SensorType)
                                {
                                    case SensorType.Temperature:
                                        if (sensor.Value.Value > 0 && sensor.Value.Value > temp)
                                            temp = sensor.Value.Value;
                                        break;
                                    case SensorType.Load:
                                        // Utilization = any Load that isn't memory-related
                                        if (sensor.Name.IndexOf("Memory", StringComparison.OrdinalIgnoreCase) < 0 &&
                                            sensor.Name.IndexOf("Mem Controller", StringComparison.OrdinalIgnoreCase) < 0 &&
                                            sensor.Name.IndexOf("MC", StringComparison.OrdinalIgnoreCase) < 0 &&
                                            sensor.Value.Value > util)
                                            util = sensor.Value.Value;
                                        break;
                                    case SensorType.SmallData:
                                        if (sensor.Value.Value >= 0)
                                        {
                                            string sn = sensor.Name;
                                            double sv = sensor.Value.Value;

                                            // Exclude D3D Shared Memory — that's system RAM
                                            // shared with the GPU, not dedicated VRAM.
                                            bool isShared = sn.IndexOf("Shared", StringComparison.OrdinalIgnoreCase) >= 0;

                                            // -- Used memory (only dedicated VRAM) --
                                            if (sn.IndexOf("Used", StringComparison.OrdinalIgnoreCase) >= 0 && !isShared)
                                            {
                                                if (sn.IndexOf("GPU Memory", StringComparison.OrdinalIgnoreCase) >= 0 ||
                                                    sn.IndexOf("Dedicated Memory", StringComparison.OrdinalIgnoreCase) >= 0 ||
                                                    sn.IndexOf("VRAM", StringComparison.OrdinalIgnoreCase) >= 0)
                                                {
                                                    memUsed = Math.Max(memUsed, (ulong)Math.Round(sv));
                                                }
                                            }

                                            // -- Total memory (only dedicated VRAM) --
                                            if (sn.IndexOf("Total", StringComparison.OrdinalIgnoreCase) >= 0 && !isShared)
                                            {
                                                if (sn.IndexOf("GPU Memory", StringComparison.OrdinalIgnoreCase) >= 0 ||
                                                    sn.IndexOf("Dedicated Memory", StringComparison.OrdinalIgnoreCase) >= 0 ||
                                                    sn.IndexOf("VRAM", StringComparison.OrdinalIgnoreCase) >= 0)
                                                {
                                                    memTotal = Math.Max(memTotal, (ulong)Math.Round(sv));
                                                }
                                            }

                                            // Free → Used calculation (for sensors like "GPU Memory Free")
                                            if (!isShared &&
                                                sn.IndexOf("Free", StringComparison.OrdinalIgnoreCase) >= 0 &&
                                                (sn.IndexOf("GPU Memory", StringComparison.OrdinalIgnoreCase) >= 0 ||
                                                 sn.IndexOf("Dedicated Memory", StringComparison.OrdinalIgnoreCase) >= 0))
                                            {
                                                ulong free = (ulong)Math.Round(sv);
                                                if (memTotal > free)
                                                {
                                                    memUsed = Math.Max(memUsed, memTotal - free);
                                                }
                                            }
                                        }
                                        break;
                                    case SensorType.Fan:
                                        if (sensor.Value.Value > 0 && sensor.Value.Value > fan)
                                            fan = sensor.Value.Value;
                                        break;
                                }
                            }

                            // Also check sub-hardware for fans, temps, and additional sensors
                            foreach (var sub in hardware.SubHardware)
                            {
                                sub.Update();
                                debugLines.Add("  Sub: " + sub.Name);
                                foreach (var sensor in sub.Sensors)
                                {
                                    if (!sensor.Value.HasValue) continue;
                                    debugLines.Add(string.Format("    {0} | {1} = {2}",
                                        sensor.SensorType, sensor.Name, sensor.Value));

                                    if (sensor.SensorType == SensorType.Fan && sensor.Value.Value > 0 && sensor.Value.Value > fan)
                                        fan = sensor.Value.Value;
                                    if (sensor.SensorType == SensorType.Temperature && sensor.Value.Value > 0 && sensor.Value.Value > temp)
                                        temp = sensor.Value.Value;
                                    if (sensor.SensorType == SensorType.Load && sensor.Value.Value > 0 && sensor.Value.Value > util)
                                    {
                                        if (sensor.Name.IndexOf("Memory", StringComparison.OrdinalIgnoreCase) < 0)
                                            util = sensor.Value.Value;
                                    }
                                }
                            }

                            debugLines.Add(string.Format("  => RESULT: temp={0} util={1} memUsed={2} memTotal={3} fan={4}",
                                temp, util, memUsed, memTotal, fan));

                            gpuLines.Add(string.Format("{0}|{1}|{2}|{3}|{4}|{5}|{6}",
                                id, (int)Math.Round(temp), (int)Math.Round(util),
                                memUsed, memTotal, (int)Math.Round(fan), name));
                        }
                    }

                    // Write debug dump
                    try { File.WriteAllLines(debugFile, debugLines); } catch { }

                    if (gpuLines.Count > 0)
                    {
                        File.WriteAllLines(gpuFile, gpuLines);
                    }
                    else
                    {
                        File.WriteAllText(gpuFile, "");
                    }
                }
                catch (Exception) { }
                System.Threading.Thread.Sleep(3000);
            }
        }
    }
}
