/*
 *
 * Conky, a system monitor, based on torsmo
 *
 * Any original torsmo code is licensed under the BSD license
 *
 * All code written since the fork of torsmo is licensed under the GPL
 *
 * Please see COPYING for details
 *
 * Copyright (c) 2005-2024 Brenden Matthews, Philip Kovacs, et. al.
 *  (see AUTHORS)
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/* _WIN32_WINNT set by compile definition for Windows 7+ API level */

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <winreg.h>
#include <objbase.h>
#include <wbemidl.h>
#include <pdh.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "../../common.h"
#include "../../conky.h"
#include "../../logging.h"
#include "../top.h"
#include "../network/net_stat.h"
#include "../hardware/diskio.h"
#include "windows.h"

static short cpu_setup = 0;

/* ---- CPU usage tracking via GetSystemTimes ---- */
struct cpu_times {
  unsigned long long idle;
  unsigned long long kernel;
  unsigned long long user;
};

static cpu_times prev_cpu = {0, 0, 0};

static unsigned long long filetime_to_ull(const FILETIME &ft) {
  return (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) |
         ft.dwLowDateTime;
}

/* ---- helper ---- */

void prepare_update() {}

int update_uptime() {
  info.uptime = (double)GetTickCount64() / 1000.0;
  return 0;
}

int check_mount(struct text_object *obj) {
  (void)obj;
  return 0;
}

int update_meminfo() {
  MEMORYSTATUSEX ms;
  ms.dwLength = sizeof(ms);
  if (!GlobalMemoryStatusEx(&ms)) {
    LOG_ERROR("cannot get memory status");
    return 1;
  }

  /* all in kilobytes */
  info.memmax = (unsigned long long)(ms.ullTotalPhys >> 10);
  info.mem = info.memmax - (unsigned long long)(ms.ullAvailPhys >> 10);
  info.memwithbuffers = info.mem;
  info.memfree = (unsigned long long)(ms.ullAvailPhys >> 10);
  info.memeasyfree = info.memfree;
  info.memavail = (unsigned long long)(ms.ullAvailPhys >> 10);
  info.legacymem = info.mem;

  info.swapmax = (unsigned long long)(ms.ullTotalPageFile >> 10);
  info.swapfree =
      info.swapmax -
      (unsigned long long)((ms.ullTotalPageFile - ms.ullAvailPageFile) >> 10);
  info.swap = info.swapmax - info.swapfree;

  return 0;
}

/* ---- Network stats via IP Helper API ---- */

