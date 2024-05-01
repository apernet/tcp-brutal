# ![TCP Brutal](logo.png)

TCP Brutal is [Hysteria](https://hysteria.network/)'s congestion control algorithm ported to TCP, as a Linux kernel module. Information about Brutal itself can be found in the [Hysteria documentation](https://hysteria.network/docs/advanced/Full-Server-Config/#bandwidth-behavior-explained). As an official subproject of Hysteria, TCP Brutal is actively maintained to be in sync with the Brutal implementation in Hysteria.

**中文文档：[README.zh.md](README.zh.md)**

## For users

Installation script:

```bash
bash <(curl -fsSL https://tcp.hy2.sh/)
```

Manual compilation and loading:

```bash
# Make sure kernel headers are installed
# Ubuntu: apt install linux-headers-$(uname -r)
make && make load
```

> Kernel version 4.9 or later is required, version 5.8 or later is recommended. **If your kernel version is earlier than 5.8, only IPv4 is supported.** [(lack of exported symbol `tcpv6_prot`)](https://github.com/torvalds/linux/commit/6abde0b241224347cd88e2ae75902e07f55c42cb#diff-8b341e52e57c996bc4f294087ab526ac0b1c3c47e045557628cc24277cbfda0dR2124)
>
> **⚠️ Warning** For systems with kernel versions lower than 4.13, you MUST manually enable fq pacing (`tc qdisc add dev eth0 root fq pacing`), otherwise TCP Brutal will not work properly.

### Do I need a new proxy protocol?

No. TCP Brutal supports all existing TCP proxy protocols, **but requires support from both the client and server software** (to provide bandwidth options, exchange bandwidth information, etc.). Ask the developers of the proxy software you use to add support.

### Speed test

The [example](example) directory contains a simple speed test server+client in Python. Usage:

```bash
# Server, listening on TCP port 1234
python server.py -p 1234

# Client, connect to example.com:1234, request download speed of 50 Mbps
python client.py -p 1234 example.com 50
```

### Do I need to configure sysctl? / Can I set TCP Brutal as the system's default congestion control?

You don't need to, and shouldn't. Unlike BBR, TCP Brutal can only work properly if the program sets the bandwidth using a special sockopt, which most programs don't support unless otherwise specified. Setting it as the default congestion control would slow down all connections to 1 Mbps. Programs that do support it will actively switch to using TCP Brutal congestion control on their own.

## For developers

This kernel module adds a new "brutal" TCP congestion control algorithm to the system, which programs can enable using TCP_CONGESTION sockopt.

```python
s.setsockopt(socket.IPPROTO_TCP, TCP_CONGESTION, "brutal".encode())
```

To set the send rate and congestion window gain (we recommend a default value of 1.5x to 2x, which is expressed as 15/20 since the kernel doesn't support floating point):

```c
struct brutal_params
{
    u64 rate;      // Send rate in bytes per second
    u32 cwnd_gain; // CWND gain in tenths (10=1.0)
} __packed;
```

```python
TCP_BRUTAL_PARAMS = 23301

rate = 2000000 # 2 MB/s
cwnd_gain = 15
brutal_params_value = struct.pack("QI", rate, cwnd_gain)
conn.setsockopt(socket.IPPROTO_TCP, TCP_BRUTAL_PARAMS, brutal_params_value)
```

### For proxy developers (important)

Like Hysteria, Brutal is designed for environments where the user knows the bandwidth of their connection, as this information is essential for Brutal to work. While Hysteria's protocol is designed with this in mind, none of the existing TCP proxy protocols (at the time of this writing) have such a mechanism for exchanging bandwidth information between client and server, so that a client can tell the server how fast it should send and vice versa.

To work around this, we suggest using the "destination address" field, which every proxy protocol has in one form or another. Clients and servers supporting TCP Brutal can use a special address (e.g. `_BrutalBwExchange`) to indicate that they want to exchange bandwidth information. For example, the client can create a `_BrutalBwExchange` connection request and, if the server accepts, use that connection to exchange bandwidth information with the server.

The following link shows how this is implemented in sing-box:

<https://github.com/SagerNet/sing-mux/commit/6b086ed6bb0790160de73b16683e75efe2220a79>

An important aspect to understand about TCP Brutal's rate setting is that it applies to each individual connection. **This makes it suitable only for protocols that support multiplexing (mux), which allows a client to consolidate all proxy connections into a single TCP connection.** For protocols that require a separate connection for each proxy connection, using TCP Brutal will overwhelm the receiver if multiple connections are active at the same time.

### Compatibility

TCP Brutal is only a congestion control algorithm for TCP and does not alter the TCP protocol itself. Clients and servers can use TCP Brutal unilaterally. The congestion control algorithm controls the sending of data, and since proxy users typically download far more data than they upload, implementing TCP Brutal on the server side alone can reap most of the benefits. (Clients using TCP Brutal could achieve better upload speeds, but many users are on Windows, MacOS, or phones where installing kernel modules is impractical).
