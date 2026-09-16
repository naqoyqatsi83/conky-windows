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
#include <wlanapi.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "../../common.h"
#include "../../conky.h"
#include "../../logging.h"
#include "../proc.h"
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
  if (obj == nullptr || obj->data.s == nullptr || obj->data.s[0] == '\0') {
    return 0;
  }

  std::string path = obj->data.s;
  /* A bare drive letter ("D:" or "D") needs a trailing backslash --
   * GetFileAttributes on "D:" alone reports on the current directory of
   * that drive rather than the volume itself, which can exist (or not)
   * independently of whether the drive is actually mounted. */
  if (path.size() <= 2 && (path.size() == 1 || path[1] == ':')) {
    if (path.size() == 1) { path += ':'; }
    path += '\\';
  }

  DWORD attrs = GetFileAttributesA(path.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES &&
         (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
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
          /* ns->addr is a plain "struct sockaddr" (16 bytes) -- copying a
           * full sockaddr_in6 (28 bytes) here overflows it and corrupts
           * the adjacent v6addrs field. The real IPv6 address data lives
           * in ns->v6addrs (below); this just records the family. */
          memcpy(&ns->addr, sa, sizeof(struct sockaddr));
        }
#endif
      }

      /* ${addrs}: every IPv4 address on this adapter, comma-joined --
       * print_addrs() (src/data/network/net_stat.cc) expects this exact
       * "a, b, c, " shape (trailing ", ") and trims it before display. */
      ns->addrs[0] = '\0';
      for (auto *ua = p->FirstUnicastAddress; ua != nullptr; ua = ua->Next) {
        SOCKADDR *ua_sa = ua->Address.lpSockaddr;
        if (ua_sa->sa_family != AF_INET) { continue; }
        auto *sin = reinterpret_cast<sockaddr_in *>(ua_sa);
        char one[32];
        snprintf(one, sizeof(one), "%s, ", inet_ntoa(sin->sin_addr));
        size_t used = strlen(ns->addrs);
        if (used + 1 < sizeof(ns->addrs)) {
          strncat(ns->addrs, one, sizeof(ns->addrs) - used - 1);
        }
      }

#ifdef BUILD_IPV6
      /* ${v6addrs}: same idea as addrs above, but IPv6 -- walk the same
       * adapter's unicast list for AF_INET6 entries.
       *
       * v6addr nodes must be allocated with malloc()/calloc(), not new --
       * clear_net_stats() in net_stat.cc (shared with all platforms) frees
       * this list with free_and_zero(), and mixing new[]/delete with
       * malloc/free on the same allocation corrupts the heap (it compiled
       * and ran, but crashed later on an unrelated-looking garbage
       * pointer read once corrupted heap metadata got walked). */
      while (ns->v6addrs != nullptr) {
        struct v6addr *next = ns->v6addrs->next;
        free(ns->v6addrs);
        ns->v6addrs = next;
      }
      struct v6addr *v6_tail = nullptr;
      for (auto *ua = p->FirstUnicastAddress; ua != nullptr; ua = ua->Next) {
        SOCKADDR *ua_sa = ua->Address.lpSockaddr;
        if (ua_sa->sa_family != AF_INET6) { continue; }
        auto *sin6 = reinterpret_cast<sockaddr_in6 *>(ua_sa);

        auto *node = static_cast<struct v6addr *>(malloc(sizeof(struct v6addr)));
        node->addr = sin6->sin6_addr;
        node->netmask = ua->OnLinkPrefixLength;
        node->scope = IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)   ? 'L'
                      : IN6_IS_ADDR_SITELOCAL(&sin6->sin6_addr) ? 'S'
                                                                : 'G';
        node->next = nullptr;

        if (v6_tail != nullptr) {
          v6_tail->next = node;
        } else {
          ns->v6addrs = node;
        }
        v6_tail = node;
      }
#endif /* BUILD_IPV6 */
    }
  }