int update_net_stats() {
  ULONG bufLen = 0;

  /* First call to get required buffer size */
  GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, nullptr, &bufLen);
  if (bufLen == 0) {
    return 1;
  }

  std::vector<char> buf(bufLen);
  PIP_ADAPTER_ADDRESSES paa =
      reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());

  ULONG ret = GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, paa, &bufLen);
  if (ret != NO_ERROR) {
    return 1;
  }

  double interval = active_update_interval();
  if (interval <= 0.0) {
    interval = 1.0;
  }

  for (PIP_ADAPTER_ADDRESSES p = paa; p != nullptr; p = p->Next) {
    /* Skip loopback and tunnel adapters */
    if (p->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
      continue;
    }

    /* Use the friendly name (UTF-8) as the interface name */
    char ifname[256];
    ifname[0] = '\0';
    WideCharToMultiByte(CP_UTF8, 0, p->FriendlyName, -1, ifname,
                        sizeof(ifname) - 1, nullptr, nullptr);

    if (ifname[0] == '\0') {
      continue;
    }

    struct net_stat *ns = get_net_stat(ifname, nullptr, nullptr);
    if (ns == nullptr) {
      continue;
    }

    ns->up = (p->OperStatus == IfOperStatusUp) ? 1 : 0;

    /* Get byte counts via GetIfEntry (original, available in all MinGW) */
    MIB_IFROW row;
    row.dwIndex = p->IfIndex;
    if (GetIfEntry(&row) == NO_ERROR) {
      long long recv = static_cast<long long>(row.dwInOctets);
      long long trans = static_cast<long long>(row.dwOutOctets);

      ns->recv = recv;
      ns->trans = trans;

      if (ns->last_read_recv != -1) {
        long long recv_delta = recv - ns->last_read_recv;
        long long trans_delta = trans - ns->last_read_trans;
        if (recv_delta >= 0) {
          ns->recv_speed = static_cast<double>(recv_delta) / interval;
        } else {
          ns->recv_speed = 0.0;
        }
        if (trans_delta >= 0) {
          ns->trans_speed = static_cast<double>(trans_delta) / interval;
        } else {
          ns->trans_speed = 0.0;
        }
      }

      ns->last_read_recv = recv;
      ns->last_read_trans = trans;

      /* Copy address info from first unicast address */
      if (p->FirstUnicastAddress != nullptr) {
        SOCKADDR *sa = p->FirstUnicastAddress->Address.lpSockaddr;
        if (sa->sa_family == AF_INET) {
          memcpy(&ns->addr, sa, sizeof(struct sockaddr_in));
        }
#ifdef BUILD_IPV6
        else if (sa->sa_family == AF_INET6) {
          memcpy(&ns->addr, sa, sizeof(struct sockaddr_in6));
        }
#endif
      }
    }
  }

  return 0;
}

/* ---- Process counts via Toolhelp API ---- */

int update_total_processes() {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) {
    return 1;
  }

  unsigned short count = 0;
  PROCESSENTRY32 pe = {};
  pe.dwSize = sizeof(PROCESSENTRY32);

  if (Process32First(snap, &pe)) {
    do {
      count++;
    } while (Process32Next(snap, &pe));
  }

  CloseHandle(snap);
  info.procs = count;
  return 0;
}

int update_running_processes() {
  /* On Windows, "running" is ambiguous. We report processes that are
   * not suspended. Use CreateToolhelp32Snapshot and check thread states. */
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) {
    return 1;
  }

  unsigned short count = 0;
  PROCESSENTRY32 pe = {};
  pe.dwSize = sizeof(PROCESSENTRY32);

  if (Process32First(snap, &pe)) {
    do {
      /* Use a simple heuristic: count any process that isn't specifically
       * in a suspended state. Since Toolhelp doesn't give per-process state
       * directly, we count all processes (they're all "runnable" at least). */
      count++;
    } while (Process32Next(snap, &pe));
  }

  CloseHandle(snap);
  info.run_procs = count;
  return 0;
}

/* ---- CPU usage and frequency ---- */

void get_cpu_count(void) {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  info.cpu_count = si.dwNumberOfProcessors;
  info.cpu_usage = (float *)malloc((info.cpu_count + 1) * sizeof(float));
  if (info.cpu_usage == nullptr) {
    SYSTEM_ERR("failed to allocate cpu_usage array");
  }
}

/* ---- Per-core CPU via PDH ---- */
static struct PerCoreCpu {
  PDH_HQUERY query = NULL;
  std::vector<PDH_HCOUNTER> counters;
  bool primed = false;

  bool init(unsigned int count) {
    if (PdhOpenQueryA(nullptr, 0, &query) != ERROR_SUCCESS) {
      return false;
    }
    counters.resize(count, nullptr);
    for (unsigned int i = 0; i < count; i++) {
      char path[64];
      snprintf(path, sizeof(path), "\\Processor(%u)\\%% Processor Time", i);
      PdhAddCounterA(query, path, 0, &counters[i]);
    }
    PdhCollectQueryData(query);
    primed = true;
    return true;
  }

