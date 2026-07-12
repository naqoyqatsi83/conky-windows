using System;
using System.IO;
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

            string tempFile = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
                "Conky", "temp.dat");
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
                                    goto write;
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
                                        goto write;
                                    }
                                }
                            }
                        }
                    }
                    write:
                    File.WriteAllText(tempFile, result + "\n");
                }
                catch (Exception) { }
                System.Threading.Thread.Sleep(3000);
            }
        }
    }
}
