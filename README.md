# ![TCP Brutal](logo.png)

TCP Brutal is [Hysteria](https://hysteria.network/)'s congestion control algorithm ported to TCP. Information about Brutal itself can be found in the [Hysteria documentation](https://hysteria.network/docs/advanced/Full-Server-Config/#bandwidth-behavior-explained). As an official subproject of Hysteria, TCP Brutal is actively maintained to be in sync with the Brutal implementation in Hysteria.

**中文文档：[README.zh.md](README.zh.md)**

## For users

TODO: Installation instructions

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

To work around this, we suggest using the destination address field, which every proxy protocol has in one form or another. Clients and servers supporting TCP Brutal can use a special address (e.g. `_BrutalBwExchange`) to indicate that they want to exchange bandwidth information. For example, the client can create a `_BrutalBwExchange` connection request and, if the server accepts, send its bandwidth information and also receive the server's bandwidth information over that connection.

The following link shows how this is implemented in sing-box:

<https://github.com/SagerNet/sing-mux/commit/a36b95857a9be5cd3c9c0cfbdbec376af270a180>

An important aspect to understand about TCP Brutal's rate setting is that it applies to each individual connection. **This makes it suitable only for protocols that support multiplexing (mux), which allows a client to consolidate all proxy connections into a single TCP connection.** For protocols that require a separate connection for each proxy connection, using TCP Brutal on each connection can cause the cumulative send rate to significantly exceed the client's capacity when multiple connections are active at the same time.