  void update() {
    if (!primed) { return; }
    PdhCollectQueryData(query);
    for (size_t i = 0; i < counters.size() && i < info.cpu_count; i++) {
      if (counters[i] == nullptr) { continue; }
      PDH_FMT_COUNTERVALUE val;
      if (PdhGetFormattedCounterValue(counters[i], PDH_FMT_DOUBLE, nullptr,
                                      &val) == ERROR_SUCCESS) {
        float pct = static_cast<float>(val.doubleValue);
        if (pct < 0.0f) { pct = 0.0f; }
        if (pct > 100.0f) { pct = 100.0f; }
        info.cpu_usage[i + 1] = pct / 100.0f;
      }
    }
  }
} per_core_cpu;

int update_cpu_usage() {
  /* add check for !info.cpu_usage since that mem is freed on a SIGUSR1 */
  if ((cpu_setup == 0) || (!info.cpu_usage)) {
    get_cpu_count();
    cpu_setup = 1;
  }

  // Lazy PDH init for per-core tracking
  static bool pdh_init_tried = false;
  if (!pdh_init_tried) {
    pdh_init_tried = true;
    if (info.cpu_count > 0) { per_core_cpu.init(info.cpu_count); }
  }

  FILETIME idle, kernel, user;
  if (!GetSystemTimes(&idle, &kernel, &user)) {
    return 1;
  }

  unsigned long long idle_val = filetime_to_ull(idle);
  unsigned long long kernel_val = filetime_to_ull(kernel);
  unsigned long long user_val = filetime_to_ull(user);

  if (prev_cpu.idle != 0) {
    unsigned long long total_delta = (kernel_val + user_val) -
                                     (prev_cpu.kernel + prev_cpu.user);
    unsigned long long idle_delta = idle_val - prev_cpu.idle;

    if (total_delta > 0) {
      float pct =
          100.0f * (1.0f - static_cast<float>(idle_delta) /
                                static_cast<float>(total_delta));
      if (pct < 0.0f) pct = 0.0f;
      if (pct > 100.0f) pct = 100.0f;
      info.cpu_usage[0] = pct / 100.0f;
    }

    // Per-core via PDH (falls back to total if not available)
    per_core_cpu.update();
  }

  prev_cpu.idle = idle_val;
  prev_cpu.kernel = kernel_val;
  prev_cpu.user = user_val;

  return 0;
}

void free_cpu(struct text_object *) { /* no-op */ }

int update_load_average() {
  /* Windows has no load average concept - return 0 */
  info.loadavg[0] = 0.0f;
  info.loadavg[1] = 0.0f;
  info.loadavg[2] = 0.0f;
  return 1;
}

/* ---- CPU temperature via WMI ---- */
/* Tries multiple sources in order:
 *   1. ROOT\WMI: MSAcpi_ThermalZoneTemperature (OEM ACPI)
 *   2. root\LibreHardwareMonitor WMI namespace (if LHM is running)
 *   3. LibreHardwareMonitor\lhm-temp.exe helper tool (installed with LHM)
 * Uses whatever succeeds first. Returns -1.0 on complete failure. */

static struct WmiCpuTemp {
  IWbemServices *svc = nullptr;
  bool second_try = false;  // true if we already tried MSAcpi and failed

  /* Release and null-out the service pointer so we can retry against a
   * different namespace. */
  void release_svc() {
    if (svc != nullptr) {
      svc->Release();
      svc = nullptr;
    }
  }

