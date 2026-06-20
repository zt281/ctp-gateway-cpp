#include "ctp_loader.h"
#include "gateway_log.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <string.h>  // for strnlen (MSVC compat)

// ── 平台适配层 ───────────────────────────────────────────────────
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

// 在已加载模块的导出表中查找包含 substr 的函数名（用于 C++ mangled names）
// 包含基本边界检查，防止 malformed DLL 导致越界读取
static void* find_export_containing(HMODULE h, const char* substr) {
    auto base = reinterpret_cast<const unsigned char*>(h);
    auto dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    const auto& export_entry =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (export_entry.VirtualAddress == 0 || export_entry.Size == 0)
        return nullptr;

    // 确保导出目录完全落在映像内
    const DWORD image_size = nt->OptionalHeader.SizeOfImage;
    if (export_entry.VirtualAddress >= image_size ||
        export_entry.Size > image_size - export_entry.VirtualAddress)
        return nullptr;

    auto exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(
        base + export_entry.VirtualAddress);

    // 对 NumberOfNames 做合理性上限检查（防止 corrupted export table）
    constexpr DWORD MAX_EXPORT_NAMES = 1000000;
    if (exports->NumberOfNames == 0 || exports->NumberOfNames > MAX_EXPORT_NAMES)
        return nullptr;
    if (exports->NumberOfFunctions == 0 || exports->NumberOfFunctions > MAX_EXPORT_NAMES)
        return nullptr;

    // 验证各数组指针在映像范围内
    if (exports->AddressOfNames >= image_size ||
        exports->AddressOfNameOrdinals >= image_size ||
        exports->AddressOfFunctions >= image_size)
        return nullptr;

    auto names    = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
    auto ordinals = reinterpret_cast<const WORD*>(base + exports->AddressOfNameOrdinals);
    auto funcs    = reinterpret_cast<const DWORD*>(base + exports->AddressOfFunctions);

    // 验证数组大小不超出映像
    const size_t names_size = static_cast<size_t>(exports->NumberOfNames) * sizeof(DWORD);
    const size_t ordinals_size = static_cast<size_t>(exports->NumberOfNames) * sizeof(WORD);
    const size_t funcs_size = static_cast<size_t>(exports->NumberOfFunctions) * sizeof(DWORD);
    if (exports->AddressOfNames + names_size > image_size ||
        exports->AddressOfNameOrdinals + ordinals_size > image_size ||
        exports->AddressOfFunctions + funcs_size > image_size)
        return nullptr;

    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        DWORD name_rva = names[i];
        if (name_rva >= image_size) continue;

        const char* fn_name = reinterpret_cast<const char*>(base + name_rva);
        // 限制搜索长度，防止无终止符字符串导致越界
        constexpr size_t MAX_NAME_LEN = 4096;
        if (strnlen(fn_name, MAX_NAME_LEN) >= MAX_NAME_LEN)
            continue;

        if (std::strstr(fn_name, substr)) {
            WORD ordinal = ordinals[i];
            if (ordinal >= exports->NumberOfFunctions) continue;

            DWORD func_rva = funcs[ordinal];
            if (func_rva >= image_size) continue;

            void* addr = reinterpret_cast<void*>(
                const_cast<unsigned char*>(base) + func_rva);
            LOG_INFO("Resolved '%s' via mangled export: %s", substr, fn_name);
            return addr;
        }
    }
    return nullptr;
}

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
    // 优先尝试 plain C 名称（标准 CTP SDK）
    void* sym = reinterpret_cast<void*>(GetProcAddress(h, name));
    if (sym) return sym;

    // 回退：在导出表中搜索包含目标名称的 C++ mangled symbol（TTS DLL）
    sym = find_export_containing(h, name);
    if (sym) return sym;

    DWORD err = GetLastError();
    throw std::runtime_error(
        std::string("[CtpLoader] GetProcAddress failed for '") + name +
        "' (error=" + std::to_string(err) + ")");
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

// 验证 DLL 路径安全性：禁止路径遍历和可疑文件名
static void validate_dll_path(const std::string& path,
                               const std::string& expected_name) {
    // 要求 DLL 目录为绝对路径
#ifdef _WIN32
    bool is_absolute = false;
    if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) &&
        path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
        is_absolute = true;
    } else if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') {
        is_absolute = true;
    }
