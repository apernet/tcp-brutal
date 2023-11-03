# ![TCP Brutal](logo.png)

TCP Brutal 是 [Hysteria](https://hysteria.network/) 中的同名拥塞控制算法移植到 TCP 的版本。关于 Brutal 本身的信息，可以在 [Hysteria 文档](https://hysteria.network/zh/docs/advanced/Full-Server-Config/#_6)中找到。作为 Hysteria 官方子项目，TCP Brutal 会保持与 Hysteria 中的 Brutal 同步更新。

## 用户指南

TODO：安装指南

## 开发者指南

该内核模块向系统添加了一个新的 "brutal" TCP 拥塞控制算法，程序可以使用 TCP_CONGESTION sockopt 来启用。

```python
s.setsockopt(socket.IPPROTO_TCP, TCP_CONGESTION, "brutal".encode())
```

设置发送速率和 CWND 增益（推荐默认值为 1.5 倍到 2 倍，需要表达为 15/20，因为内核不支持浮点数）：

```c
struct brutal_params
{
    u64 rate;      // 发送速率，以每秒字节数计
    u32 cwnd_gain; // CWND 增益，以十分之一为单位（10=1.0）
} __packed;
```

```python
TCP_BRUTAL_PARAMS = 23301

rate = 2000000 # 2 MB/s
cwnd_gain = 15
brutal_params_value = struct.pack("QI", rate, cwnd_gain)
conn.setsockopt(socket.IPPROTO_TCP, TCP_BRUTAL_PARAMS, brutal_params_value)
```

### 代理开发者须知（重要）

与 Hysteria 中一样，Brutal 需要用户知道自己所处网络环境的带宽上限是多少。Hysteria 的协议从设计上就考虑了这一点，但目前现有的 TCP 代理协议中没有一个有在客户端与服务端之间交换带宽信息的机制，因此客户端无法告知服务端应该以多快的速度发送数据，反之亦然。

为了解决这个问题，我们建议利用所有代理协议中都存在的 目标地址 字段。支持 TCP Brutal 的客户端和服务端可以使用一个特殊的地址（例如 `_BrutalBwExchange`）来表示他们希望交换带宽信息。例如，客户端可以创建一个 `_BrutalBwExchange` 连接请求，如果服务端接受，就可以通过这个连接发送其带宽信息并接收服务端的带宽信息。

以下链接是 sing-box 的实现：

<https://github.com/SagerNet/sing-mux/commit/a36b95857a9be5cd3c9c0cfbdbec376af270a180>

另外需要注意的是，TCP Brutal 的速率设置是连接层面的。**这意味着其只适用于支持多路复用（mux）的协议，因为多路复用让客户端可以将所有代理连接整合到一个 TCP 连接中传输。** 对于需要为每个代理连接单独建立连接的协议，当同时有多个连接活跃时，使用 TCP Brutal 会导致累计发送速率远超过客户端的带宽上限。

### 关于兼容性

TCP Brutal 只是 TCP 的拥塞控制算法，并没有修改 TCP 协议本身，因此不会影响与其他 TCP 实现的兼容性。换句话说，客户端和服务端可以单边使用 TCP Brutal。拥塞控制算法控制的是数据的发送，而考虑到代理用户通常下载的数据量远大于上传，只在服务端使用 TCP Brutal 就可以获得大部分的收益。（客户端使用 TCP Brutal 可以获得更好的上传速度，但很多人使用的是 Windows, macOS 或手机，安装内核模块往往不现实。）
