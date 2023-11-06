#include <linux/module.h>
#include <net/tcp.h>
#include <net/transp_v6.h>

#define INIT_PACING_RATE 125000 // 1 Mbps
#define INIT_CWND_GAIN 20

#define MIN_PACING_RATE 62500 // 500 Kbps
#define MIN_CWND_GAIN 5
#define MAX_CWND_GAIN 80
#define MIN_CWND 4

#define PKT_INFO_SLOTS 5
#define MIN_PKT_INFO_SAMPLES 50
#define MIN_ACK_RATE 0.8

#define TCP_BRUTAL_PARAMS 23301

struct brutal_pkt_info
{
    u64 sec;
    u32 acked;
    u32 losses;
};

struct brutal
{
    u64 rate;
    u32 cwnd_gain;

    struct brutal_pkt_info slots[PKT_INFO_SLOTS];
};

struct brutal_params
{
    u64 rate;      // Send rate in bytes per second
    u32 cwnd_gain; // CWND gain in tenths (10=1.0)
} __packed;

static struct proto tcp_prot_override __ro_after_init;
static struct proto tcpv6_prot_override __ro_after_init;

#ifdef _LINUX_SOCKPTR_H
static int brutal_set_params(struct sock *sk, sockptr_t optval, unsigned int optlen)
#else
static int brutal_set_params(struct sock *sk, char __user *optval, unsigned int optlen)
#endif
{
    struct brutal *brutal = inet_csk_ca(sk);
    struct brutal_params params;

    if (optlen < sizeof(params))
        return -EINVAL;

#ifdef _LINUX_SOCKPTR_H
    if (copy_from_sockptr(&params, optval, sizeof(params)))
        return -EFAULT;
#else
    if (copy_from_user(&params, optval, sizeof(params)))
        return -EFAULT;
#endif

    // Sanity checks
    if (params.rate < MIN_PACING_RATE)
        return -EINVAL;
    if (params.cwnd_gain < MIN_CWND_GAIN || params.cwnd_gain > MAX_CWND_GAIN)
        return -EINVAL;

    brutal->rate = params.rate;
    brutal->cwnd_gain = params.cwnd_gain;

    return 0;
}

#ifdef _LINUX_SOCKPTR_H
static int brutal_tcp_setsockopt(struct sock *sk, int level, int optname, sockptr_t optval, unsigned int optlen)
#else
static int brutal_tcp_setsockopt(struct sock *sk, int level, int optname, char __user *optval, unsigned int optlen)
#endif
{
    if (level == IPPROTO_TCP && optname == TCP_BRUTAL_PARAMS)
        return brutal_set_params(sk, optval, optlen);
    else
        return tcp_prot.setsockopt(sk, level, optname, optval, optlen);
}

#ifdef _LINUX_SOCKPTR_H
static int brutal_tcpv6_setsockopt(struct sock *sk, int level, int optname, sockptr_t optval, unsigned int optlen)
#else
static int brutal_tcpv6_setsockopt(struct sock *sk, int level, int optname, char __user *optval, unsigned int optlen)
#endif
{
    if (level == IPPROTO_TCP && optname == TCP_BRUTAL_PARAMS)
        return brutal_set_params(sk, optval, optlen);
    else
        return tcpv6_prot.setsockopt(sk, level, optname, optval, optlen);
}

static void brutal_init(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    struct brutal *brutal = inet_csk_ca(sk);

    if (sk->sk_family == AF_INET)
        sk->sk_prot = &tcp_prot_override;
    else if (sk->sk_family == AF_INET6)
        sk->sk_prot = &tcpv6_prot_override;
    else
        BUG(); // WTF?

    tp->snd_ssthresh = TCP_INFINITE_SSTHRESH;

    brutal->rate = INIT_PACING_RATE;
    brutal->cwnd_gain = INIT_CWND_GAIN;

    memset(brutal->slots, 0, sizeof(brutal->slots));

    cmpxchg(&sk->sk_pacing_status, SK_PACING_NONE, SK_PACING_NEEDED);
}

// Copied from tcp.h for compatibility reasons
static inline u32 brutal_tcp_snd_cwnd(const struct tcp_sock *tp)
{
    return tp->snd_cwnd;
}