#ifdef BUILD_WLAN
  /* ${wireless_*}: populate the same ns->essid/channel/freq/bitrate/mode/
   * link_qual/link_qual_max/ap fields print_wireless_*() (net_stat.cc)
   * already reads on every platform -- via the Windows Native Wifi API
   * (wlanapi.h) instead of Linux wireless-tools ioctls. */
  {
    HANDLE wlan_handle = nullptr;
    DWORD wlan_negotiated_version = 0;
    if (WlanOpenHandle(2, nullptr, &wlan_negotiated_version, &wlan_handle) ==
        ERROR_SUCCESS) {
      PWLAN_INTERFACE_INFO_LIST if_list = nullptr;
      if (WlanEnumInterfaces(wlan_handle, nullptr, &if_list) ==
              ERROR_SUCCESS &&
          if_list != nullptr) {
        for (DWORD wi = 0; wi < if_list->dwNumberOfItems; ++wi) {
          const WLAN_INTERFACE_INFO &wlan_if = if_list->InterfaceInfo[wi];

          /* WlanEnumInterfaces has no friendly connection name (e.g.
           * "Wi-Fi"), only strInterfaceDescription (the hardware model,
           * e.g. "MediaTek Wi-Fi 7 MT7927...") -- not the same string
           * GetAdaptersAddresses()'s FriendlyName uses as the net_stat
           * key above. Match by GUID instead: format this interface's
           * GUID the same way Windows formats IP_ADAPTER_ADDRESSES::
           * AdapterName, and look for an adapter with that name. */
          wchar_t wguid[64];
          StringFromGUID2(wlan_if.InterfaceGuid, wguid,
                          sizeof(wguid) / sizeof(wguid[0]));
          char guid_str[128];
          WideCharToMultiByte(CP_UTF8, 0, wguid, -1, guid_str,
                              sizeof(guid_str), nullptr, nullptr);

          char ifname[256] = "";
          for (PIP_ADAPTER_ADDRESSES p = paa; p != nullptr; p = p->Next) {
            if (p->AdapterName != nullptr &&
                _stricmp(p->AdapterName, guid_str) == 0) {
              WideCharToMultiByte(CP_UTF8, 0, p->FriendlyName, -1, ifname,
                                  sizeof(ifname) - 1, nullptr, nullptr);
              break;
            }
          }
          if (ifname[0] == '\0') { continue; }

          struct net_stat *ns = get_net_stat(ifname, nullptr, nullptr);
          if (ns == nullptr) { continue; }

          /* Reset to "no wireless data" defaults every cycle; overwritten
           * below only if this interface is actually connected right
           * now (mirrors Linux's has_essid/essid_on branch in
           * src/data/os/linux.cc, which leaves these untouched/blank the
           * same way when a query comes back empty). */
          ns->essid[0] = '\0';
          ns->channel = 0;
          ns->freq[0] = '\0';
          ns->bitrate[0] = '\0';
          ns->mode[0] = '\0';
          ns->link_qual = 0;
          ns->link_qual_max = 0;
          ns->ap[0] = '\0';

          if (wlan_if.isState != wlan_interface_state_connected) { continue; }

          DWORD conn_attr_size = 0;
          PWLAN_CONNECTION_ATTRIBUTES conn_attr = nullptr;
          WLAN_OPCODE_VALUE_TYPE opcode_type;
          if (WlanQueryInterface(wlan_handle, &wlan_if.InterfaceGuid,
                                  wlan_intf_opcode_current_connection,
                                  nullptr, &conn_attr_size,
                                  reinterpret_cast<PVOID *>(&conn_attr),
                                  &opcode_type) != ERROR_SUCCESS ||
              conn_attr == nullptr) {
            continue;
          }

          const WLAN_ASSOCIATION_ATTRIBUTES &assoc =
              conn_attr->wlanAssociationAttributes;

          ULONG ssid_len = assoc.dot11Ssid.uSSIDLength;
          if (ssid_len > sizeof(ns->essid) - 1) {
            ssid_len = sizeof(ns->essid) - 1;
          }
          memcpy(ns->essid, assoc.dot11Ssid.ucSSID, ssid_len);
          ns->essid[ssid_len] = '\0';

          snprintf(ns->ap, sizeof(ns->ap), "%02X:%02X:%02X:%02X:%02X:%02X",
                  assoc.dot11Bssid[0], assoc.dot11Bssid[1],
                  assoc.dot11Bssid[2], assoc.dot11Bssid[3],
                  assoc.dot11Bssid[4], assoc.dot11Bssid[5]);

          /* wlanSignalQuality is already a 0-100 percentage (unlike
           * Linux's driver-specific qual/qual_max range), so link_qual
           * and link_qual_max together still produce the right percent
           * out of print_wireless_link_qual_perc()'s existing division. */
          ns->link_qual = static_cast<int>(assoc.wlanSignalQuality);
          ns->link_qual_max = 100;

          /* ulTxRate is in units of 100 kbps per the WLAN API docs. */
          snprintf(ns->bitrate, sizeof(ns->bitrate), "%.1f Mb/s",
                  assoc.ulTxRate / 1000.0);

          /* Windows' WLAN API doesn't expose an ad-hoc-vs-infrastructure
           * distinction the way iw_operation_mode does; virtually all
           * real-world Wi-Fi is infrastructure ("Managed" in
           * wireless-tools' naming), so use that as the one supported
           * value rather than leaving this blank. */
          snprintf(ns->mode, sizeof(ns->mode), "Managed");

          DWORD chan_size = 0;
          PVOID chan_data = nullptr;
          if (WlanQueryInterface(wlan_handle, &wlan_if.InterfaceGuid,
                                  wlan_intf_opcode_channel_number, nullptr,
                                  &chan_size, &chan_data,
                                  &opcode_type) == ERROR_SUCCESS &&
              chan_data != nullptr) {
            ns->channel =
                static_cast<int>(*reinterpret_cast<ULONG *>(chan_data));
            WlanFreeMemory(chan_data);
          }

          WlanFreeMemory(conn_attr);
        }
      }
      if (if_list != nullptr) { WlanFreeMemory(if_list); }
      WlanCloseHandle(wlan_handle, nullptr);
    }
  }