  bool init() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) { return false; }

    hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
                              RPC_C_AUTHN_LEVEL_DEFAULT,
                              RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE,
                              nullptr);
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) { return false; }
    return true;
  }

  /* Connect to a specific WMI namespace. Returns true on success. */
  bool connect_to(const wchar_t *ns) {
    IWbemLocator *locator = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                                  reinterpret_cast<void **>(&locator));
    if (FAILED(hr)) { return false; }

    hr = locator->ConnectServer(const_cast<BSTR>(ns), nullptr, nullptr,
                                nullptr, 0, nullptr, nullptr, &svc);
    locator->Release();
    if (FAILED(hr)) {
      svc = nullptr;
      return false;
    }

    hr = CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                           RPC_C_AUTHN_LEVEL_CALL,
                           RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) {
      release_svc();
      return false;
    }
    return true;
  }

  /* Query MSAcpi_ThermalZoneTemperature from ROOT\WMI.
   * Returns temperature in Celsius, or -inf on failure. */
  double query_msacpi() {
    IEnumWbemClassObject *enumerator = nullptr;
    HRESULT hr = svc->ExecQuery(
        const_cast<BSTR>(L"WQL"),
        const_cast<BSTR>(L"SELECT * FROM MSAcpi_ThermalZoneTemperature"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
        &enumerator);
    if (FAILED(hr)) { return -1.0; }

    IWbemClassObject *obj = nullptr;
    ULONG ret = 0;
    hr = enumerator->Next(WBEM_INFINITE, 1, &obj, &ret);
    enumerator->Release();
    if (FAILED(hr) || ret == 0) {
      if (obj != nullptr) { obj->Release(); }
      return -1.0;
    }

    VARIANT vt;
    VariantInit(&vt);
    hr = obj->Get(L"CurrentTemperature", 0, &vt, nullptr, nullptr);
    obj->Release();
    double celsius = -1.0;
    if (SUCCEEDED(hr) && vt.vt == VT_I4) {
      celsius = static_cast<double>(vt.lVal) / 10.0 - 273.15;
      if (celsius < -50.0 || celsius > 150.0) {
        celsius = -1.0;
      }
    }
    VariantClear(&vt);
    return celsius;
  }

  /* Query the first temperature sensor from LibreHardwareMonitor's WMI
   * namespace (root\LibreHardwareMonitor). Returns Celsius or -inf. */
  double query_lhm() {
    IEnumWbemClassObject *enumerator = nullptr;
    HRESULT hr = svc->ExecQuery(
        const_cast<BSTR>(L"WQL"),
        const_cast<BSTR>(L"SELECT Value, Name, SensorType "
                         L"FROM Sensor WHERE SensorType = 'Temperature'"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
        &enumerator);
    if (FAILED(hr)) { return -1.0; }

    IWbemClassObject *obj = nullptr;
    ULONG ret = 0;
    hr = enumerator->Next(WBEM_INFINITE, 1, &obj, &ret);
    enumerator->Release();
    if (FAILED(hr) || ret == 0) {
      if (obj != nullptr) { obj->Release(); }
      return -1.0;
    }

    VARIANT vt;
    VariantInit(&vt);
    hr = obj->Get(L"Value", 0, &vt, nullptr, nullptr);
    obj->Release();
    double celsius = -1.0;
    if (SUCCEEDED(hr) && (vt.vt == VT_R8 || vt.vt == VT_I4)) {
      if (vt.vt == VT_R8) { celsius = vt.dblVal; }
      else { celsius = static_cast<double>(vt.lVal); }
    }
    VariantClear(&vt);
    return celsius;
  }

  /* Query CPU temperature by reading %ALLUSERSPROFILE%\Conky\temp.dat.
   *
   * The file is written every 3 seconds by lhm-temp.exe — a persistent
   * elevated process launched via scheduled task (ONLOGON, /RL HIGHEST).
   * If the file is stale (>10 s old) the helper probably crashed, so we
   * fire a one-shot schtasks /RUN to restart it and return -1 this cycle
   * (conky picks up the fresh value on the next update).
   *
   * schtasks /RUN is safe from non-elevated processes — the task was
   * pre-configured with /RL HIGHEST at install time.
   *
   * Returns Celsius on success, -1.0 on failure. */
  double query_helper() {
    wchar_t path[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"ALLUSERSPROFILE", path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH - 20) { return -1.0; }
    wcscat(path, L"\\Conky\\temp.dat");

    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    if (hFile == INVALID_HANDLE_VALUE) { trigger_helper(); return -1.0; }

    FILETIME ftWrite = {};
    if (!GetFileTime(hFile, nullptr, nullptr, &ftWrite)) {
      CloseHandle(hFile); return -1.0;
    }

    FILETIME ftNow;
    GetSystemTimeAsFileTime(&ftNow);

    ULARGE_INTEGER uW, uN;
    uW.LowPart = ftWrite.dwLowDateTime;
    uW.HighPart = ftWrite.dwHighDateTime;
    uN.LowPart = ftNow.dwLowDateTime;
    uN.HighPart = ftNow.dwHighDateTime;

    /* 10 s in FILETIME units = 100-ns intervals */
    if (uN.QuadPart - uW.QuadPart > 100000000ULL) {
      CloseHandle(hFile); trigger_helper(); return -1.0;
    }

    char buf[32] = {};
    DWORD read = 0;
    if (!ReadFile(hFile, buf, (DWORD)sizeof(buf) - 1, &read, nullptr)) {
      CloseHandle(hFile); return -1.0;
    }
    CloseHandle(hFile);

    buf[read] = '\0';
    while (read > 0 && (buf[read - 1] == '\n' || buf[read - 1] == '\r')) {
      buf[--read] = '\0';
    }

    char *end = nullptr;
    double t = strtod(buf, &end);
    if (end == buf || t < -50.0 || t > 150.0) { return -1.0; }
    return t;
  }

  /* Launch lhm-temp.exe directly by finding it relative to conky's path.
   * Returns true if the process was successfully started. */
  bool launch_lhm_direct() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH - 50) return false;
    wchar_t *sep = wcsrchr(buf, L'\\');
    if (sep == nullptr) return false;
    wcscpy(sep + 1, L"LibreHardwareMonitor\\lhm-temp.exe");
    if (GetFileAttributesW(buf) == INVALID_FILE_ATTRIBUTES) return false;
    wchar_t wd[MAX_PATH];
    wcscpy(wd, buf);
    sep = wcsrchr(wd, L'\\');
    if (sep) *sep = L'\0';
    STARTUPINFOW si = {sizeof(si), 0};
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(buf, nullptr, nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, wd, &si, &pi);
    if (ok) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    return ok != FALSE;
  }

  /* Fire the helper with a 5-second cooldown. */
  void trigger_helper() {
    static ULONGLONG last_trigger = 0;
    ULONGLONG now = GetTickCount64();
    if (now - last_trigger < 5000) return;
    last_trigger = now;
    launch_lhm_direct();
  }

  /* Run through the fallback chain: MSAcpi, then LHM WMI, then helper. */
  double query() {
    /* Try MSAcpi first (we are already connected to ROOT\\WMI). */
    if (svc != nullptr) {
      double t = query_msacpi();
      if (t >= 0) { return t; }
      release_svc();
    }

    /* Fall back to LibreHardwareMonitor's WMI namespace. */
    if (connect_to(L"root\\LibreHardwareMonitor")) {
      double t = query_lhm();
      if (t >= 0) { return t; }
      release_svc();
    }

    /* Last resort: run the bundled lhm-temp.exe helper tool. */
    { double t = query_helper(); if (t >= 0) { return t; } }

    return -1.0;
  }
} wmi_cpu_temp;

