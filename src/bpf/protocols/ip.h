#if !defined(PROTOCOLS_IP_H)
#define PROTOCOLS_IP_H

#include <linux/ip.h>
#include <types.h>
#include <linux/bpf.h>

static u32 ipv4_handle(struct xdp_md *ctx, struct iphdr *iph);
static u8 ip_inner_check_ipv4(struct xdp_md *ctx, struct iphdr *iph);

#endif // PROTOCOLS_IP_H