#endif /* BUILD_WLAN */

  return 0;
}

/* ---- Process-by-name lookup for if_running (see common.cc) ---- */

bool win_process_by_name_running(const char *name) {
  if (name == nullptr || name[0] == '\0') { return false; }

  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) { return false; }

  std::string want = name;
  bool want_has_exe = want.size() > 4 &&
                      _stricmp(want.c_str() + want.size() - 4, ".exe") == 0;

  PROCESSENTRY32 pe = {};
  pe.dwSize = sizeof(PROCESSENTRY32);
  bool found = false;

  if (Process32First(snap, &pe)) {
    do {
      std::string have = pe.szExeFile;
      /* Linux theme authors write process names without an extension
       * (e.g. "firefox"); Windows process names always end in ".exe".
       * Compare without the extension unless the theme's own argument
       * already included one. */
      if (!want_has_exe && have.size() > 4 &&
          _stricmp(have.c_str() + have.size() - 4, ".exe") == 0) {
        have.resize(have.size() - 4);
      }
      if (_stricmp(have.c_str(), want.c_str()) == 0) {
        found = true;
        break;
      }
    } while (Process32Next(snap, &pe));
  }

  CloseHandle(snap);
  return found;
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

int update_threads() {
  /* PROCESSENTRY32's cntThreads field is the thread count owned by that
   * process -- summing it across a process snapshot gives the system-wide
   * thread count without a separate TH32CS_SNAPTHREAD walk. */
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) {
    return 1;
  }

  unsigned short count = 0;
  PROCESSENTRY32 pe = {};
  pe.dwSize = sizeof(PROCESSENTRY32);

  if (Process32First(snap, &pe)) {
    do {
      count += static_cast<unsigned short>(pe.cntThreads);
    } while (Process32Next(snap, &pe));
  }

  CloseHandle(snap);
  info.threads = count;
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

  /* Run `schtasks /RUN /TN "ConkyTempHelper"` directly (no cmd.exe wrapper).
   * Returns true if the task exists and the run was accepted. Also returns
   * true when the task is already running — schtasks uses the default
   * IgnoreNew MultipleInstances policy, so no duplicate helper is spawned. */
  static bool run_schtasks_restart() {
    wchar_t args[] = L"schtasks.exe /RUN /TN \"ConkyTempHelper\"";
    HANDLE hNul = CreateFileW(L"NUL", GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si = {sizeof(si), 0};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hNul != INVALID_HANDLE_VALUE ? hNul : nullptr;
    si.hStdError = hNul != INVALID_HANDLE_VALUE ? hNul : nullptr;
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, args, nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (hNul != INVALID_HANDLE_VALUE) { CloseHandle(hNul); }
    if (!ok) { return false; }
    DWORD wait = WaitForSingleObject(pi.hProcess, 5000);
    DWORD exit = 0;
    if (wait == WAIT_OBJECT_0) { GetExitCodeProcess(pi.hProcess, &exit); }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return wait == WAIT_OBJECT_0 && exit == 0;
  }

  /* Restart the lhm-temp helper via the "ConkyTempHelper" scheduled task.
   *
   * Previous code tried to spawn lhm-temp.exe directly via CreateProcessW
   * and kill zombies via OpenProcess(PROCESS_TERMINATE).  This failed
   * because the scheduled task runs lhm-temp.exe with /RL HIGHEST (SYSTEM),
   * and a normal-user conky process cannot terminate SYSTEM processes —
   * OpenProcess silently returns NULL.  The spawn then created yet another
   * lhm-temp.exe as a normal user, and the cycle repeated, piling up
   * thousands of zombie processes that ate RAM and handles.
   *
   * The fix: use `schtasks /RUN` to restart the pre-existing scheduled
   * task.  schtasks /RUN works from non-elevated processes for tasks
   * created with /RL HIGHEST, and never spawns a duplicate of a running
   * helper (IgnoreNew policy).
   *
   * If the scheduled task is missing we do NOT spawn lhm-temp.exe directly:
   * a non-elevated conky cannot run it with the SYSTEM privileges it needs,
   * so direct spawns only pile up useless processes.  We back off and retry
   * the task later. */
  void trigger_helper() {
    static ULONGLONG last_trigger = 0;
    static int fail_count = 0;
    ULONGLONG now = GetTickCount64();

    /* Back off on repeated failures: 5s, 10s, 20s, ... up to ~5 min. */
    ULONGLONG delay_ms = 5000ULL << std::min(fail_count, 6);
    if (now - last_trigger < delay_ms) { return; }
    last_trigger = now;

    if (run_schtasks_restart()) {
      fail_count = 0;
      return;
    }

    if (fail_count == 0) {
      LOG_WARNING(
          "ConkyTempHelper scheduled task not found — cannot auto-restart the "
          "temperature helper. Install Conky via the installer, or create it "
          "with: schtasks /Create /SC ONLOGON /TN \"ConkyTempHelper\" /TR "
          "\"<path to lhm-temp.exe>\" /RL HIGHEST /F");
    }
    ++fail_count;
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

namespace {
/* System-wide CPU-time delta since the last call, in centiseconds --
 * independent of update_cpu_usage()'s own prev_cpu/filetime_to_ull
 * tracking above (different consumer, own state), mirroring how
 * calc_cpu_total() (src/data/os/linux.cc, the Linux reference this
 * mirrors) keeps its own independent previous_total static too. */
unsigned long long top_cpu_total_delta() {
  static unsigned long long previous_total = 0;
  FILETIME idle, kernel, user;
  if (!GetSystemTimes(&idle, &kernel, &user)) { return 0; }
  unsigned long long total =
      (filetime_to_ull(kernel) + filetime_to_ull(user)) / 10000;
  unsigned long long delta = (total >= previous_total) ? (total - previous_total) : 0;
  previous_total = total;
  return delta;
}
}  // namespace

/* Populates the shared first_process list (src/data/top.cc) -- NOT
 * info.cpu[]/info.memu[] directly. process_find_top() (top.cc), the only
 * caller, walks first_process itself right after calling this to build
 * those arrays via its own priority queues; an earlier version of this
 * function sorted into info.cpu[]/info.memu[] directly and never touched
 * first_process at all, so process_find_top()'s walk immediately
 * overwrote that work with the result of walking an empty list --
 * ${top}/${top_mem} always printed blank. */
void get_top_info(void) {
  unsigned long long total = top_cpu_total_delta();

  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) { return; }

  PROCESSENTRY32 pe = {};
  pe.dwSize = sizeof(PROCESSENTRY32);

  if (Process32First(snap, &pe)) {
    do {
      /* get_process() (top.cc) finds-or-creates the persistent entry for
       * this pid in first_process -- new entries start with
       * previous_user_time/previous_kernel_time = ULONG_MAX (sentinel). */
      struct process *p = get_process(static_cast<pid_t>(pe.th32ProcessID));
      if (p == nullptr) { continue; }

      p->time_stamp = g_time; /* mark alive; process_cleanup() purges stale ones */

      free_and_zero(p->name);
      free_and_zero(p->basename);
      p->name = strndup(pe.szExeFile, DEFAULT_TEXT_BUFFER_SIZE);
      p->basename = strndup(pe.szExeFile, DEFAULT_TEXT_BUFFER_SIZE);

      HANDLE hProcess = OpenProcess(
          PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE,
          pe.th32ProcessID);
      if (hProcess != nullptr) {
        FILETIME create, exit, kt, ut;
        if (GetProcessTimes(hProcess, &create, &exit, &kt, &ut)) {
          auto user_time =
              static_cast<unsigned long>(filetime_to_ull(ut) / 10000);
          auto kernel_time =
              static_cast<unsigned long>(filetime_to_ull(kt) / 10000);

          /* Same delta/sentinel handling as calc_cpu_total()'s
           * per-process counterpart in the Linux reference
           * (process_parse_stat(), src/data/os/linux.cc): store the
           * *delta* into user_time/kernel_time (what calc_cpu_each()
           * below reads), not the raw cumulative value. */
          if (p->previous_user_time == ULONG_MAX) {
            p->previous_user_time = user_time;
          }
          if (p->previous_kernel_time == ULONG_MAX) {
            p->previous_kernel_time = kernel_time;
          }
          if (p->previous_user_time > user_time) {
            p->previous_user_time = user_time;
          }
          if (p->previous_kernel_time > kernel_time) {
            p->previous_kernel_time = kernel_time;
          }

          p->user_time = user_time - p->previous_user_time;
          p->kernel_time = kernel_time - p->previous_kernel_time;
          p->previous_user_time = user_time;
          p->previous_kernel_time = kernel_time;
        }

        PROCESS_MEMORY_COUNTERS_EX pmc = {};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(
                hProcess, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc),
                sizeof(pmc))) {
          p->rss = pmc.WorkingSetSize;
          p->vsize = pmc.PagefileUsage;
        }
        CloseHandle(hProcess);
      }
    } while (Process32Next(snap, &pe));
  }
  CloseHandle(snap);

  /* Same formula as calc_cpu_each() (src/data/top.cc's Linux reference);
   * skips its optional top_cpu_separate core-count scaling for now. */
  for (struct process *p = first_process; p != nullptr; p = p->next) {
    p->amount = total > 0 ? 100.0f * static_cast<float>(p->user_time + p->kernel_time) /
                                static_cast<float>(total)
                          : 0.0f;
  }
}