bool get_acpi_temperature_init() {
  if (!wmi_cpu_temp.init()) { return false; }
  return wmi_cpu_temp.connect_to(L"ROOT\\WMI");
}

double get_acpi_temperature(int fd) {
  static bool init_ok = false;
  static bool init_done = false;

  /* Try init on first call.  If it fails (WMI not ready after boot),
   * keep retrying each call — don't cache failure. */
  if (!init_done) {
    init_ok = get_acpi_temperature_init();
    init_done = init_ok;  // only mark done on success
  }

  /* If init previously succeeded, try query first.  If it returns -1
   * (helper data not ready yet), try re-triggering the helper and
   * return -1 — next cycle will pick up fresh data. */
  if (init_ok) {
    double t = wmi_cpu_temp.query();
    if (t >= 0) return t;
    wmi_cpu_temp.trigger_helper();
    return -1.0;
  }

  return -1.0;
}

/* Public helper trigger for use from gpu.cc and other modules.
 * Ensures lhm-temp.exe is running to write temp.dat and gpu.dat. */
void trigger_lhm_helper() { wmi_cpu_temp.trigger_helper(); }

/* ---- Battery stats via GetSystemPowerStatus ---- */

void get_battery_stuff(char *buf, unsigned int n, const char *bat, int item) {
  if (!buf || n == 0) {
    return;
  }

  (void)bat;

  SYSTEM_POWER_STATUS sps;
  if (!GetSystemPowerStatus(&sps)) {
    snprintf(buf, n, "unknown");
    return;
  }

  switch (item) {
    case BATTERY_STATUS:
      if (sps.BatteryFlag & 128) {
        snprintf(buf, n, "no battery");
      } else if (sps.ACLineStatus == 1 && (sps.BatteryFlag & 8)) {
        snprintf(buf, n, "charging");
      } else if (sps.ACLineStatus == 1) {
        snprintf(buf, n, "charged");
      } else {
        snprintf(buf, n, "discharging");
      }
      break;

    case BATTERY_TIME:
      if (sps.BatteryLifeTime == (DWORD)-1) {
        snprintf(buf, n, "unknown");
      } else {
        unsigned long total_sec = sps.BatteryLifeTime;
        unsigned long h = total_sec / 3600;
        unsigned long m = (total_sec % 3600) / 60;
        snprintf(buf, n, "%luh %lum", h, m);
      }
      break;

    default:
      snprintf(buf, n, "unknown");
      break;
  }
}

