# ![TCP Brutal](logo.png)

TCP Brutal 是 [Hysteria](https://hysteria.network/) 中的同名拥塞控制算法移植到 TCP 的版本，作为一个 Linux 内核模块。关于 Brutal 本身的信息，可以在 [Hysteria 文档](https://hysteria.network/zh/docs/advanced/Full-Server-Config/#_6)中找到。作为 Hysteria 官方子项目，TCP Brutal 会保持与 Hysteria 中的 Brutal 同步更新。

## 用户指南

安装脚本：

```bash
bash <(curl -fsSL https://tcp.hy2.sh/)
```

手动编译并加载：

```bash
# 确保安装了内核头文件
# Ubuntu: apt install linux-headers-$(uname -r)
make && make load
```

> 需要内核版本 4.9 或以上，推荐使用 5.8 以上的内核。**对于小于 5.8 的内核, 只支持 IPv4。** [(缺导出符号 `tcpv6_prot`)](https://github.com/torvalds/linux/commit/6abde0b241224347cd88e2ae75902e07f55c42cb#diff-8b341e52e57c996bc4f294087ab526ac0b1c3c47e045557628cc24277cbfda0dR2124)
>
> **⚠️ 注意** 对于内核版本低于 4.13 的系统，必须手动开启 fq pacing (`tc qdisc add dev eth0 root fq pacing`) 否则 TCP Brutal 无法正常工作。

### 需要新协议吗？

不需要。TCP Brutal 支持一切已有的 TCP 代理协议，**但是需要代理客户端和服务端软件的支持**（以提供带宽设置选项，交换带宽信息等）。请向你使用的代理软件的作者请求适配。

### 测速

[example](example) 目录中提供了一个 Python 的简单测速服务端+客户端。使用方法：

```bash
# 服务端，监听在 TCP 1234 端口
python server.py -p 1234

# 客户端，连接到 example.com:1234，请求下载速度 50 Mbps
python client.py -p 1234 example.com 50
```

### 需要配置 sysctl 吗？ / 能把 TCP Brutal 设置成系统默认的拥塞控制吗？

不需要也不能。与 BBR 不同，TCP Brutal 仅在应用程序对每个 TCP 连接设置带宽参数之后才能正常工作，绝大部分应用程序都不支持这个操作，将 TCP Brutal 设置成默认拥塞控制只会让系统的所有 TCP 连接降速到 1 Mbps。支持的应用程序会主动配置 TCP 连接使用 TCP Brutal 拥塞控制。

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

与 Hysteria 一样，Brutal 需要用户知道自己所处网络环境的带宽上限是多少。Hysteria 的协议从设计上就考虑了这一点，但目前现有的 TCP 代理协议中没有一个有在客户端与服务端之间交换带宽信息的机制，因此客户端无法告知服务端应该以多快的速度发送数据，反之亦然。

为了解决这个问题，我们建议利用所有代理协议中都存在的 "目标地址" 字段。支持 TCP Brutal 的客户端和服务端可以使用一个特殊的地址（例如 `_BrutalBwExchange`）来表示他们希望交换带宽信息。例如，客户端可以发起一个 `_BrutalBwExchange` 连接请求，如果服务端接受，就通过这个连接和服务端交换各自的带宽信息。

以下链接是 sing-box 的实现：

<https://github.com/SagerNet/sing-mux/commit/6b086ed6bb0790160de73b16683e75efe2220a79>

另外需要注意的是，sockopt 设置的是每个连接的速度。**这意味着其只适用于支持多路复用（mux）的协议，因为多路复用让客户端可以将所有代理连接整合到一个 TCP 连接中传输。** 对于需要为每个代理连接单独建立连接的协议，当同时有多个连接活跃时，使用 TCP Brutal 会导致总发送速率超过所设置的上限。

### 兼容性

TCP Brutal 只是 TCP 的拥塞控制算法，并不修改 TCP 协议本身。客户端和服务端可以只有单边安装内核模块。拥塞控制算法控制的是数据的发送，而考虑到代理用户通常下载的数据量远大于上传，只在服务端使用 TCP Brutal 就可以获得大部分的收益。（客户端使用 TCP Brutal 可以获得更好的上传速度，但很多人使用的是 Windows, macOS 或手机，安装内核模块往往不现实。）
