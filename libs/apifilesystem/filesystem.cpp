/*

 MIT License
 
 Copyright © 2020-2026 Samuel Venable
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 
*/

#include <apifilesystem/filesystem.hpp>
#if defined(__apifilesystem_supported__)
#include <apifilesystem/ghc/filesystem.hpp>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <vector>
#include <random>
#include <thread>
#include <set>

#include <climits>
#include <cstdlib>
#include <cstring>
#if (defined(_WIN32) || defined(_WIN64))
#include <cwchar>
#elif (defined(__APPLE__) && defined(__MACH__))
#include <cstdint>
#endif

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#if (defined(_WIN32) || defined(_WIN64)) 
#include <windows.h>
#include <fileapi.h>
#include <shlobj.h>
#include <share.h>
#include <io.h>
#else
#if (defined(__APPLE__) && defined(__MACH__))
#include <sysdir.h>
#include <mach-o/dyld.h>
#elif ((defined(__FreeBSD__) || defined(__FreeBSD_kernel__)) || defined(__DragonFly__) || defined(__NetBSD__) || defined(__OpenBSD__))
#include <sys/sysctl.h>
#if ((defined(__FreeBSD__) || defined(__FreeBSD_kernel__)) || defined(__DragonFly__) || defined(__OpenBSD__))
#if defined(__OpenBSD__)
#include <kvm.h>
#endif
#include <sys/user.h>
#endif
#endif
#include <unistd.h>
#endif

using std::string;
#if (defined(_WIN32) || defined(_WIN64))
using std::wstring;
#elif (defined(__APPLE__) && defined(__MACH__))
using std::uint32_t;
#endif
using std::stringstream;
using std::random_device;
using std::error_code;
using std::uintmax_t;
using std::localtime;
using std::to_string;
using std::mt19937;
using std::shuffle;
using std::getline;
using std::reverse;
using std::thread;
using std::vector;
using std::size_t;
using std::sort;
using std::set;

namespace ngs::fs {

  namespace {

    void message_pump() {
      #if (defined(_WIN32) || defined(_WIN64)) 
      MSG msg; while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
      #endif
    }

    #if (defined(_WIN32) || defined(_WIN64)) 
    wstring widen(string str) {
      if (str.empty()) return L"";
      size_t wchar_count = str.size() + 1;
      vector<wchar_t> buf(wchar_count);
      wchar_count = (size_t)MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, buf.data(), (int)wchar_count);
      if (!wchar_count) return L"";
      return wstring { buf.data(), wchar_count };
    }