// Copied from tcp.h for compatibility reasons
static inline void brutal_tcp_snd_cwnd_set(struct tcp_sock *tp, u32 val)
{
    WARN_ON_ONCE((int)val <= 0);
    tp->snd_cwnd = val;
}

static void brutal_update_rate(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    struct brutal *brutal = inet_csk_ca(sk);

    u64 sec = tp->tcp_mstamp / USEC_PER_SEC;
    u64 min_sec = sec - PKT_INFO_SLOTS;
    u32 acked = 0, losses = 0;
    u32 ack_rate; // Scaled by 100 (100=1.00) as kernel doesn't support float
    u64 rate = brutal->rate;
    u32 cwnd;

    u32 mss = tp->mss_cache;
    u32 rtt_ms = (tp->srtt_us >> 3) / USEC_PER_MSEC;
    if (!rtt_ms)
        rtt_ms = 1;

    for (int i = 0; i < PKT_INFO_SLOTS; i++)
    {
        if (brutal->slots[i].sec >= min_sec)
        {
            acked += brutal->slots[i].acked;
            losses += brutal->slots[i].losses;
        }
    }
    if (acked + losses < MIN_PKT_INFO_SAMPLES)
        ack_rate = 100;
    else
    {
        ack_rate = acked * 100 / (acked + losses);
        if (ack_rate < MIN_ACK_RATE * 100)
            ack_rate = MIN_ACK_RATE * 100;
    }

    rate *= 100;
    rate /= ack_rate;

    // The order here is chosen carefully to avoid overflow as much as possible
    cwnd = rate / MSEC_PER_SEC;
    cwnd *= rtt_ms;
    cwnd /= mss;
    cwnd *= brutal->cwnd_gain;
    cwnd /= 10;
    cwnd = max_t(u32, cwnd, MIN_CWND);

    brutal_tcp_snd_cwnd_set(tp, min(cwnd, tp->snd_cwnd_clamp));

    WRITE_ONCE(sk->sk_pacing_rate, min_t(u64, rate, READ_ONCE(sk->sk_max_pacing_rate)));
}

static void brutal_main(struct sock *sk, const struct rate_sample *rs)
{
    struct tcp_sock *tp = tcp_sk(sk);
    struct brutal *brutal = inet_csk_ca(sk);

    u64 sec;
    u32 slot;

    // Ignore invalid rate samples
    if (rs->delivered < 0 || rs->interval_us <= 0)
        return;

    sec = tp->tcp_mstamp / USEC_PER_SEC;
    slot = sec % PKT_INFO_SLOTS;

    if (brutal->slots[slot].sec == sec)
    {
        // Current slot, update
        brutal->slots[slot].acked += rs->acked_sacked;
        brutal->slots[slot].losses += rs->losses;
    }
    else
    {
        // Uninitialized slot or slot expired
        brutal->slots[slot].sec = sec;
        brutal->slots[slot].acked = rs->acked_sacked;
        brutal->slots[slot].losses = rs->losses;
    }

    brutal_update_rate(sk);
}

static u32 brutal_undo_cwnd(struct sock *sk)
{
    return brutal_tcp_snd_cwnd(tcp_sk(sk));
}

static u32 brutal_ssthresh(struct sock *sk)
{
    return tcp_sk(sk)->snd_ssthresh;
}

static struct tcp_congestion_ops tcp_brutal_ops = {
    .flags = TCP_CONG_NON_RESTRICTED,
    .name = "brutal",
    .owner = THIS_MODULE,
    .init = brutal_init,
    .cong_control = brutal_main,
    .undo_cwnd = brutal_undo_cwnd,
    .ssthresh = brutal_ssthresh,
};

static int __init brutal_register(void)
{
    BUILD_BUG_ON(sizeof(struct brutal) > ICSK_CA_PRIV_SIZE);

    tcp_prot_override = tcp_prot;
    tcp_prot_override.setsockopt = brutal_tcp_setsockopt;
    tcpv6_prot_override = tcpv6_prot;
    tcpv6_prot_override.setsockopt = brutal_tcpv6_setsockopt;

    return tcp_register_congestion_control(&tcp_brutal_ops);
}

static void __exit brutal_unregister(void)
{
    tcp_unregister_congestion_control(&tcp_brutal_ops);
}

module_init(brutal_register);
module_exit(brutal_unregister);

MODULE_AUTHOR("Aperture Internet Laboratory");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Brutal");
MODULE_VERSION("1.0.0");