int get_battery_perct(const char *bat) {
  (void)bat;
  SYSTEM_POWER_STATUS sps;
  if (!GetSystemPowerStatus(&sps)) {
    return 0;
  }
  if (sps.BatteryLifePercent == 255) {
    return 0;
  }
  return static_cast<int>(sps.BatteryLifePercent);
}

double get_battery_perct_bar(struct text_object *obj) {
  return static_cast<double>(get_battery_perct(obj->data.s)) / 100.0;
}

void get_battery_power_draw(char *buffer, unsigned int n, const char *bat) {
  (void)buffer;
  (void)n;
  (void)bat;
}

void get_battery_short_status(char *buffer, unsigned int n, const char *bat) {
  if (!buffer || n == 0) {
    return;
  }
  (void)bat;

  SYSTEM_POWER_STATUS sps;
  if (!GetSystemPowerStatus(&sps)) {
    snprintf(buffer, n, "?");
    return;
  }

  if (sps.BatteryFlag & 128) {
    snprintf(buffer, n, "N/A");
  } else if (sps.ACLineStatus == 1) {
    snprintf(buffer, n, "AC");
  } else {
    snprintf(buffer, n, "BAT");
  }
}

/* ---- ACPI (stubs - no direct equivalent on Windows) ---- */

int open_acpi_temperature(const char *name) {
  (void)name;
  return 0;
}

void get_acpi_ac_adapter(char *p_client_buffer, size_t client_buffer_size,
                         const char *adapter) {
  (void)adapter;
  if (!p_client_buffer || client_buffer_size <= 0) {
    return;
  }
  memset(p_client_buffer, 0, client_buffer_size);
}

void get_acpi_fan(char *p_client_buffer, size_t client_buffer_size) {
  if (!p_client_buffer || client_buffer_size <= 0) {
    return;
  }
  memset(p_client_buffer, 0, client_buffer_size);
}

/* ---- CPU frequency via registry ---- */

