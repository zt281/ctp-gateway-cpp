#include "ctp_loader.h"

#include <stdexcept>
#include <string>

// ── 平台适配层 ───────────────────────────────────────────────────
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

void* CtpLoader::open_lib(const std::string& path) {
    HMODULE h = LoadLibraryA(path.c_str());
    if (!h) {
        DWORD err = GetLastError();
        throw std::runtime_error(
            "[CtpLoader] LoadLibraryA failed: " + path +
            " (error=" + std::to_string(err) + ")");
    }
    return reinterpret_cast<void*>(h);
}

void* CtpLoader::get_sym(void* handle, const char* name) {
    HMODULE h = reinterpret_cast<HMODULE>(handle);
    void* sym = reinterpret_cast<void*>(GetProcAddress(h, name));
    if (!sym) {
        DWORD err = GetLastError();
        throw std::runtime_error(
            std::string("[CtpLoader] GetProcAddress failed for '") + name +
            "' (error=" + std::to_string(err) + ")");
    }
    return sym;
}

const char* CtpLoader::default_md_dll_name() {
    return "thostmduserapi_se.dll";
}
const char* CtpLoader::default_td_dll_name() {
    return "thosttraderapi_se.dll";
}

#else  // Linux / macOS
#  include <dlfcn.h>

void* CtpLoader::open_lib(const std::string& path) {
    void* h = dlopen(path.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    if (!h) {
        throw std::runtime_error(
            std::string("[CtpLoader] dlopen failed: ") + dlerror());
    }
    return h;
}

void* CtpLoader::get_sym(void* handle, const char* name) {
    void* sym = dlsym(handle, name);
    if (!sym) {
        throw std::runtime_error(
            std::string("[CtpLoader] dlsym failed for '") + name +
            "': " + dlerror());
    }
    return sym;
}

const char* CtpLoader::default_md_dll_name() {
    return "libthostmduserapi_se.so";
}
const char* CtpLoader::default_td_dll_name() {
    return "libthosttraderapi_se.so";
}

#endif  // _WIN32

// ── 路径工具 ────────────────────────────────────────────────────
std::string CtpLoader::join_path(const std::string& dir,
                                  const std::string& filename) {
    if (dir.empty()) return filename;
    char last = dir.back();
    if (last == '/' || last == '\\') {
        return dir + filename;
    }
    return dir + "/" + filename;
}

std::string CtpLoader::resolve_md_dll(const std::string& dll_dir,
                                       const std::string& dll_name) {
    const std::string name = dll_name.empty()
        ? std::string(default_md_dll_name())
        : dll_name;
    return join_path(dll_dir, name);
}

std::string CtpLoader::resolve_td_dll(const std::string& dll_dir,
                                       const std::string& dll_name) {
    const std::string name = dll_name.empty()
        ? std::string(default_td_dll_name())
        : dll_name;
    return join_path(dll_dir, name);
}

// ── CTP API 函数类型 ───────────────────────────────────────────
// MD: CThostFtdcMdApi* CreateFtdcMdApi(const char* pszFlowPath,
//                                       bool bIsUsingUdp,
//                                       bool bIsMulticast)
using CreateMdApiFn = void* (*)(const char*, bool, bool);

// TD: CThostFtdcTraderApi* CreateFtdcTraderApi(const char* pszFlowPath)
using CreateTdApiFn = void* (*)(const char*);

// ── 公开创建函数 ───────────────────────────────────────────────
CThostFtdcMdApi* CtpLoader::create_md_api(const std::string& dll_dir,
                                            const std::string& dll_name,
                                            const std::string& flow_path) {
    std::string path = resolve_md_dll(dll_dir, dll_name);
    void* handle     = open_lib(path);
    void* sym        = get_sym(handle, "CreateFtdcMdApi");

    auto fn = reinterpret_cast<CreateMdApiFn>(sym);
    void* api = fn(flow_path.c_str(), false, false);
    if (!api) {
        throw std::runtime_error(
            "[CtpLoader] CreateFtdcMdApi returned null (dll=" + path + ")");
    }
    return reinterpret_cast<CThostFtdcMdApi*>(api);
}

CThostFtdcTraderApi* CtpLoader::create_td_api(const std::string& dll_dir,
                                                const std::string& dll_name,
                                                const std::string& flow_path) {
    std::string path = resolve_td_dll(dll_dir, dll_name);
    void* handle     = open_lib(path);
    void* sym        = get_sym(handle, "CreateFtdcTraderApi");

    auto fn = reinterpret_cast<CreateTdApiFn>(sym);
    void* api = fn(flow_path.c_str());
    if (!api) {
        throw std::runtime_error(
            "[CtpLoader] CreateFtdcTraderApi returned null (dll=" + path + ")");
    }
    return reinterpret_cast<CThostFtdcTraderApi*>(api);
}
