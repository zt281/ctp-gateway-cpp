#pragma once
#include <string>
#include <stdexcept>

// 前向声明，避免在 loader 头文件中引入 CTP 头文件。
// CTP API 对象通过 void* 中转，调用方在 .cpp 中强制转换为具体类型。
struct CThostFtdcMdApi;
struct CThostFtdcTraderApi;

// CtpLoader — 跨平台动态加载 CTP DLL/SO。
//
// Windows: 使用 LoadLibraryA / GetProcAddress
// Linux:   使用 dlopen / dlsym
//
// 不依赖 CTP 头文件，只依赖 DLL 中导出的 C 函数符号。
class CtpLoader {
public:
    // 加载 MD DLL 并调用 CreateFtdcMdApi，返回 MdApi 实例。
    //   dll_dir   : DLL 所在目录（可含 / 或 \）
    //   dll_name  : DLL 文件名；空字符串时自动推断平台默认文件名
    //   flow_path : CTP 流文件路径（一般传 ""）
    static CThostFtdcMdApi* create_md_api(
        const std::string& dll_dir,
        const std::string& dll_name,
        const std::string& flow_path);

    // 加载 TD DLL 并调用 CreateFtdcTraderApi，返回 TraderApi 实例。
    static CThostFtdcTraderApi* create_td_api(
        const std::string& dll_dir,
        const std::string& dll_name,
        const std::string& flow_path);

    // 构造 MD DLL 完整路径（dll_name 为空时自动推断文件名）。
    static std::string resolve_md_dll(const std::string& dll_dir,
                                       const std::string& dll_name);

    // 构造 TD DLL 完整路径（dll_name 为空时自动推断文件名）。
    static std::string resolve_td_dll(const std::string& dll_dir,
                                       const std::string& dll_name);

private:
    // 加载动态库，返回 handle（Windows: HMODULE，Linux: void*）
    static void* open_lib(const std::string& path);

    // 从已加载库中获取符号地址
    static void* get_sym(void* handle, const char* name);

    // 当前平台的默认 MD/TD DLL 文件名
    static const char* default_md_dll_name();
    static const char* default_td_dll_name();

    // 路径拼接（处理尾部分隔符）
    static std::string join_path(const std::string& dir,
                                  const std::string& filename);
};