/* ---- Gateway info via GetBestInterface + GetAdaptersAddresses ---- */

namespace {
struct gateway_info_state {
  bool valid = false;
  char iface[256] = {0};
  char ip[64] = {0};
};
gateway_info_state g_gateway_info;
}  // namespace

int update_gateway_info(void) {
  g_gateway_info.valid = false;

  /* GetBestInterface(0, ...) finds the interface used to reach 0.0.0.0 --
   * i.e. the default route's interface, the same thing /proc/net/route's
   * "destination 0" entry identifies on Linux. */
  DWORD best_if = 0;
  if (GetBestInterface(0, &best_if) != NO_ERROR) { return 1; }

  ULONG bufLen = 0;
  GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, nullptr,
                       &bufLen);
  if (bufLen == 0) { return 1; }

  std::vector<char> buf(bufLen);
  auto *paa = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
  if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, paa,
                           &bufLen) != NO_ERROR) {
    return 1;
  }

  for (PIP_ADAPTER_ADDRESSES p = paa; p != nullptr; p = p->Next) {
    if (p->IfIndex != best_if) { continue; }

    WideCharToMultiByte(CP_UTF8, 0, p->FriendlyName, -1, g_gateway_info.iface,
                        sizeof(g_gateway_info.iface) - 1, nullptr, nullptr);

    if (p->FirstGatewayAddress != nullptr) {
      auto *sin = reinterpret_cast<sockaddr_in *>(
          p->FirstGatewayAddress->Address.lpSockaddr);
      snprintf(g_gateway_info.ip, sizeof(g_gateway_info.ip), "%s",
               inet_ntoa(sin->sin_addr));
    }

    g_gateway_info.valid = g_gateway_info.iface[0] != '\0';
    break;
  }

  return 0;
}

