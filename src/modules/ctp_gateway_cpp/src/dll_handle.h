#pragma once

#include <type_traits>

#if defined(_WIN32) || defined(__CYGWIN__)
    #include <windows.h>
    using DllHandleType = HMODULE;
    #define CTP_DLL_CLOSE FreeLibrary
#else
    #include <dlfcn.h>
    using DllHandleType = void*;
    #define CTP_DLL_CLOSE dlclose
#endif

/**
 * @brief RAII wrapper for dynamic library handles.
 *
 * Automatically closes the library handle on destruction.
 * Move-only semantics to prevent double-close and resource leaks.
 */
class DllHandle {
public:
    /**
     * @brief Default constructor creates an empty (invalid) handle.
     */
    DllHandle() noexcept : handle_(nullptr) {}

    /**
     * @brief Construct from an existing raw handle.
     * @param handle Raw library handle (may be nullptr for empty handle).
     */
    explicit DllHandle(DllHandleType handle) noexcept : handle_(handle) {}

    /**
     * @brief Construct from void* (for cross-platform compatibility).
     * @param handle Raw library handle as void*.
     */
    explicit DllHandle(void* handle) noexcept : handle_(static_cast<DllHandleType>(handle)) {}

    /**
     * @brief Destructor closes the library handle if valid.
     */
    ~DllHandle() noexcept {
        if (handle_ != nullptr) {
            CTP_DLL_CLOSE(handle_);
            handle_ = nullptr;
        }
    }

    // Non-copyable
    DllHandle(const DllHandle&) = delete;
    DllHandle& operator=(const DllHandle&) = delete;

    // Moveable
    DllHandle(DllHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    DllHandle& operator=(DllHandle&& other) noexcept {
        if (this != &other) {
            if (handle_ != nullptr) {
                CTP_DLL_CLOSE(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    /**
     * @brief Get the raw library handle.
     * @return Raw handle value.
     */
    DllHandleType get() const noexcept { return handle_; }

    /**
     * @brief Check if the handle is valid (non-null).
     * @return true if handle is valid, false otherwise.
     */
    explicit operator bool() const noexcept { return handle_ != nullptr; }

    /**
     * @brief Release ownership of the handle without closing it.
     * @return The raw handle value; the wrapper becomes empty.
     */
    DllHandleType release() noexcept {
        DllHandleType h = handle_;
        handle_ = nullptr;
        return h;
    }

    /**
     * @brief Reset to a new handle, closing any previously owned handle.
     * @param handle New handle to take ownership of.
     */
    void reset(DllHandleType handle = nullptr) noexcept {
        if (handle_ != nullptr && handle_ != handle) {
            CTP_DLL_CLOSE(handle_);
        }
        handle_ = handle;
    }

private:
    DllHandleType handle_;
};