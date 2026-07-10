# CTP Gateway CPP

Tyche CTP 网关 C++ 模块 - 连接中国期货市场CTP接口的C++网关实现。

## 概述

本模块提供：
- CTP MdApi/TdApi 双通道网关
- 行情数据接收与转发（通过ZMQ和SHM）
- 交易指令发送与回报处理
- CTP API 动态加载（支持CTP/CTPOPT/TTS）
- 可执行文件和DLL两种运行模式

## 前置依赖

- **TycheCore-CPP**: Tyche引擎C++核心库
- **CTP SDK**: 由CTP官方提供的API头文件和库
- **ZeroMQ**: 通信库（通过TycheCore-CPP间接依赖）

## 构建

```bash
# 添加 TycheCore-CPP 子模块
git submodule add https://github.com/zt281/TycheCore-CPP.git third_party/TycheCore-CPP

# 构建
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## 运行模式

### 独立进程模式
```bash
./build/ctp_gateway_cpp --config config/simttsfut.json
```

### DLL模式（由引擎加载）
将 `ctp_gateway_cpp.dll` 放入引擎的 `modules/` 目录。

## License

MIT
