#include <protocols/eth.h>
#include <protocols/ip.h>
#include <protocols/udp.h>
#include <protocols/gtp.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <bpf_helpers.h>
#include <endian.h>
#include <types.h>
#include <utils/logger.h>
#include <utils/utils.h>

/////////////////////////////////////////////////////////////////////////
///////////////////// GTP FUNCTIONS /////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

static u32 gtp_handle(struct xdp_md *ctx, struct gtpuhdr *gtpuh)
{
  void *data_end = (void *)(long)ctx->data_end;
  struct iphdr* iph;
  u8 ret;

  if (gtpuh + 1 > data_end)
  {
    bpf_debug("Invalid GTPU packet\n");
    return XDP_DROP;
  }
  if (gtpuh->message_type != GTPU_G_PDU)
    bpf_debug("Message type 0x%x is not GTPU GPDU(0x%x)\n", gtpuh->message_type, GTPU_G_PDU);

  bpf_debug("GTP GPDU received\n");
  // ret = ip_inner_check_ipv4(ctx, iph);
  if(!ip_inner_check_ipv4(ctx, (struct iphdr* ) (gtpuh + 1)))
    return XDP_DROP;

  bpf_debug("GTP GPDU with IPv4 payload received\n");
  return XDP_PASS;
}

/////////////////////////////////////////////////////////////////////////
////////////////////// UDP FUNCTIONS ////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
static u32 udp_handle(struct xdp_md *ctx, struct udphdr *udph)
{
  void *data_end = (void *)(long)ctx->data_end;
  u32 dport;
  /* Hint: +1 is sizeof(struct udphdr) */
  if (udph + 1 > data_end)
  {
    bpf_debug("Invalid UDP packet\n");
    return XDP_ABORTED;
  }

  bpf_debug("UDP packet validated\n");
  dport = htons(udph->dest);
  switch (dport)
  {
  case GTP_UDP_PORT:
    return gtp_handle(ctx, (struct gtpuhdr *)(udph + 1));
  default:
    bpf_debug("GTP port %lu not valid\n", dport);
    return XDP_PASS;
  }
}

/////////////////////////////////////////////////////////////////////////
////////////////////// IP FUNCTIONS /////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
static u32 ipv4_handle(struct xdp_md *ctx, struct iphdr *iph)
{
  void *data_end = (void *)(long)ctx->data_end;
  // Type need to match map.
  u32 ip_dest;

  // Hint: +1 is sizeof(struct iphdr)
  if (iph + 1 > data_end)
  {
    bpf_debug("Invalid IPv4 packet\n");
    return XDP_ABORTED;
  }
  ip_dest = iph->daddr;

  bpf_debug("Valid IPv4 packet: raw daddr:0x%x\n", ip_dest);
  switch (iph->protocol)
  {
  case IPPROTO_UDP:
    return udp_handle(ctx, (struct udphdr *)(iph + 1));
  case IPPROTO_TCP:
  default:
    return XDP_PASS;
  }
}

static u8 ip_inner_check_ipv4(struct xdp_md *ctx, struct iphdr *iph)
{
  void *data_end = (void *)(long)ctx->data_end;
  // Type need to match map.
  u32 ip_dest;

  // Hint: +1 is sizeof(struct iphdr)
  if (iph + 1 > data_end)
  {
    bpf_debug("Invalid IPv4 packet\n");
    return XDP_ABORTED;
  }

  return iph->version == 4;
}

/////////////////////////////////////////////////////////////////////////
///////////////////// ETH FUNCTIONS /////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
struct vlan_hdr
{
  __be16 h_vlan_TCI;
  __be16 h_vlan_encapsulated_proto;
};

/* 
 * Parse Ethernet layer 2, extract network layer 3 offset and protocol
 * Call next protocol handler (e.g. ipv4).
 * Returns The XDP action.
 */
static u32 eth_handle(struct xdp_md *ctx, struct ethhdr *ethh)
{
  void *data_end = (void *)(long)ctx->data_end;
  u16 eth_type;
  u64 offset;
  struct vlan_hdr *vlan_hdr;

  offset = sizeof(*ethh);
  if ((void *)ethh + offset > data_end)
  {
    bpf_debug("Cannot parse L2\n");
    return XDP_PASS;
  }

  eth_type = htons(ethh->h_proto);
  bpf_debug("Debug: eth_type:0x%x\n", eth_type);

  switch (eth_type)
  {
  case ETH_P_8021Q:
  case ETH_P_8021AD:
    vlan_hdr = (void *)ethh + offset;
    offset += sizeof(*vlan_hdr);
    if (!((void *)ethh + offset > data_end))
      eth_type = htons(vlan_hdr->h_vlan_encapsulated_proto);
  case ETH_P_IP:
    return ipv4_handle(ctx, (struct iphdr *)((void *)ethh + offset));
  case ETH_P_IPV6:
  // Skip non 802.3 Ethertypes
  case ETH_P_ARP:
  // Skip non 802.3 Ethertypes
  // Fall-through
  default:
    bpf_debug("Cannot parse L2: L3off:%llu proto:0x%x\n", offset, eth_type);
    return XDP_PASS;
  }
}

/////////////////////////////////////////////////////////////////////////
///////////////////// SECTION FUNCTIONS //////////////////////////////////
/////////////////////////////////////////////////////////////////////////
SEC("gtp_process_chain")
int xdp_prog1(struct xdp_md *ctx)
{
  void *data = (void *)(long)ctx->data;
  struct ethhdr *eth = data;

  return eth_handle(ctx, eth);
}

char _license[] SEC("license") = "GPL";