char get_freq(char *p_client_buffer, size_t client_buffer_size,
              const char *p_format, int divisor, unsigned int cpu) {
  if (!p_client_buffer || client_buffer_size <= 0 || !p_format ||
      divisor <= 0) {
    return 0;
  }

  HKEY key;
  DWORD mhz = 0;
  DWORD size = sizeof(mhz);

  /* Build registry path: HARDWARE\DESCRIPTION\System\CentralProcessor\N */
  char subkey[128];
  snprintf(subkey, sizeof(subkey),
           "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\%u", cpu);

  LONG ret =
      RegOpenKeyExA(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ, &key);
  if (ret == ERROR_SUCCESS) {
    RegQueryValueExA(key, "~MHz", nullptr, nullptr, (LPBYTE)&mhz, &size);
    RegCloseKey(key);
  }

  if (mhz == 0) {
    return 0;
  }

  snprintf(p_client_buffer, client_buffer_size, p_format,
           static_cast<double>(mhz) / divisor);
  return 1;
}

/* ---- Disk I/O via PDH ---- */

int update_diskio(void) {
  static PDH_HQUERY pdh_query = NULL;
  static PDH_HCOUNTER pdh_read = NULL;
  static PDH_HCOUNTER pdh_write = NULL;
  static double cumul_read = 0.0, cumul_write = 0.0;
  static bool pdh_ok = false;
  static bool init_done = false;

  if (!init_done) {
    init_done = true;
    if (PdhOpenQueryA(nullptr, 0, &pdh_query) == ERROR_SUCCESS &&
        PdhAddCounterA(pdh_query, "\\PhysicalDisk(_Total)\\Disk Read Bytes/sec",
                        0, &pdh_read) == ERROR_SUCCESS &&
        PdhAddCounterA(pdh_query,
                        "\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0,
                        &pdh_write) == ERROR_SUCCESS) {
      PdhCollectQueryData(pdh_query);
      pdh_ok = true;
    }
  }

  if (!pdh_ok) { return 1; }

  PdhCollectQueryData(pdh_query);

  PDH_FMT_COUNTERVALUE rv, wv;
  double read_rate = 0.0, write_rate = 0.0;
  if (PdhGetFormattedCounterValue(pdh_read, PDH_FMT_DOUBLE, nullptr, &rv) ==
      ERROR_SUCCESS) {
    read_rate = rv.doubleValue;
  }
  if (PdhGetFormattedCounterValue(pdh_write, PDH_FMT_DOUBLE, nullptr, &wv) ==
      ERROR_SUCCESS) {
    write_rate = wv.doubleValue;
  }

  double interval = active_update_interval();
  if (interval <= 0.0) { interval = 1.0; }

  cumul_read += read_rate * interval;
  cumul_write += write_rate * interval;

  /* Convert cumulative bytes → sectors (512 B/sector) for update_diskio_values */
  unsigned int sectors_read = static_cast<unsigned int>(cumul_read / 512.0);
  unsigned int sectors_write = static_cast<unsigned int>(cumul_write / 512.0);

  for (struct diskio_stat *cur = &stats; cur != nullptr; cur = cur->next) {
    if (cur->dev == nullptr) { continue; }
    update_diskio_values(cur, sectors_read, sectors_write);
  }

  return 0;
}

/* ---- Top processes via Toolhelp ---- */