    string narrow(wstring wstr) {
      if (wstr.empty()) return "";
      int nbytes = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), nullptr, 0, nullptr, nullptr);
      if (!nbytes) return "";
      vector<char> buf((size_t)nbytes);
      nbytes = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), buf.data(), nbytes, nullptr, nullptr);
      if (!nbytes) return "";
      return string { buf.data(), (size_t)nbytes };
    }

    wchar_t *_wrealpath(const wchar_t *path, wchar_t *resolved_path) {
      wstring result;
      wchar_t buf[MAX_PATH];
      wchar_t *ptr = (((wchar_t *)resolved_path) ? ((wchar_t *)resolved_path) : ((wchar_t *)buf));
      HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
      if (hFile != INVALID_HANDLE_VALUE) {
        unsigned long len = GetFinalPathNameByHandleW(hFile, ptr, MAX_PATH, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (len && len <= MAX_PATH - 1) {
          result = ptr;
          if (!result.substr(0, 8).compare(L"\\\\?\\UNC\\")) {
            result = L"\\" + result.substr(7);
          } else if (!result.substr(0, 4).compare(L"\\\\?\\")) {
            result = result.substr(4);
          }
        }
        CloseHandle(hFile);
      }
      if (!result.empty()) {
        if (!resolved_path) {
          return _wcsdup(result.c_str());
        } else {
          wcsncpy_s(ptr, MAX_PATH, result.c_str(), _TRUNCATE);
          return (wchar_t *)ptr;
        }
      }
      return nullptr;
    }
    #endif

    bool is_digit(char byte) {
      return (byte >= '0' && byte <= '9');
    }

    vector<string> directory_contents;
    unsigned directory_contents_index = 0;
    unsigned directory_contents_order = DC_ATOZ;
    unsigned directory_contents_cntfiles = 1;
    unsigned directory_contents_maxfiles = 0;
    bool directory_contents_completion_async = false;
    bool directory_contents_completion_status = false;

    time_t file_datetime_helper(string fname, int timestamp) {
      int result = -1;
      #if (defined(_WIN32) || defined(_WIN64))
      wstring wfname = widen(fname);
      struct _stat info; 
      result = _wstat(wfname.c_str(), &info);
      #else
      struct stat info; 
      result = stat(fname.c_str(), &info);
      #endif
      if (result == -1) return 0;
      time_t time = 0;
      if (timestamp == 0) time = info.st_atime;
      if (timestamp == 1) time = info.st_mtime;
      if (timestamp == 2) time = info.st_ctime;
      return time;
    }

    int file_datetime(string fname, int timestamp, int measurement) {
      int result = -1;
      time_t time = file_datetime_helper(fname, timestamp);
      #if (defined(_WIN32) || defined(_WIN64))
      struct tm timeinfo;
      if (localtime_s(&timeinfo, &time)) return -1;
      switch (measurement) {
        case  0: return timeinfo.tm_year + 1900;
        case  1: return timeinfo.tm_mon  + 1;
        case  2: return timeinfo.tm_mday;
        case  3: return timeinfo.tm_hour;
        case  4: return timeinfo.tm_min;
        case  5: return timeinfo.tm_sec;
        default: return result;
      }
      #else
      struct tm *timeinfo = localtime(&time);
      switch (measurement) {
        case  0: return timeinfo->tm_year + 1900;
        case  1: return timeinfo->tm_mon  + 1;
        case  2: return timeinfo->tm_mday;
        case  3: return timeinfo->tm_hour;
        case  4: return timeinfo->tm_min;
        case  5: return timeinfo->tm_sec;
        default: return result;
      }
      #endif
      return result;
    }

    int file_bin_datetime(int fd, int timestamp, int measurement) {
      int result = -1;
      #if (defined(_WIN32) || defined(_WIN64))
      struct _stat info; 
      result = _fstat(fd, &info);
      #else
      struct stat info; 
      result = fstat(fd, &info);
      #endif
      time_t time = 0; 
      if (timestamp == 0) time = info.st_atime;
      if (timestamp == 1) time = info.st_mtime;
      if (timestamp == 2) time = info.st_ctime;
      if (result == -1) return result;
      #if (defined(_WIN32) || defined(_WIN64))
      struct tm timeinfo;
      if (localtime_s(&timeinfo, &time)) return -1;
      switch (measurement) {
        case  0: return timeinfo.tm_year + 1900;
        case  1: return timeinfo.tm_mon  + 1;
        case  2: return timeinfo.tm_mday;
        case  3: return timeinfo.tm_hour;
        case  4: return timeinfo.tm_min;
        case  5: return timeinfo.tm_sec;
        default: return result;
      }
      #else
      struct tm *timeinfo = localtime(&time);
      switch (measurement) {
        case  0: return timeinfo->tm_year + 1900;
        case  1: return timeinfo->tm_mon  + 1;
        case  2: return timeinfo->tm_mday;
        case  3: return timeinfo->tm_hour;
        case  4: return timeinfo->tm_min;
        case  5: return timeinfo->tm_sec;
        default: return result;
      }
      #endif
      return result;
    }
    
    string string_replace_all(string str, string substr, string nstr) {
      size_t pos = 0;
      while ((pos = str.find(substr, pos)) != string::npos) {
        message_pump();
        str.replace(pos, substr.length(), nstr);
        pos += nstr.length();
      }
      return str;
    }

    vector<string> string_split(string str, char delimiter) {
      vector<string> vec;
      stringstream sstr(str);
      string tmp;
      while (getline(sstr, tmp, delimiter)) {
        message_pump();
        vec.push_back(tmp);
      }
      return vec;
    }

    string filename_path(string fname) {
      #if (defined(_WIN32) || defined(_WIN64))
      size_t fp = fname.find_last_of("\\/");
      #else
      size_t fp = fname.find_last_of("/");
      #endif
      if (fp == string::npos) return fname;
      return fname.substr(0, fp + 1);
    }

    string filename_name(string fname) {
      #if (defined(_WIN32) || defined(_WIN64))
      size_t fp = fname.find_last_of("\\/");
      #else
      size_t fp = fname.find_last_of("/");
      #endif
      if (fp == string::npos) return fname;
      return fname.substr(fp + 1);
    }

    string filename_ext(string fname) {
      fname = filename_name(fname);
      size_t fp = fname.find_last_of(".");
      if (fp == string::npos) return "";
      return fname.substr(fp);
    }

    string expand_without_trailing_slash(string dname) {
      error_code ec;
      dname = environment_expand_variables(dname);
      ghc::filesystem::path p = ghc::filesystem::path(dname);
      p = ghc::filesystem::absolute(p, ec);
      if (ec.value() != 0) return "";
      dname = p.string();
      #if (defined(_WIN32) || defined(_WIN64))
      while (!dname.empty() && ((dname.back() == '\\' || dname.back() == '/') && 
        (p.root_name().string() + "\\" != dname && p.root_name().string() + "/" != dname))) {
        message_pump(); p = ghc::filesystem::path(dname); dname.pop_back();
      }
      #else
      while (!dname.empty() && dname.back() == '/' && string("/") != dname) {
        dname.pop_back();
      }
      #endif
      return dname;
    }

    string expand_with_trailing_slash(string dname) {
      dname = expand_without_trailing_slash(dname);
      #if (defined(_WIN32) || defined(_WIN64))
      if (!dname.empty() && dname.back() != '\\') dname += "\\";
      #else
      if (!dname.empty() && dname.back() != '/') dname += "/";
      #endif
      return dname;
    }

    struct file_bin_hardlinks_struct {
      vector<string> x;
      vector<string> y;
      bool recursive;
      unsigned i;
      unsigned j;
      #if (defined(_WIN32) || defined(_WIN64))
      BY_HANDLE_FILE_INFORMATION info;
      #else
      struct stat info;
      #endif
    };

    vector<string> file_bin_hardlinks_result;
    void file_bin_hardlinks_helper(file_bin_hardlinks_struct *s) {
      #if (defined(_WIN32) || defined(_WIN64))
      if (file_bin_hardlinks_result.size() >= s->info.nNumberOfLinks) return;
      #else
      if (file_bin_hardlinks_result.size() >= s->info.st_nlink) return;
      #endif
      if (s->i < s->x.size()) {
        error_code ec; if (!directory_exists(s->x[s->i])) return;
        s->x[s->i] = expand_without_trailing_slash(s->x[s->i]);
        const ghc::filesystem::path path = ghc::filesystem::path(s->x[s->i]);
        if (directory_exists(s->x[s->i]) || path.root_name().string() + "\\" == path.string()) {
          ghc::filesystem::directory_iterator end_itr;
          for (ghc::filesystem::directory_iterator dir_ite(path, ec); dir_ite != end_itr; dir_ite.increment(ec)) {
            message_pump(); if (ec.value() != 0) { break; }
            ghc::filesystem::path file_path = ghc::filesystem::path(filename_absolute(dir_ite->path().string()));
            #if (defined(_WIN32) || defined(_WIN64))
            int fd = -1;
            BY_HANDLE_FILE_INFORMATION info;
            if (file_exists(file_path.string())) {
              // printf("%s\n", file_path.string().c_str());
              if (!_wsopen_s(&fd, file_path.wstring().c_str(), _O_RDONLY, _SH_DENYNO, _S_IREAD)) {
                bool success = GetFileInformationByHandle((HANDLE)_get_osfhandle(fd), &info);
                bool matches = (info.ftLastWriteTime.dwLowDateTime == s->info.ftLastWriteTime.dwLowDateTime && 
                  info.ftLastWriteTime.dwHighDateTime == s->info.ftLastWriteTime.dwHighDateTime && 
                  info.nFileSizeHigh == s->info.nFileSizeHigh && info.nFileSizeLow == s->info.nFileSizeLow &&
                  info.nFileIndexHigh == s->info.nFileIndexHigh && info.nFileIndexLow == s->info.nFileIndexLow &&
                  info.dwVolumeSerialNumber == s->info.dwVolumeSerialNumber);
                if (matches && success) {
                  file_bin_hardlinks_result.push_back(file_path.string());
                  if (file_bin_hardlinks_result.size() >= info.nNumberOfLinks) {
                    s->info.nNumberOfLinks = info.nNumberOfLinks; s->x.clear();
                    _close(fd);
                    return;
                  }
                }
                _close(fd);
              }
            }
            #else
            struct stat info; 
            if (file_exists(file_path.string())) {
              // printf("%s\n", file_path.string().c_str());
              if (!stat(file_path.string().c_str(), &info)) {
                if (info.st_dev == s->info.st_dev && info.st_ino == s->info.st_ino && 
                  info.st_size == s->info.st_size && info.st_mtime == s->info.st_mtime) {
                  file_bin_hardlinks_result.push_back(file_path.string());
                  if (file_bin_hardlinks_result.size() >= info.st_nlink) {
                    s->info.st_nlink = info.st_nlink; s->x.clear();
                    return;
                  }
                }
              }
            }
            #endif
            if (s->recursive && directory_exists(file_path.string())) {
              // printf("%s\n", file_path.string().c_str());
              s->x.push_back(file_path.string());
              s->i++; file_bin_hardlinks_helper(s);
            }
          }
        }
      }
      while (s->j < s->y.size() && directory_exists(s->y[s->j])) {
        message_pump(); s->x.clear(); s->x.push_back(s->y[s->j]);
        s->j++; file_bin_hardlinks_helper(s);
      }
    }

    string directory_get_special_path(int dtype) {
      string result;
      #if (defined(_WIN32) || defined(_WIN64))
      wchar_t *ptr = nullptr;
      KNOWNFOLDERID fid;
      switch (dtype) {
        case  0: { fid = FOLDERID_Desktop;   break; }
        case  1: { fid = FOLDERID_Documents; break; }
        case  2: { fid = FOLDERID_Downloads; break; }
        case  3: { fid = FOLDERID_Music;     break; }
        case  4: { fid = FOLDERID_Pictures;  break; }
        case  5: { fid = FOLDERID_Videos;    break; }
        case  6: { fid = FOLDERID_Public;    break; }
        default: { fid = FOLDERID_Desktop;   break; }
      }
      if (SUCCEEDED(SHGetKnownFolderPath(fid, KF_FLAG_CREATE | KF_FLAG_DONT_UNEXPAND, nullptr, &ptr))) {
        result = narrow(ptr);
        if (!result.empty() && result.back() != '\\') {
          result.push_back('\\');
        }
      }
      CoTaskMemFree(ptr); 
      #elif (defined(__APPLE__) && defined(__MACH__))
      char buf[PATH_MAX];
      sysdir_search_path_directory_t fid;
      sysdir_search_path_enumeration_state state;
      switch (dtype) {
        case  0: { fid = SYSDIR_DIRECTORY_DESKTOP;       break; }
        case  1: { fid = SYSDIR_DIRECTORY_DOCUMENT;      break; }
        case  2: { fid = SYSDIR_DIRECTORY_DOWNLOADS;     break; }
        case  3: { fid = SYSDIR_DIRECTORY_MUSIC;         break; }
        case  4: { fid = SYSDIR_DIRECTORY_PICTURES;      break; }
        case  5: { fid = SYSDIR_DIRECTORY_MOVIES;        break; }
        case  6: { fid = SYSDIR_DIRECTORY_SHARED_PUBLIC; break; }
        default: { fid = SYSDIR_DIRECTORY_DESKTOP;       break; }
      }
      state = sysdir_start_search_path_enumeration(fid, SYSDIR_DOMAIN_MASK_USER);
      while ((state = sysdir_get_next_search_path_enumeration(state, buf))) {
        if (buf[0] == '~') {
          result = buf;
          string home = environment_get_variable("HOME");
          if (!home.empty()) {
            result.replace(0, 1, home);
            if (!result.empty() && result.back() != '/') {
              result.push_back('/');
            }
          } else {
            result.clear();
          }
          break;
        }
      }
      #elif !defined(__ANDROID__)
      string fid;
      switch (dtype) {
        case  0: { fid = "XDG_DESKTOP_DIR=";     break; }
        case  1: { fid = "XDG_DOCUMENTS_DIR=";   break; }
        case  2: { fid = "XDG_DOWNLOAD_DIR=";    break; }
        case  3: { fid = "XDG_MUSIC_DIR=";       break; }
        case  4: { fid = "XDG_PICTURES_DIR=";    break; }
        case  5: { fid = "XDG_VIDEOS_DIR=";      break; }
        case  6: { fid = "XDG_PUBLICSHARE_DIR="; break; }
        default: { fid = "XDG_DESKTOP_DIR=";     break; }
      }
      string conf = environment_get_variable("HOME") + "/.config/user-dirs.dirs";
      if (file_exists(conf)) {
        int dirs = file_text_open_read(conf);
        if (dirs != -1) {
          while (!file_text_eof(dirs)) {
            string line = file_text_read_string(dirs);
            file_text_readln(dirs);
            size_t pos = line.find(fid, 0);
            if (pos != string::npos) {
              FILE *fp = popen(("echo " + line.substr(pos + fid.length())).c_str(), "r");
              if (fp) {
                char buf[PATH_MAX];
                if (fgets(buf, sizeof(buf), fp)) {
                  string str = buf;
                  size_t pos = str.find("\n", strlen(buf) - 1);
                  if (pos != string::npos) {
                    str.replace(pos, 1, "");
                  }
                  if (!directory_exists(str)) {
                    directory_create(str);
                  }
                  result = str;
                  if (!result.empty() && result.back() != '/') {
                    result.push_back('/');
                  }
                }
                pclose(fp);
              }
            }
          }
          file_text_close(dirs);
        }
      }
      #endif
      return expand_with_trailing_slash(result);
    }

  } // anonymous namespace

  string directory_get_current_working() {
    error_code ec;
    string result = expand_with_trailing_slash(ghc::filesystem::current_path(ec).string());
    return (ec.value() == 0) ? result : "";
  }

  bool directory_set_current_working(string dname) {
    error_code ec;
    dname = expand_without_trailing_slash(dname);
    const ghc::filesystem::path path = ghc::filesystem::path(dname);
    ghc::filesystem::current_path(path, ec);
    return (ec.value() == 0);
  }

  string directory_get_temporary_path() {
    error_code ec;
    string result = expand_with_trailing_slash(ghc::filesystem::temp_directory_path(ec).string());
    return (ec.value() == 0) ? result : "";
  }

  string directory_get_desktop_path() {
    return directory_get_special_path(0);
  }

  string directory_get_documents_path() {
    return directory_get_special_path(1);
  }

  string directory_get_downloads_path() {
    return directory_get_special_path(2);
  }

  string directory_get_music_path() {
    return directory_get_special_path(3);
  }

  string directory_get_pictures_path() {
    return directory_get_special_path(4);
  }

  string directory_get_videos_path() {
    return directory_get_special_path(5);
  }

  string directory_get_public_path() {
    return directory_get_special_path(6);
  }

  string executable_get_pathname() {
    string path;
    #if (defined(_WIN32) || defined(_WIN64))
    wchar_t buffer[MAX_PATH];
    if (GetModuleFileNameW(nullptr, buffer, sizeof(buffer))) {
      wchar_t exe[MAX_PATH];
      if (_wrealpath(buffer, exe)) {
        path = narrow(exe);
      }
    }
    #elif (defined(__APPLE__) && defined(__MACH__))
    char buffer[PATH_MAX];
    uint32_t size = sizeof(buffer);
    if (!_NSGetExecutablePath(buffer, &size)) {
      char exe[PATH_MAX];
      if (realpath(buffer, exe)) {
        path = exe;
      }
    }
    #elif (defined(__linux__) || defined(__ANDROID__))
    char exe[PATH_MAX];
    if (realpath("/proc/self/exe", exe)) {
      path = exe;
    }
    #elif ((defined(__FreeBSD__) || defined(__FreeBSD_kernel__)) || defined(__DragonFly__))
    int mib[4]; 
    size_t len = 0;
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PATHNAME;
    mib[3] = -1;
    if (!sysctl(mib, 4, nullptr, &len, nullptr, 0)) {
      string strbuff;
      strbuff.resize(len, '\0');
      char *buffer = strbuff.data();
      if (!sysctl(mib, 4, buffer, &len, nullptr, 0)) {
        char exe[PATH_MAX];
        if (realpath(buffer, exe)) {
          path = exe;
        }
      }
    }
    #elif defined(__NetBSD__)
    int mib[4]; 
    size_t len = 0;
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC_ARGS;
    mib[2] = -1;
    mib[3] = KERN_PROC_PATHNAME;
    if (!sysctl(mib, 4, nullptr, &len, nullptr, 0)) {
      string strbuff;
      strbuff.resize(len, '\0');
      char *buffer = strbuff.data();
      if (!sysctl(mib, 4, buffer, &len, nullptr, 0)) {
        char exe[PATH_MAX];
        if (realpath(buffer, exe)) {
          path = exe;
        }
      }
    }
    #elif defined(__OpenBSD__)
    auto verify_exe = [](string exe) {
      int cntp = 0;
      string res;
      kvm_t *kd = nullptr;
      kinfo_file *kif = nullptr;
      bool error1 = false, error2 = false;
      kd = kvm_openfiles(nullptr, nullptr, nullptr, KVM_NO_FILES, nullptr);
      if (kd) {
        if ((kif = kvm_getfiles(kd, KERN_FILE_BYPID, getpid(), sizeof(struct kinfo_file), &cntp))) {
          for (int i = 0; i < cntp && kif[i].fd_fd < 0; i++) {
            if (kif[i].fd_fd == KERN_FILE_TEXT) {
              fallback:
              struct stat st;
              char buffer[PATH_MAX];
              if (!stat(exe.c_str(), &st) && (st.st_mode & S_IXUSR) &&
                S_ISREG(st.st_mode) && realpath(exe.c_str(), buffer) &&
                st.st_dev == (dev_t)kif[i].va_fsid && st.st_ino == (ino_t)kif[i].va_fileid) {
                res = buffer;
              }
              if (res.empty() && !error1) {
                error1 = true;
                size_t last_slash_pos = exe.find_last_of("/");
                if (last_slash_pos != string::npos) {
                  exe = exe.substr(0, last_slash_pos + 1) + kif[i].p_comm;
                  goto fallback;
                }
              }
              if (res.empty() && !error2) {
                error2 = true;
                size_t last_slash_pos = exe.find_last_of("/");
                if (last_slash_pos != string::npos) {
                  const char *progname = getprogname();
                  if (progname) {
                    exe = exe.substr(0, last_slash_pos + 1) + progname;
                    goto fallback;
                  }
                }
              }
              break;
            }
          }
        }
        kvm_close(kd);
      }
      return res;
    };
    int cntp = 0;
    string buffer;
    kvm_t *kd = nullptr;
    kinfo_proc *proc_info = nullptr;
    bool error = false, retried = false, leading_dash_removed = false;
    kd = kvm_openfiles(nullptr, nullptr, nullptr, KVM_NO_FILES, nullptr);
    if (kd) {
      if ((proc_info = kvm_getprocs(kd, KERN_PROC_PID, getpid(), sizeof(struct kinfo_proc), &cntp))) {
        char **cmd = kvm_getargv(kd, proc_info, 0);
        if (cmd && cmd[0]) {
          buffer = cmd[0];
        }
      }
      kvm_close(kd);
    }
    string argv0;
    bool argv0_does_not_exist = false;
    size_t slash_pos = string::npos;
    size_t colon_pos = string::npos;
    if (buffer.empty()) {
      argv0_does_not_exist = true;
      goto path_lookup;
    } else {
      fallback:
      slash_pos = buffer.find('/');
      colon_pos = buffer.find(':');
      if (slash_pos == 0) {
        argv0 = buffer;
        path = verify_exe(argv0);
      } else if (slash_pos == string::npos || (colon_pos != string::npos && colon_pos > 0 && slash_pos > colon_pos)) {
        path_lookup:
        retry_without_leading_dash:
        string penv = environment_get_variable("PATH");
        if (!penv.empty()) {
          retry:
          string tmp;
          stringstream sstr(penv);
          while (getline(sstr, tmp, ':')) {
            argv0 = tmp + "/" + buffer;
            path = verify_exe(argv0);
            if (!path.empty()) break;
            if (!argv0_does_not_exist && colon_pos != string::npos && colon_pos > 0 && slash_pos > colon_pos) {
              argv0 = tmp + "/" + buffer.substr(0, colon_pos);
              path = verify_exe(argv0);
              if (!path.empty()) break;
            }
          }
        }
        if (path.empty() && !retried) {
          retried = true;
          penv = "/usr/bin:/bin:/usr/sbin:/sbin:/usr/X11R6/bin:/usr/local/bin:/usr/local/sbin";
          string home = environment_get_variable("HOME");
         if (!home.empty()) {
            penv = home + "/bin:" + penv;
          }
          goto retry;
        }
        if (path.empty() && !argv0_does_not_exist && !leading_dash_removed && slash_pos == string::npos && buffer.length() > 1 && buffer[0] == '-') {
          buffer = buffer.substr(1);
          retried = false;
          leading_dash_removed = true;
          goto retry_without_leading_dash;
        }
      }
      if (path.empty() && (argv0_does_not_exist || (slash_pos != string::npos && slash_pos > 0))) {
        string pwd = environment_get_variable("PWD");
        if (!pwd.empty()) {
          argv0 = pwd + "/" + buffer;
          path = verify_exe(argv0);
        }
        if (path.empty()) {
          string cwd = directory_get_current_working();
          if (!cwd.empty()) {
            argv0 = cwd + "/" + buffer;
            path = verify_exe(argv0);
          }
        }
      }
      if (path.empty() && !error) {
        error = true;
        buffer.clear();
        string underscore = environment_get_variable("_");
        if (!underscore.empty()) {
          buffer = underscore;
          leading_dash_removed = false;
          retried = false;
          goto fallback;
        }
      }
    }
    if (path.empty() && !argv0_does_not_exist) {
      argv0_does_not_exist = true;
      retried = false;
      buffer.clear();
      goto path_lookup;
    }
    #elif (defined(__sun) && defined(__SVR4))
    const char *execname = getexecname();
    if (execname) {
      char exe[PATH_MAX];
      if (realpath(execname, exe)) {
        path = exe;
      }
    }
    if (path.empty()) {
      char exe[PATH_MAX];
      if (realpath("/proc/self/path/a.out", exe)) {
        path = exe;
      }
    }
    #endif
    return path;
  }

  bool symlink_create(string fname, string newname) {
    error_code ec;
    fname = expand_without_trailing_slash(fname);
    newname = expand_without_trailing_slash(newname);
    ghc::filesystem::path path1 = ghc::filesystem::path(fname);
    ghc::filesystem::path path2 = ghc::filesystem::path(newname);
    if (file_exists(fname)) {
      if (!directory_exists(filename_path(newname)))
        directory_create(filename_path(newname));
      ghc::filesystem::create_symlink(path1, path2, ec);
      return (ec.value() == 0);
    } else if (directory_exists(fname)) {
      if (!directory_exists(filename_path(newname)))
        directory_create(filename_path(newname));
      ghc::filesystem::create_directory_symlink(path1, path2, ec);
      return (ec.value() == 0);
    }
    return false;
  }

  bool symlink_copy(string fname, string newname) {
    error_code ec;
    fname = expand_without_trailing_slash(fname);
    newname = expand_without_trailing_slash(newname);
    ghc::filesystem::path path1 = ghc::filesystem::path(fname);
    ghc::filesystem::path path2 = ghc::filesystem::path(newname);
    if (symlink_exists(fname)) {
      if (!directory_exists(filename_path(newname)))
        directory_create(filename_path(newname));
      ghc::filesystem::copy_symlink(path1, path2, ec);
      return (ec.value() == 0);
    }
    return false;
  }

  bool symlink_exists(string fname) {
    error_code ec;
    fname = expand_without_trailing_slash(fname);
    ghc::filesystem::path path = ghc::filesystem::path(fname);
    return (ghc::filesystem::exists(path, ec) && ec.value() == 0 &&
      ghc::filesystem::is_symlink(path, ec) && ec.value() == 0);
  }

  bool hardlink_create(string fname, string newname) {
    fname = expand_without_trailing_slash(fname);
    newname = expand_without_trailing_slash(newname);
    if (file_exists(fname)) {
      if (!directory_exists(filename_path(newname)))
        directory_create(filename_path(newname));
      #if (defined(_WIN32) || defined(_WIN64))
      error_code ec;
      const ghc::filesystem::path path1 = ghc::filesystem::path(fname);
      const ghc::filesystem::path path2 = ghc::filesystem::path(newname);
      ghc::filesystem::create_hard_link(path1, path2, ec);
      return (ec.value() == 0);
      #else
      return (!link(fname.c_str(), newname.c_str()));
      #endif
    }
    return false;
  }

  uintmax_t file_numblinks(string fname) {
    error_code ec;
    fname = expand_without_trailing_slash(fname);
    if (file_exists(fname)) {
      int fd = file_bin_open(fname, FD_RDONLY);
      if (fd != -1) {
        uintmax_t result = file_bin_numblinks(fd);
        file_bin_close(fd);
        return result;
      }
    }
    return 0;
  }

  uintmax_t file_bin_numblinks(int fd) {
    #if (defined(_WIN32) || defined(_WIN64))
    BY_HANDLE_FILE_INFORMATION info;
    if (GetFileInformationByHandle((HANDLE)_get_osfhandle(fd), &info)) {
      return info.nNumberOfLinks;
    }
    #else
    struct stat info;
    if (!fstat(fd, &info)) {
      return info.st_nlink;
    }
    #endif
    return 0;
  }

  string file_bin_hardlinks(int fd, string dnames, bool recursive) {
    string paths;
    #if (defined(_WIN32) || defined(_WIN64))
    BY_HANDLE_FILE_INFORMATION info;
    if (GetFileInformationByHandle((HANDLE)_get_osfhandle(fd), &info) && info.nNumberOfLinks) {
    #else
    struct stat info;
    if (!fstat(fd, &info) && info.st_nlink) {
    #endif
      file_bin_hardlinks_result.clear();
      struct file_bin_hardlinks_struct s; 
      vector<string> in = string_split(dnames, '\n');
      if (in.empty()) return paths;
      vector<string> first;
      first.push_back(in[0]);
      in.erase(in.begin());
      s.x         = first;
      s.y         = in;
      s.i         = 0;
      s.j         = 0;
      s.recursive = recursive;
      s.info      = info;
      file_bin_hardlinks_helper(&s);
      for (unsigned i = 0; i < file_bin_hardlinks_result.size(); i++) {
        message_pump(); paths += file_bin_hardlinks_result[i] + "\n";
      }
      if (!paths.empty()) {
        paths.pop_back();
      }
    }
    return paths;
  }

  string executable_get_directory() {
    return filename_path(executable_get_pathname());
  }

  string executable_get_filename() {
    return filename_name(executable_get_pathname());
  }

  string environment_get_variable(string name) {
    #if (defined(_WIN32) || defined(_WIN64))
    string value; 
    DWORD length = 0;
    wstring u8name = widen(name);
    if ((length = GetEnvironmentVariableW(u8name.c_str(), nullptr, 0)) != 0) {
      wchar_t *buffer = new wchar_t[length]();
      if (GetEnvironmentVariableW(u8name.c_str(), buffer, length) != 0) {
        value = narrow(buffer);
      }
      delete[] buffer;
    }
    return value;
    #else
    char *value = getenv(name.c_str());
    return value ? value : "";
    #endif
  }

  bool environment_get_variable_exists(string name) {
    #if (defined(_WIN32) || defined(_WIN64))
    wstring u8name = widen(name);
    return (!(GetEnvironmentVariableW(u8name.c_str(), nullptr, 0) == 0 && 
      GetLastError() == ERROR_ENVVAR_NOT_FOUND));
    #else
    return (getenv(name.c_str()) != nullptr);
    #endif
  }

  bool environment_set_variable(string name, string value) {
    #if (defined(_WIN32) || defined(_WIN64))
    wstring u8name = widen(name); 
    wstring u8value = widen(value);
    return (SetEnvironmentVariableW(u8name.c_str(), u8value.c_str()) != 0);
    #else
    return (setenv(name.c_str(), value.c_str(), 1) == 0);
    #endif
  }

  bool environment_unset_variable(string name) {
    #if (defined(_WIN32) || defined(_WIN64))
    wstring u8name = widen(name);
    return (SetEnvironmentVariableW(u8name.c_str(), nullptr) != 0);
    #else
    return (unsetenv(name.c_str()) == 0);
    #endif
  }

  string environment_expand_variables(string str) {
    if (str.find("${") == string::npos) return str;
    string pre = str.substr(0, str.find("${"));
    string post = str.substr(str.find("${") + 2);
    if (post.find('}') == string::npos) return str;
    string variable = post.substr(0, post.find('}'));
    size_t pos = post.find('}') + 1; post = post.substr(pos);
    string value = environment_get_variable(variable);
    if (!environment_get_variable_exists(variable))
      return str.substr(0, pos) + environment_expand_variables(str.substr(pos));
    return environment_expand_variables(pre + value + post);
  }

  bool file_exists(string fname) {
    error_code ec;
    fname = expand_without_trailing_slash(fname);
    const ghc::filesystem::path path = ghc::filesystem::path(fname);
    return (ghc::filesystem::exists(path, ec) && ec.value() == 0 && 
      (!ghc::filesystem::is_directory(path, ec)) && ec.value() == 0);
  }

  bool directory_exists(string dname) {
    error_code ec;
    dname = expand_without_trailing_slash(dname);
    dname = expand_without_trailing_slash(dname);
    const ghc::filesystem::path path = ghc::filesystem::path(dname);
    return (ghc::filesystem::exists(path, ec) && ec.value() == 0 && 
      ghc::filesystem::is_directory(path, ec) && ec.value() == 0);
  }

  string filename_canonical(string fname) {
    error_code ec;
    fname = expand_without_trailing_slash(fname);
    const ghc::filesystem::path path = ghc::filesystem::path(fname);
    string result = ghc::filesystem::weakly_canonical(path, ec).string();
    if (ec.value() == 0 && directory_exists(result)) {
      return expand_with_trailing_slash(result);
    }
    return (ec.value() == 0) ? result : "";
  }

  string filename_absolute(string fname) {
    string result;
    if (directory_exists(fname)) {
      result = expand_with_trailing_slash(fname);
    } else if (file_exists(fname)) {
      result = expand_without_trailing_slash(fname);
    }
    return result;
  }

  bool filename_equivalent(string fname1, string fname2) {
    error_code ec;
    fname1 = expand_without_trailing_slash(fname1);
    fname2 = expand_without_trailing_slash(fname2);
    ghc::filesystem::path path1 = ghc::filesystem::path(fname1);
    ghc::filesystem::path path2 = ghc::filesystem::path(fname2);
    if (ghc::filesystem::exists(path1, ec) && ec.value() == 0 &&
      ghc::filesystem::exists(path2, ec) && ec.value() == 0) {
      return (ghc::filesystem::equivalent(path1, path2, ec) && ec.value() == 0);
    }
    return false;
  }

  uintmax_t file_size(string fname) {
    error_code ec;
    if (!file_exists(fname)) return 0;
    fname = expand_without_trailing_slash(fname);
    const ghc::filesystem::path path = ghc::filesystem::path(fname);
    uintmax_t result = ghc::filesystem::file_size(path, ec);
    return (ec.value() == 0) ? result : 0;
  }

  bool file_delete(string fname) {
    error_code ec;
    if (!file_exists(fname)) return false;
    fname = expand_without_trailing_slash(fname);
    const ghc::filesystem::path path = ghc::filesystem::path(fname);
    return (ghc::filesystem::remove(path, ec) && ec.value() == 0);
  }

  bool directory_create(string dname) {
    error_code ec;
    dname = expand_without_trailing_slash(dname);
    const ghc::filesystem::path path = ghc::filesystem::path(dname);
    return (ghc::filesystem::create_directories(path, ec) && ec.value() == 0);
  }

  bool file_rename(string oldname, string newname) {
    error_code ec;
    if (!file_exists(oldname)) return false;
    oldname = expand_without_trailing_slash(oldname);
    newname = expand_without_trailing_slash(newname);
    if (!directory_exists(filename_path(newname)))
      directory_create(filename_path(newname));
    const ghc::filesystem::path path1 = ghc::filesystem::path(oldname);
    const ghc::filesystem::path path2 = ghc::filesystem::path(newname);
    ghc::filesystem::rename(path1, path2, ec);
    return (ec.value() == 0);
  }

  bool file_copy(string fname, string newname) {
    error_code ec;
    if (!file_exists(fname)) return false;
    fname = expand_without_trailing_slash(fname);
    newname = expand_without_trailing_slash(newname);
    if (!directory_exists(filename_path(newname)))
      directory_create(filename_path(newname));
    const ghc::filesystem::path path1 = ghc::filesystem::path(fname);
    const ghc::filesystem::path path2 = ghc::filesystem::path(newname);
    ghc::filesystem::copy(path1, path2, ec);
    return (ec.value() == 0);
  }

  uintmax_t directory_size(string dname) {
    error_code ec;
    uintmax_t result = 0;
    if (!directory_exists(dname)) return 0;
    const ghc::filesystem::path path = ghc::filesystem::path(expand_without_trailing_slash(dname));
    if (ghc::filesystem::exists(path, ec)) {
      ghc::filesystem::directory_iterator end_itr;
      for (ghc::filesystem::directory_iterator dir_ite(path, ec); dir_ite != end_itr; dir_ite.increment(ec)) {
        message_pump(); if (ec.value() != 0) { break; }
        ghc::filesystem::path file_path = ghc::filesystem::path(filename_absolute(dir_ite->path().string()));
        if (!directory_exists(file_path.string())) {
          result += file_size(file_path.string());
        } else {
          result += directory_size(file_path.string());
        }
      }
    }
    return result;
  }

  bool directory_destroy(string dname) {
    error_code ec;
    if (!directory_exists(dname)) return false;
    dname = expand_without_trailing_slash(dname);
    const ghc::filesystem::path path = ghc::filesystem::path(dname);
    return (ghc::filesystem::remove_all(path, ec) && ec.value() == 0);
  }

  unsigned directory_contents_get_length() {
    return (unsigned)directory_contents.size();
  }

  unsigned directory_contents_get_cntfiles() {
    return directory_contents_cntfiles;
  }

  unsigned directory_contents_get_maxfiles() {
    return directory_contents_maxfiles;
  }

  void directory_contents_set_maxfiles(unsigned maxfiles) {
    directory_contents_maxfiles = maxfiles;
  }

  static inline vector<string> directory_contents_helper(string dname, string pattern, bool includedirs) {
    error_code ec; vector<string> result;
    if (!directory_exists(dname)) return result;
    dname = expand_without_trailing_slash(dname);
    const ghc::filesystem::path path = ghc::filesystem::path(dname);
    if (directory_exists(dname)) {
      ghc::filesystem::directory_iterator end_itr;
      for (ghc::filesystem::directory_iterator dir_ite(path, ec); dir_ite != end_itr && !directory_contents_completion_status &&
        (directory_contents_maxfiles == 0 || directory_contents_cntfiles < directory_contents_maxfiles); dir_ite.increment(ec)) {
        message_pump(); if (ec.value() != 0) { break; }
        ghc::filesystem::path file_path = ghc::filesystem::path(filename_absolute(dir_ite->path().string()));
        if (!directory_exists(file_path.string())) {
          result.push_back(file_path.string());
        } else if (ec.value() == 0 && includedirs) {
          result.push_back(expand_with_trailing_slash(file_path.string()));
        }
        directory_contents_cntfiles++;
      }
    }
    if (pattern.empty()) pattern = "*.*";
    pattern = string_replace_all(pattern, " ", "");
    pattern = string_replace_all(pattern, "*", "");
    vector<string> extVec = string_split(pattern, ';');
    set<string> filteredItems;
    for (const string &item : result) {
      message_pump();
      for (const string &ext : extVec) {
        message_pump();
        if (ext == "." || ext == filename_ext(item) || directory_exists(item)) {
          filteredItems.insert(item);
          break;
        }
      }
    }
    vector<string> result_filtered;
    if (filteredItems.empty()) return result_filtered;
    for (const string &filteredName : filteredItems) {
      message_pump();
      result_filtered.push_back(filteredName);
    }
    return result_filtered;
  }

  static inline vector<string> directory_contents_recursive_helper(string dname, string pattern, bool includedirs) {
    vector<string> result = directory_contents_helper(dname, pattern, true);
    for (unsigned i = 0; i < result.size(); i++) {
      message_pump();
      if (directory_exists(result[i])) {
        vector<string> recursive_result = directory_contents_recursive_helper(result[i], pattern, includedirs);
        for (unsigned j = 0; j < recursive_result.size(); j++) {
          message_pump();
          result.insert(result.end(), recursive_result[j]);
        }
      }
    }
    vector<string> result_filtered;
    for (unsigned i = 0; i < result.size(); i++) {
      message_pump();
      if (!directory_exists(result[i])) {
        result_filtered.push_back(result[i]);
      } else if (includedirs) {
        result_filtered.push_back(result[i]);
      }
    }
    set<string> removed_duplicates(result_filtered.begin(), result_filtered.end());
    result_filtered.clear(); result_filtered.assign(removed_duplicates.begin(), removed_duplicates.end());
    return result_filtered;
  }

  void directory_contents_close() {
    directory_contents.clear(); 
    directory_contents_index = 0;
    directory_contents_cntfiles = 1;
    directory_contents_completion_status = false;
  }

  unsigned directory_contents_get_order() {
    return directory_contents_order;
  }

  void directory_contents_set_order(unsigned order) {
    directory_contents_order = order;
  }

  string directory_contents_first(string dname, string pattern, bool includedirs, bool recursive) {
    if (directory_contents_completion_status) directory_contents_close();
    if (!recursive) directory_contents = directory_contents_helper(dname, pattern, includedirs);
    else directory_contents = directory_contents_recursive_helper(dname, pattern, includedirs);
    if (directory_contents_index < directory_contents.size()) {
      if (directory_contents_order == DC_ZTOA) {
        reverse(directory_contents.begin(), directory_contents.end());
      } else if (directory_contents_order == DC_AOTON) {
        sort(directory_contents.begin(), directory_contents.end(),
        [](const string &l, const string &r) {
        return (file_datetime_helper(l, 0) < file_datetime_helper(r, 0));
        });
      } else if (directory_contents_order == DC_ANTOO) {
        sort(directory_contents.begin(), directory_contents.end(),
        [](const string &l, const string &r) {
        return (file_datetime_helper(l, 0) > file_datetime_helper(r, 0));
        });
      } else if (directory_contents_order == DC_MOTON) {
        sort(directory_contents.begin(), directory_contents.end(),
        [](const string &l, const string &r) {
        return (file_datetime_helper(l, 1) < file_datetime_helper(r, 1));
        });
      } else if (directory_contents_order == DC_MNTOO) {
        sort(directory_contents.begin(), directory_contents.end(),
        [](const string &l, const string &r) {
        return (file_datetime_helper(l, 1) > file_datetime_helper(r, 1));
        });
      } else if (directory_contents_order == DC_COTON) {
        sort(directory_contents.begin(), directory_contents.end(),
        [](const string &l, const string &r) {
        return (file_datetime_helper(l, 2) < file_datetime_helper(r, 2));
        });
      } else if (directory_contents_order == DC_CNTOO) {
        sort(directory_contents.begin(), directory_contents.end(),
        [](const string &l, const string &r) {
        return (file_datetime_helper(l, 2) > file_datetime_helper(r, 2));
        });
      } else if (directory_contents_order == DC_RAND) {
        random_device rd; mt19937 g(rd());
        shuffle(directory_contents.begin(), directory_contents.end(), g);
      }
      directory_contents_completion_status = true;
      return directory_contents[directory_contents_index];
    } 
    directory_contents_completion_async = false;
    directory_contents_completion_status = true;
    return "";
  }

  string directory_contents_next() {
    if (!directory_contents_completion_async)
      directory_contents_index++;
    directory_contents_completion_async = false;
    if (directory_contents_index < directory_contents.size())
      return directory_contents[directory_contents_index];
    return "";
  }

  void directory_contents_first_async(string dname, string pattern, bool includedirs, bool recursive) {
    directory_contents_completion_async = true;
    directory_contents_completion_status = false;
    thread(directory_contents_first, dname, pattern, includedirs, recursive).detach();
  }

  bool directory_contents_get_completion_status() {
    return directory_contents_completion_status;
  }

  void directory_contents_set_completion_status(bool complete) {
    directory_contents_completion_status = complete;
  }

  static inline bool file_is_inside_directory(string outer, string inner) {
    if (!directory_exists(outer)) return false;
    outer = expand_without_trailing_slash(outer);
    inner = expand_without_trailing_slash(inner);
    const ghc::filesystem::path path1 = ghc::filesystem::path(outer);
    ghc::filesystem::path path2 = ghc::filesystem::path(inner);
    #if (defined(_WIN32) || defined(_WIN64)) 
    while (expand_without_trailing_slash(path2.string()) !=
      expand_without_trailing_slash(path2.root_name().string())) {
    #else
    while (expand_without_trailing_slash(path2.string()) != "/") {
    #endif
      message_pump();
      if (!filename_equivalent(path1.string(), path2.string())) {
        path2 = path2.parent_path();
      } else {
        return true;
      }
    }
    return false;
  }

  bool directory_copy(string dname, string newname) {
    error_code ec;
    if (!directory_exists(dname)) return false;
    dname = expand_without_trailing_slash(dname);
    newname = expand_without_trailing_slash(newname);
    if (!file_is_inside_directory(dname, newname)) {
      const ghc::filesystem::path path1 = ghc::filesystem::path(dname);
      const ghc::filesystem::path path2 = ghc::filesystem::path(newname);
      if (!directory_exists(path2.parent_path().string())) {
        if (!directory_create(path2.parent_path().string())) {
          return false;
        }
      }
      ghc::filesystem::copy(path1, path2, 
        ghc::filesystem::copy_options::recursive |
        ghc::filesystem::copy_options::copy_symlinks, ec);
      return (ec.value() == 0);
    } else {
      return false;
    }
    return true;
  }

  bool directory_rename(string oldname, string newname) {
    error_code ec;
    if (!directory_exists(oldname)) return false;
    oldname = expand_without_trailing_slash(oldname);
    newname = expand_without_trailing_slash(newname);
    if (!file_is_inside_directory(oldname, newname)) {
      const ghc::filesystem::path path1 = ghc::filesystem::path(oldname);
      const ghc::filesystem::path path2 = ghc::filesystem::path(newname);
      if (!directory_exists(path2.parent_path().string())) {
        if (!directory_create(path2.parent_path().string())) {
          return false;
        }
      }
      ghc::filesystem::rename(path1, path2, ec);
      return (ec.value() == 0);
    } else {
      return false;
    }
    return true;
  }

  int file_datetime_accessed_year(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 0, 0);
  }

  int file_datetime_accessed_month(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 0, 1);
  }

  int file_datetime_accessed_day(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 0, 2);
  }

  int file_datetime_accessed_hour(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 0, 3);
  }

  int file_datetime_accessed_minute(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 0, 4);
  }

  int file_datetime_accessed_second(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 0, 5);
  }

  int file_datetime_modified_year(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 1, 0);
  }

  int file_datetime_modified_month(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 1, 1);
  }

  int file_datetime_modified_day(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 1, 2);
  }

  int file_datetime_modified_hour(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 1, 3);
  }

  int file_datetime_modified_minute(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 1, 4);
  }

  int file_datetime_modified_second(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 1, 5);
  }

  int file_datetime_created_year(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 2, 0);
  }

  int file_datetime_created_month(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 2, 1);
  }

  int file_datetime_created_day(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 2, 2);
  }

  int file_datetime_created_hour(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 2, 3);
  }

  int file_datetime_created_minute(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 2, 4);
  }

  int file_datetime_created_second(string fname) {
    fname = expand_without_trailing_slash(fname);
    return file_datetime(fname.c_str(), 2, 5);
  }

  int file_bin_datetime_accessed_year(int fd) {
    return file_bin_datetime(fd, 0, 0);
  }

  int file_bin_datetime_accessed_month(int fd) {
    return file_bin_datetime(fd, 0, 1);
  }

  int file_bin_datetime_accessed_day(int fd) {
    return file_bin_datetime(fd, 0, 2);
  }

  int file_bin_datetime_accessed_hour(int fd) {
    return file_bin_datetime(fd, 0, 3);
  }

  int file_bin_datetime_accessed_minute(int fd) {
    return file_bin_datetime(fd, 0, 4);
  }

  int file_bin_datetime_accessed_second(int fd) {
    return file_bin_datetime(fd, 0, 5);
  }

  int file_bin_datetime_modified_year(int fd) {
    return file_bin_datetime(fd, 1, 0);
  }

  int file_bin_datetime_modified_month(int fd) {
    return file_bin_datetime(fd, 1, 1);
  }

  int file_bin_datetime_modified_day(int fd) {
    return file_bin_datetime(fd, 1, 2);
  }

  int file_bin_datetime_modified_hour(int fd) {
    return file_bin_datetime(fd, 1, 3);
  }

  int file_bin_datetime_modified_minute(int fd) {
    return file_bin_datetime(fd, 1, 4);
  }

  int file_bin_datetime_modified_second(int fd) {
    return file_bin_datetime(fd, 1, 5);
  }

  int file_bin_datetime_created_year(int fd) {
    return file_bin_datetime(fd, 2, 0);
  }

  int file_bin_datetime_created_month(int fd) {
    return file_bin_datetime(fd, 2, 1);
  }

  int file_bin_datetime_created_day(int fd) {
    return file_bin_datetime(fd, 2, 2);
  }

  int file_bin_datetime_created_hour(int fd) {
    return file_bin_datetime(fd, 2, 3);
  }

  int file_bin_datetime_created_minute(int fd) {
    return file_bin_datetime(fd, 2, 4);
  }

  int file_bin_datetime_created_second(int fd) {
    return file_bin_datetime(fd, 2, 5);
  }

  int file_bin_open(string fname, int mode) {
    fname = expand_without_trailing_slash(fname);
    #if (defined(_WIN32) || defined(_WIN64))
    wstring wfname = widen(fname);
    FILE *fp = nullptr;
    switch (mode) {
      case  0: { if (!_wfopen_s(&fp, wfname.c_str(), L"rb, ccs=UTF-8" )) break; return -1; }
      case  1: { if (!_wfopen_s(&fp, wfname.c_str(), L"wb, ccs=UTF-8" )) break; return -1; }
      case  2: { if (!_wfopen_s(&fp, wfname.c_str(), L"w+b, ccs=UTF-8")) break; return -1; }
      case  3: { if (!_wfopen_s(&fp, wfname.c_str(), L"ab, ccs=UTF-8" )) break; return -1; }
      case  4: { if (!_wfopen_s(&fp, wfname.c_str(), L"a+b, ccs=UTF-8")) break; return -1; }
      default: return -1;
    }
    if (fp) { int fd = _dup(_fileno(fp));
    fclose(fp); return fd; }
    #else
    FILE *fp = nullptr;
    switch (mode) {
      case  0: { fp = fopen(fname.c_str(), "rb" ); break; }
      case  1: { fp = fopen(fname.c_str(), "wb" ); break; }
      case  2: { fp = fopen(fname.c_str(), "w+b"); break; }
      case  3: { fp = fopen(fname.c_str(), "ab" ); break; }
      case  4: { fp = fopen(fname.c_str(), "a+b"); break; }
      default: return -1;
    }
    if (fp) { int fd = dup(fileno(fp));
    fclose(fp); return fd; }
    #endif
    return -1;
  }

  int file_bin_rewrite(int fd) {
    #if (defined(_WIN32) || defined(_WIN64))
    _lseek(fd, 0, SEEK_SET);
    return _chsize(fd, 0);
    #else
    lseek(fd, 0, SEEK_SET);
    return ftruncate(fd, 0);
    #endif
  }
  
  int file_bin_close(int fd) {
    #if (defined(_WIN32) || defined(_WIN64))
    return _close(fd);
    #else
    return close(fd);
    #endif
  }
  
  long file_bin_size(int fd) {
    #if (defined(_WIN32) || defined(_WIN64))
    struct _stat info; 
    int result = _fstat(fd, &info);
    #else
    struct stat info; 
    int result = fstat(fd, &info);
    #endif
    if (result != -1) {
      return info.st_size;
    }
    return 0;
  }

  long file_bin_position(int fd) {
    #if (defined(_WIN32) || defined(_WIN64))
    return _lseek(fd, 0, SEEK_CUR);
    #else
    return lseek(fd, 0, SEEK_CUR);
    #endif
  }
  
  long file_bin_seek(int fd, long pos) {
    #if (defined(_WIN32) || defined(_WIN64))
    return _lseek(fd, pos, SEEK_CUR);
    #else
    return lseek(fd, pos, SEEK_CUR);
    #endif
  }

  int file_bin_read_byte(int fd) {
    int byte = 0;
    #if (defined(_WIN32) || defined(_WIN64))
    int num = (int)_read(fd, &byte, 1);
    #else
    int num = (int)read(fd, &byte, 1);
    #endif
    if (num == -1) return 0;
    return byte;
  }

  int file_bin_write_byte(int fd, int byte) {
    #if (defined(_WIN32) || defined(_WIN64))
    return (int)_write(fd, &byte, 1);
    #else
    return (int)write(fd, &byte, 1);
    #endif
  }

  int file_text_open_read(string fname) {
    return file_bin_open(fname, 0);
  }

  int file_text_open_write(string fname) {
    return file_bin_open(fname, 1);
  }

  int file_text_open_append(string fname) {
    return file_bin_open(fname, 3);
  }

  long file_text_write_string(int fd, string str) {
    char *buffer = str.data();
    #if (defined(_WIN32) || defined(_WIN64))
    long result = _write(fd, buffer, (unsigned)str.length());
    #else
    long result = write(fd, buffer, (unsigned)str.length());
    #endif
    return result;
  }

  long file_text_write_real(int fd, double val) {
    string str = to_string(val);
    return file_text_write_string(fd, str);
  }

  int file_text_writeln(int fd) {
    return file_bin_write_byte(fd, '\n');
  }

  static unsigned cnt = 0;
  bool file_text_eof(int fd) {
    bool res1 = ((char)file_bin_read_byte(fd) == '\0');
    bool res2 = (file_bin_position(fd) > file_bin_size(fd));
    while (res2 && cnt < 2) { message_pump(); 
    file_bin_seek(fd, -1); cnt++; }
    if (!res2) file_bin_seek(fd, -1);
    cnt = 0; return (res1 || res2);
  }

  bool file_text_eoln(int fd) {
    bool res1 = ((char)file_bin_read_byte(fd) == '\n');
    bool res2 = file_text_eof(fd);
    while (res2 && cnt < 2) { message_pump(); 
    file_bin_seek(fd, -1); cnt++; }
    if (!res2) file_bin_seek(fd, -1);
    cnt = 0; return (res1 || res2);
  }

  string file_text_read_string(int fd) {
    int byte = file_bin_read_byte(fd); string str;
    str.push_back((char)byte);
    while ((char)byte != '\n') {
      message_pump();
      byte = file_bin_read_byte(fd);
      str.push_back((char)byte);
      if (byte == 0) break;
    }
    if (str.length() >= 2) {
      if (str[str.length() - 2] != '\r' && str[str.length() - 1] == '\n') {
        file_bin_seek(fd, -1);
        str = str.substr(0, str.length() - 1);
      }
      if (str[str.length() - 2] == '\r' && str[str.length() - 1] == '\n') {
        file_bin_seek(fd, -2);
        str = str.substr(0, str.length() - 2);
      }
    } else if (str.length() == 1) {
      if (str[str.length() - 1] == '\n') {
        file_bin_seek(fd, -1);
        str = str.substr(0, str.length() - 1);
      }
    }
    return str;
  }

  double file_text_read_real(int fd) {
    bool dot = false, sign = false;
    string str; char byte = (char)file_bin_read_byte(fd);
    while (byte == '\r' || byte == '\n') { 
      message_pump();
      byte = (char)file_bin_read_byte(fd);
    }
    if (byte == '.' && !dot) {
      dot = true;
    } else if (!is_digit(byte) && byte != '+' && 
      byte != '-' && byte != '.') {
      return 0;
    } else if (byte == '+' || byte == '-') {
      sign = true;
    }
    if (byte == 0) goto finish;
    str.push_back(byte);
    if (sign) {
      byte = (char)file_bin_read_byte(fd);
      if (byte == '.' && !dot) {
        dot = true;
      } else if (!is_digit(byte) && byte != '.') {
        return strtod(str.c_str(), nullptr);
      }
      if (byte == 0) goto finish;
      str.push_back(byte);
    }
    while (byte != '\n' && !(file_bin_position(fd) > file_bin_size(fd))) {
      message_pump();
      byte = (char)file_bin_read_byte(fd);
      if (byte == '.' && !dot) {
        dot = true;
      } else if (byte == '.' && dot) {
        break;
      } else if (!is_digit(byte) && byte != '.') {
        break;
      } else if (byte == '\n' || file_bin_position(fd) > file_bin_size(fd)) {
        break;
      }
      if (byte == 0) goto finish;
      str.push_back(byte);
    }
    finish:
    return strtod(str.c_str(), nullptr);
  }

  string file_text_readln(int fd) {
    int byte = file_bin_read_byte(fd); string str;
    str.push_back((char)byte);
    while ((char)byte != '\n') {
      message_pump();
      byte = file_bin_read_byte(fd);
      str.push_back((char)byte);
      if (byte == 0) break;
    }
    return str;
  }

  string file_text_read_all(int fd) {
    string str;
    long sz = file_bin_size(fd);
    char *buffer = new char[sz];
    #if (defined(_WIN32) || defined(_WIN64))
    long result = _read(fd, buffer, sz);
    #else
    long result = read(fd, buffer, sz);
    #endif
    if (result == -1) {
      delete[] buffer;
      return "";
    }
    str = buffer ? buffer : "";
    delete[] buffer;
    return str;
  }

  int file_text_open_from_string(string str) {
    string fname = directory_get_temporary_path() + "temp.XXXXXX";
    #if (defined(_WIN32) || defined(_WIN64))
    int fd = -1; wstring wfname = widen(fname); 
    wchar_t *buffer = wfname.data(); if (_wmktemp_s(buffer, wfname.length() + 1)) return -1;
    if (_wsopen_s(&fd, buffer, _O_CREAT | _O_RDWR | _O_WTEXT, _SH_DENYNO, _S_IREAD | _S_IWRITE)) {
      return -1;
    }
    #else
    char *buffer = fname.data();
    int fd = mkstemp(buffer);
    #endif
    if (fd == -1) return -1;
    file_text_write_string(fd, str);
    #if (defined(_WIN32) || defined(_WIN64))
    _lseek(fd, 0, SEEK_SET);
    #else
    lseek(fd, 0, SEEK_SET);
    #endif
    return fd;
  }
  
  int file_text_close(int fd) {
    return file_bin_close(fd);
  }

} // namespace ngs::fs
#endif