/* update_gateway_info2/print_gateway_iface2 back ${iface}, which lists
 * *every* routed interface, not just the default gateway's -- left
 * unimplemented for now (${gw_iface}/${gw_ip}/${if_gw} above cover the
 * common case); returning "no data" rather than a wrong/partial list. */
int update_gateway_info2(void) { return 1; }
void print_gateway_iface2(struct text_object *, char *, unsigned int) {}

void free_gateway_info(struct text_object *) { g_gateway_info.valid = false; }

int gateway_exists(struct text_object *) { return g_gateway_info.valid ? 1 : 0; }

void print_gateway_iface(struct text_object *, char *p,
                         unsigned int p_max_size) {
  snprintf(p, p_max_size, "%s", g_gateway_info.valid ? g_gateway_info.iface : "");
}

void print_gateway_ip(struct text_object *, char *p, unsigned int p_max_size) {
  snprintf(p, p_max_size, "%s", g_gateway_info.valid ? g_gateway_info.ip : "");
}

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

/* ---- pid_* family: the subset with a clean Windows equivalent ----
 *
 * The Linux implementations (src/data/proc.cc) all read /proc/<pid>/...
 * text files; PROCDIR is hardcoded to "/proc" with no Windows branch, so
 * unlike most of this file these aren't "extend an existing platform
 * guard" fixes -- proc.cc's versions of the functions below are wrapped
 * in #ifndef _WIN32 and these replace them. The other ~29 pid_* objects
 * (uid/gid/environ/cwd/stdin/stdout/stderr/openfiles/...) need admin
 * rights and undocumented internals to read another process's security
 * context or open handles on Windows and aren't attempted here. */