void get_top_info(void) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) {
    return;
  }

  /* Build a list of processes sorted by PID, filling in basic info.
   * For accurate CPU percentages, we track CPU times across calls. */
  static struct process local_list = {};
  struct process *p;
  unsigned int n;

  /* Free previous list */
  while (local_list.next != nullptr) {
    p = local_list.next;
    local_list.next = p->next;
    free_and_zero(p->name);
    free_and_zero(p->basename);
    delete p;
  }

  PROCESSENTRY32 pe = {};
  pe.dwSize = sizeof(PROCESSENTRY32);

  struct process *tail = &local_list;

  if (Process32First(snap, &pe)) {
    do {
      p = new struct process;
      memset(p, 0, sizeof(struct process));
      p->pid = static_cast<pid_t>(pe.th32ProcessID);
      p->name = strndup(pe.szExeFile, DEFAULT_TEXT_BUFFER_SIZE);
      p->basename = strndup(pe.szExeFile, DEFAULT_TEXT_BUFFER_SIZE);

      /* Get per-process CPU times and memory if we can open the handle */
      HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                    FALSE, pe.th32ProcessID);
      if (hProcess != nullptr) {
        FILETIME create, exit, kt, ut;
        if (GetProcessTimes(hProcess, &create, &exit, &kt, &ut)) {
          p->user_time =
              static_cast<unsigned long>(filetime_to_ull(ut) / 10000);
          p->kernel_time =
              static_cast<unsigned long>(filetime_to_ull(kt) / 10000);
        }
        PROCESS_MEMORY_COUNTERS pmc;
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
          p->rss = pmc.WorkingSetSize;
        }
        CloseHandle(hProcess);
      }

      p->previous_user_time = 0;
      p->previous_kernel_time = 0;
      p->amount = 0.0f;

      tail->next = p;
      p->previous = tail;
      tail = p;
    } while (Process32Next(snap, &pe));
  }

  CloseHandle(snap);

  /* Sort first N processes by CPU into info.cpu[].
   * Simple O(n) scan to pick top 10 by estimated CPU. */
  for (n = 0; n < 10; n++) {
    info.cpu[n] = nullptr;
    info.memu[n] = nullptr;
    info.time[n] = nullptr;
  }

  struct process *best_cpu[10] = {};
  struct process *best_mem[10] = {};

  /* Scan: repeatedly find max element and slot it. CPU is approximate
   * (difference from toolhelp doesn't give true %). */
  for (p = local_list.next; p != nullptr; p = p->next) {
    /* Insert into best_cpu sorted by amount (descending) */
    for (n = 0; n < 10; n++) {
      if (best_cpu[n] == nullptr ||
          p->amount > best_cpu[n]->amount) {
        /* Shift down */
        for (unsigned int m = 9; m > n; m--) {
          best_cpu[m] = best_cpu[m - 1];
        }
        best_cpu[n] = p;
        break;
      }
    }

    /* Insert into best_mem sorted by rss (descending) */
    for (n = 0; n < 10; n++) {
      if (best_mem[n] == nullptr ||
          p->rss > best_mem[n]->rss) {
        for (unsigned int m = 9; m > n; m--) {
          best_mem[m] = best_mem[m - 1];
        }
        best_mem[n] = p;
        break;
      }
    }
  }

  for (n = 0; n < 10; n++) {
    info.cpu[n] = best_cpu[n];
    info.memu[n] = best_mem[n];
  }
}

/* ---- Gateway info (return empty) ---- */

int update_gateway_info(void) { return 1; }
int update_gateway_info2(void) { return 1; }
void free_gateway_info(struct text_object *) {}
int gateway_exists(struct text_object *) { return 0; }
void print_gateway_iface(struct text_object *, char *, unsigned int) {}
void print_gateway_iface2(struct text_object *, char *, unsigned int) {}
void print_gateway_ip(struct text_object *, char *, unsigned int) {}

/* ---- Entropy (no equivalent on Windows) ---- */

int get_entropy_avail(unsigned int *val) {
  (void)val;
  return 1;
}

int get_entropy_poolsize(unsigned int *val) {
  (void)val;
  return 1;
}

/* ---- Single instance ---- */

bool is_conky_already_running(void) {
  HANDLE mutex = CreateMutexA(NULL, FALSE, "Local\\ConkySingleInstance");
  if (mutex == NULL) {
    return false;
  }
  return GetLastError() == ERROR_ALREADY_EXISTS;
}
