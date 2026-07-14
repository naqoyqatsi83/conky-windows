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
            // Ensure working directory is our executable location so .NET
            // resolves LibreHardwareMonitorLib.dll correctly regardless of
            // how the task scheduler launched us.
            Environment.CurrentDirectory = AppDomain.CurrentDomain.BaseDirectory;

            string appData = Environment.GetFolderPath(
                Environment.SpecialFolder.CommonApplicationData);
            string tempFile = Path.Combine(appData, "Conky", "temp.dat");
            string gpuFile = Path.Combine(appData, "Conky", "gpu.dat");
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
                    foreach (var hardware in computer.Hardware)
                    {
                        hardware.Update();

                        // Detect GPU hardware
                        if (hardware.HardwareType == HardwareType.GpuNvidia ||
                            hardware.HardwareType == HardwareType.GpuAmd ||
                            hardware.HardwareType == HardwareType.GpuIntel)
                        {
                            int id = gpuLines.Count;
                            int temp = 0;
                            int util = 0;
                            ulong memUsed = 0;
                            ulong memTotal = 0;
                            int fan = 0;
                            string name = hardware.Name.Replace("|", "/");  // pipe is our separator

                            foreach (var sensor in hardware.Sensors)
                            {
                                if (!sensor.Value.HasValue) continue;

                                if (sensor.SensorType == SensorType.Temperature &&
                                    sensor.Value.Value > 0)
                                {
                                    temp = (int)Math.Round(sensor.Value.Value);
                                }
                                else if (sensor.SensorType == SensorType.Load &&
                                         sensor.Value.Value >= 0)
                                {
                                    // "GPU Core" load is the overall GPU utilization
                                    if (sensor.Name.IndexOf("Core", StringComparison.OrdinalIgnoreCase) >= 0 ||
                                        sensor.Name.IndexOf("GPU", StringComparison.OrdinalIgnoreCase) >= 0)
                                    {
                                        if (sensor.Value.Value > util)
                                            util = (int)Math.Round(sensor.Value.Value);
                                    }
                                }
                                else if (sensor.SensorType == SensorType.SmallData &&
                                         sensor.Value.Value >= 0)
                                {
                                    // Memory used (bytes) — some LHM builds report this as SmallData
                                    if (sensor.Name.IndexOf("Used", StringComparison.OrdinalIgnoreCase) >= 0 &&
                                        sensor.Name.IndexOf("Mem", StringComparison.OrdinalIgnoreCase) >= 0)
                                    {
                                        double.TryParse(sensor.Value.ToString(), out double mem);
                                        memUsed = (ulong)mem;
                                    }
                                }
                            }

                            // Sub-hardware (e.g. individual GPU cores on multi-GPU)
                            foreach (var sub in hardware.SubHardware)
                            {
                                sub.Update();
                                foreach (var sensor in sub.Sensors)
                                {
                                    if (!sensor.Value.HasValue) continue;
                                    if (sensor.SensorType == SensorType.Temperature &&
                                        sensor.Value.Value > 0 && temp == 0)
                                    {
                                        temp = (int)Math.Round(sensor.Value.Value);
                                    }
                                    if (sensor.SensorType == SensorType.Fan &&
                                        sensor.Value.Value > 0 && fan == 0)
                                    {
                                        fan = (int)Math.Round(sensor.Value.Value);
                                    }
                                }
                            }

                            // Also check all sensors for memory/fan at top level
                            foreach (var sensor in hardware.Sensors)
                            {
                                if (!sensor.Value.HasValue) continue;
                                if (sensor.SensorType == SensorType.Load &&
                                    sensor.Name.IndexOf("Memory", StringComparison.OrdinalIgnoreCase) >= 0)
                                {
                                    // Memory controller load — approximate usage %
                                }
                                else if (sensor.SensorType == SensorType.Fan &&
                                         sensor.Value.Value > 0 && fan == 0)
                                {
                                    fan = (int)Math.Round(sensor.Value.Value);
                                }
                            }

                            // For memory, compute from Load + total VRAM if available
                            // LibreHardwareMonitor reports memory used/total via SmallData or through Load
                            // We'll try to get more accurate values later — for now report what we can
                            gpuLines.Add(string.Format("{0}|{1}|{2}|{3}|{4}|{5}|{6}",
                                id, temp, util, memUsed, memTotal, fan, name));
                        }
                    }

                    // Write GPU data
                    if (gpuLines.Count > 0)
                    {
                        File.WriteAllLines(gpuFile, gpuLines);
                    }
                    else
                    {
                        // Empty file = no GPUs found
                        File.WriteAllText(gpuFile, "");
                    }
                }
                catch (Exception) { }
                System.Threading.Thread.Sleep(3000);
            }
        }
    }
}