namespace {
/* Every pid_* object's argument is itself a small evaluated text-object
 * chain (obj->sub), not a plain string -- this mirrors proc.cc's own
 * pattern of calling generate_text_internal() before parsing the PID. */
HANDLE open_pid_from_obj(struct text_object *obj, DWORD access) {
  std::unique_ptr<char[]> buf(new char[max_user_text.get(*state)]);
  generate_text_internal(buf.get(), max_user_text.get(*state), *obj->sub);

  char *end = nullptr;
  long pid_value = strtol(buf.get(), &end, 10);
  if (end == buf.get() || pid_value <= 0) { return nullptr; }

  return OpenProcess(access, FALSE, static_cast<DWORD>(pid_value));
}
}  // namespace

void print_pid_exe(struct text_object *obj, char *p, unsigned int p_max_size) {
  HANDLE h = open_pid_from_obj(
      obj, PROCESS_QUERY_LIMITED_INFORMATION);
  if (h == nullptr) {
    if (p_max_size > 0) p[0] = '\0';
    return;
  }
  DWORD len = p_max_size;
  if (!QueryFullProcessImageNameA(h, 0, p, &len) && p_max_size > 0) {
    p[0] = '\0';
  }
  CloseHandle(h);
}

void print_pid_priority(struct text_object *obj, char *p,
                        unsigned int p_max_size) {
  HANDLE h = open_pid_from_obj(obj, PROCESS_QUERY_LIMITED_INFORMATION);
  if (h == nullptr) {
    if (p_max_size > 0) p[0] = '\0';
    return;
  }
  DWORD cls = GetPriorityClass(h);
  CloseHandle(h);
  snprintf(p, p_max_size, "%lu", static_cast<unsigned long>(cls));
}