#else
    bool is_absolute = !path.empty() && path[0] == '/';
#endif
    if (!is_absolute) {
        throw std::runtime_error(
            "[CtpLoader] DLL directory must be an absolute path: " + path);
    }
    // 拒绝包含 .. 的路径遍历
    if (path.find("..") != std::string::npos) {
        throw std::runtime_error(
            "[CtpLoader] DLL path contains directory traversal: " + path);
    }
    // 拒绝空文件名
    if (expected_name.empty()) {
        throw std::runtime_error(
            "[CtpLoader] DLL name is empty");
    }
    // 拒绝包含路径分隔符的文件名（文件名应仅为文件名，不含目录）
    if (expected_name.find('/') != std::string::npos ||
        expected_name.find('\\') != std::string::npos) {
        throw std::runtime_error(
            "[CtpLoader] DLL name contains path separators: " + expected_name);
    }
    // 验证扩展名
    bool has_valid_ext = false;
#ifdef _WIN32
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".dll") == 0)
        has_valid_ext = true;
#else
    if (path.size() > 3 && path.compare(path.size() - 3, 3, ".so") == 0)
        has_valid_ext = true;
#endif
    if (!has_valid_ext) {
        throw std::runtime_error(
            "[CtpLoader] DLL path has invalid extension: " + path);
    }
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
// 注意: TTS(OpenCTP) DLL 可能比标准 CTP 多一个 bool 参数。
// 在 x64 调用约定下，额外的寄存器参数会被被调用方安全忽略，
// 因此统一使用完整参数列表可兼容两种 DLL。
//
// MD: CThostFtdcMdApi* CreateFtdcMdApi(const char* pszFlowPath,
//        bool bIsUsingUdp, bool bIsMulticast [, bool bIsDetectPkg])
using CreateMdApiFn = void* (*)(const char*, bool, bool, bool);

// TD: CThostFtdcTraderApi* CreateFtdcTraderApi(const char* pszFlowPath
//        [, bool bIsDetectPkg])
using CreateTdApiFn = void* (*)(const char*, bool);

// ── 公开创建函数 ───────────────────────────────────────────────
std::pair<DllHandle, MdApiPtr> CtpLoader::create_md_api(
        const std::string& dll_dir,
        const std::string& dll_name,
        const std::string& flow_path) {
    std::string path = resolve_md_dll(dll_dir, dll_name);
    validate_dll_path(path, dll_name.empty() ? default_md_dll_name() : dll_name);
    void* raw_handle = open_lib(path);
    DllHandle dll(raw_handle);  // RAII: 自动释放 DLL
    void* sym = get_sym(raw_handle, "CreateFtdcMdApi");

    auto fn = reinterpret_cast<CreateMdApiFn>(sym);
    void* raw_api = fn(flow_path.c_str(), false, false, false);
    if (!raw_api) {
        throw std::runtime_error(
            "[CtpLoader] CreateFtdcMdApi returned null (dll=" + path + ")");
    }

    MdApiPtr api(reinterpret_cast<CThostFtdcMdApi*>(raw_api));
    return {std::move(dll), std::move(api)};
}

std::pair<DllHandle, TdApiPtr> CtpLoader::create_td_api(
        const std::string& dll_dir,
        const std::string& dll_name,
        const std::string& flow_path) {
    std::string path = resolve_td_dll(dll_dir, dll_name);
    validate_dll_path(path, dll_name.empty() ? default_td_dll_name() : dll_name);
    void* raw_handle = open_lib(path);
    DllHandle dll(raw_handle);  // RAII: 自动释放 DLL
    void* sym = get_sym(raw_handle, "CreateFtdcTraderApi");

    auto fn = reinterpret_cast<CreateTdApiFn>(sym);
    void* raw_api = fn(flow_path.c_str(), false);
    if (!raw_api) {
        throw std::runtime_error(
            "[CtpLoader] CreateFtdcTraderApi returned null (dll=" + path + ")");
    }

    TdApiPtr api(reinterpret_cast<CThostFtdcTraderApi*>(raw_api));
    return {std::move(dll), std::move(api)};
}