void print_pid_state(struct text_object *obj, char *p,
                     unsigned int p_max_size) {
  /* Windows doesn't expose Linux-style Running/Sleeping/Zombie states;
   * this reports the one distinction actually available without deeper
   * (and much less reliable) undocumented queries -- whether the PID
   * still refers to a live process at all. */
  HANDLE h = open_pid_from_obj(obj, PROCESS_QUERY_LIMITED_INFORMATION);
  if (h == nullptr) {
    snprintf(p, p_max_size, "%s", "Not running");
    return;
  }
  DWORD exit_code = 0;
  bool alive = GetExitCodeProcess(h, &exit_code) && exit_code == STILL_ACTIVE;
  CloseHandle(h);
  snprintf(p, p_max_size, "%s", alive ? "Running" : "Not running");
}

void print_pid_threads(struct text_object *obj, char *p,
                       unsigned int p_max_size) {
  std::unique_ptr<char[]> buf(new char[max_user_text.get(*state)]);
  generate_text_internal(buf.get(), max_user_text.get(*state), *obj->sub);
  char *end = nullptr;
  long pid_value = strtol(buf.get(), &end, 10);
  if (end == buf.get() || pid_value <= 0) {
    if (p_max_size > 0) p[0] = '\0';
    return;
  }

  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) {
    if (p_max_size > 0) p[0] = '\0';
    return;
  }
  PROCESSENTRY32 pe = {};
  pe.dwSize = sizeof(PROCESSENTRY32);
  DWORD count = 0;
  bool found = false;
  if (Process32First(snap, &pe)) {
    do {
      if (static_cast<long>(pe.th32ProcessID) == pid_value) {
        count = pe.cntThreads;
        found = true;
        break;
      }
    } while (Process32Next(snap, &pe));
  }
  CloseHandle(snap);
  if (found) {
    snprintf(p, p_max_size, "%lu", static_cast<unsigned long>(count));
  } else if (p_max_size > 0) {
    p[0] = '\0';
  }
}

namespace {
void print_pid_vm(struct text_object *obj, char *p, unsigned int p_max_size,
                  bool peak) {
  HANDLE h = open_pid_from_obj(
      obj, PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ);
  if (h == nullptr) {
    if (p_max_size > 0) p[0] = '\0';
    return;
  }
  PROCESS_MEMORY_COUNTERS_EX pmc = {};
  pmc.cb = sizeof(pmc);
  bool ok = GetProcessMemoryInfo(
      h, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc));
  CloseHandle(h);
  if (!ok) {
    if (p_max_size > 0) p[0] = '\0';
    return;
  }
  /* VmSize on Linux is total committed virtual memory; PagefileUsage is
   * the closest Windows analog (commit charge), not WorkingSetSize
   * (that's VmRSS's counterpart instead). */
  SIZE_T bytes = peak ? pmc.PeakPagefileUsage : pmc.PagefileUsage;
  snprintf(p, p_max_size, "%llu kB",
           static_cast<unsigned long long>(bytes / 1024));
}
}  // namespace

void print_pid_vmpeak(struct text_object *obj, char *p,
                      unsigned int p_max_size) {
  print_pid_vm(obj, p, p_max_size, true);
}

void print_pid_vmsize(struct text_object *obj, char *p,
                      unsigned int p_max_size) {
  print_pid_vm(obj, p, p_max_size, false);
}

void print_pid_vmrss(struct text_object *obj, char *p,
                     unsigned int p_max_size) {
  HANDLE h = open_pid_from_obj(
      obj, PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ);
  if (h == nullptr) {
    if (p_max_size > 0) p[0] = '\0';
    return;
  }
  PROCESS_MEMORY_COUNTERS pmc = {};
  pmc.cb = sizeof(pmc);
  bool ok = GetProcessMemoryInfo(h, &pmc, sizeof(pmc));
  CloseHandle(h);
  if (!ok) {
    if (p_max_size > 0) p[0] = '\0';
    return;
  }
  snprintf(p, p_max_size, "%llu kB",
           static_cast<unsigned long long>(pmc.WorkingSetSize / 1024));
}
