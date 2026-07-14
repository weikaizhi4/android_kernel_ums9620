/*
 *This program is used for get the uid and ip infomation.
 */
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/version.h>
#include <linux/tick.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>
#include <asm/suspend.h>
#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/system_misc.h>
#include <linux/seq_file.h>
#include <linux/fb.h>
#include <linux/version.h>
#include <linux/syscore_ops.h>
#include <linux/irqchip/arm-gic-v3.h>

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>

#include <trace/events/power.h>
//zsw added begin
#include <trace/events/sock.h>
#include <net/sock.h>
#include <linux/in.h>
#include <linux/socket.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/rtc.h>
#include <linux/fb.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>

#define TCP_IP_LOG_ENABLE          0x00000001
#define TCP_IPV6_LOG_ENABLE        0x00000002
#define TCP_IP_LOG_REDUCED  3
#define TRANS_TYPE_TCP 0
#define TRANS_TYPE_UDP 1
#define TRANS_TYPE_ICMP 2
#define DATA_DIRECT_SEND 0
#define DATA_DIRECT_RECV 1
static char* TRANS_TYPE_STR[3] = {"[TCP]", "[UDP]", "[ICMP]"};
static char* DATA_DIRECT_STR[2] = {"[SEND]", "[RECV]"};
static char* DATA_DIRECT_ARROW[2] = {"-->", "<--"};

//::0
#define IN6_IS_ADDR_UNSPECIFIED(a) \
  ((((a)->s6_addr32[0]) == 0) && \
   (((a)->s6_addr32[1]) == 0) && \
   (((a)->s6_addr32[2]) == 0) && \
   (((a)->s6_addr32[3]) == 0))

//::1
#define IN6_IS_ADDR_LOOPBACK(a) \
  ((((a)->s6_addr32[0]) == 0) && \
   (((a)->s6_addr32[1]) == 0) && \
   (((a)->s6_addr32[2]) == 0) && \
   (((a)->s6_addr32[3]) == htonl(1)))

//::ffff:127.0.0.1
#define IN6_IS_ADDR_V4MAPPED_LOOPBACK(a) \
  ((((a)->s6_addr32[0]) == 0) && \
   (((a)->s6_addr32[1]) == 0) && \
   (((a)->s6_addr32[2]) == htonl(0x0000ffff)) && \
   (((a)->s6_addr32[3] & 0xff000000) == htonl(0x0000007f)))
static int tcp_output_debug = 0;
static bool gIsNfNetHookRegistered = false;
static int glastUid = 0;
static int glastPort = 0;
static int glastDataDirect = 0;
static long glastTime = 0;
static int gpacketCount = 0;
#define PACKET_PRINT_TIME_THRESHOLD 2
#define PACKET_PRINT_COUNT_THRESHOLD 4
//zsw added end


static int zte_pm_debug_probe(struct platform_device *pdev)
{
	return 0;
}
static int zte_pm_debug_suspend(struct device *dev)
{
	return 0;
}

static int zte_pm_debug_resume(struct device *dev)
{
	return 0;
}
static int  zte_pm_debug_remove(struct platform_device *pdev)
{
	return 0;
}
static const struct of_device_id zte_pm_debug_table[] = {
	{.compatible = "zte_tcpip_debug_vendor"},
	{},
};
static const struct dev_pm_ops zte_pm_debug_ops = {
	.suspend	= zte_pm_debug_suspend,
	.resume		= zte_pm_debug_resume,
};
static struct platform_driver zte_pm_debug_driver = {
	.probe = zte_pm_debug_probe,
	.remove	= zte_pm_debug_remove,
	.driver = {
		.name = "zte_tcpip_debug_vendor",
		.owner = THIS_MODULE,
		.pm	= &zte_pm_debug_ops,
		.of_match_table = zte_pm_debug_table,
	},
};
//zsw added
static bool can_print_packet(int uid, int port,  int data_direct)
{
	long curTime;
	if (tcp_output_debug == TCP_IP_LOG_REDUCED) {
		curTime = ktime_get_real_seconds();
		if (uid == glastUid || uid == 0 || uid == 1000) {
			if (curTime - glastTime < PACKET_PRINT_TIME_THRESHOLD) {
				if (port == glastPort && data_direct == glastDataDirect) {
					return false;
				}
				glastPort = port;
				glastDataDirect = data_direct;
				++ gpacketCount;
				if (gpacketCount > PACKET_PRINT_COUNT_THRESHOLD - 1) {
					return false;
				}
			} else {
				glastPort = 0;
				glastDataDirect = 0;
				glastTime = curTime;
				gpacketCount = 0;
			}
		} else {
			glastUid = uid;
			glastPort = 0;
			glastDataDirect = 0;
			glastTime = curTime;
			gpacketCount = 0;
		}
	}
	return true;
}


static unsigned int packet_v4_filter(void *priv, struct sk_buff *skb, const struct nf_hook_state *state, int data_direct)
{
	if (!skb) {
		return NF_ACCEPT;
	} else {
		struct iphdr *iph;
		struct tcphdr *tcph;
		struct udphdr *udph;
		struct icmphdr *icmph;
		__be32 *local_addr;
		__be32 *remote_addr;
		int local_port = 0;
		int remote_port = 0;
		int uid = 0;
		if ((tcp_output_debug & TCP_IP_LOG_ENABLE) == 0) {
			return NF_ACCEPT;
		}

		/*if (state != NULL && state->sk != NULL) {
			uid = state->sk->sk_uid.val;
		}*/
          
		iph = ip_hdr(skb);
		if (iph == NULL) {
			return NF_ACCEPT;
		}

		if (data_direct == DATA_DIRECT_SEND) {
			local_addr = &iph->saddr;
			remote_addr = &iph->daddr;
		} else {
			local_addr = &iph->daddr;
			remote_addr = &iph->saddr;
		}

		if (local_addr == NULL || remote_addr == NULL) {
			return NF_ACCEPT;
		}

		if (ipv4_is_loopback(*remote_addr) || (*remote_addr ==  htonl(INADDR_ANY))) {
			return NF_ACCEPT;
		}

		if (iph->protocol == IPPROTO_TCP) {
                	if (skb->sk != NULL) {
			    uid = skb->sk->sk_uid.val;
		        }
			tcph = tcp_hdr(skb);
			if (tcph == NULL) {
				return NF_ACCEPT;
			}
			if (data_direct == DATA_DIRECT_SEND) {
				local_port = tcph->source;
				remote_port = tcph->dest;
			} else {
				local_port = tcph->dest;
				remote_port = tcph->source;
			}
			if (!can_print_packet(uid, ntohs(remote_port), data_direct)) {
				return NF_ACCEPT;
			}
			pr_info("[IP] %s %s uid = %d, len=%d, (%pI4:%hu %s %pI4:%hu)\n",
				TRANS_TYPE_STR[TRANS_TYPE_TCP], DATA_DIRECT_STR[data_direct],
				uid, ntohs(iph->tot_len),
				local_addr, ntohs(local_port),
				DATA_DIRECT_ARROW[data_direct],
				remote_addr, ntohs(remote_port));
		} else if (iph->protocol == IPPROTO_UDP) {
                	if (skb->sk != NULL) {
			    uid = skb->sk->sk_uid.val;
		        }
			udph = udp_hdr(skb);
			if (udph == NULL) {
				return NF_ACCEPT;
			}
			if (data_direct == DATA_DIRECT_SEND) {
				local_port = udph->source;
				remote_port = udph->dest;
			} else {
				local_port = udph->dest;
				remote_port = udph->source;
			}
			if (!can_print_packet(uid, ntohs(remote_port), data_direct)) {
				return NF_ACCEPT;
			}
			pr_info("[IP] %s %s uid = %d, len=%d, (%pI4:%hu %s %pI4:%hu)\n",
				TRANS_TYPE_STR[TRANS_TYPE_UDP], DATA_DIRECT_STR[data_direct],
				uid, ntohs(iph->tot_len),
				local_addr, ntohs(local_port),
				DATA_DIRECT_ARROW[data_direct],
				remote_addr, ntohs(remote_port));
		} else if (iph->protocol == IPPROTO_ICMP) {
                	if (skb->sk != NULL) {
			    uid = skb->sk->sk_uid.val;
		        }
			icmph = icmp_hdr(skb);
			if (icmph == NULL) {
				return NF_ACCEPT;
			}
			pr_info("[IP] %s %s uid = %d, len=%d, (%pI4 %s %pI4), T: %u, C: %u\n",
				TRANS_TYPE_STR[TRANS_TYPE_ICMP], DATA_DIRECT_STR[data_direct],
				uid, ntohs(iph->tot_len),
				local_addr, DATA_DIRECT_ARROW[data_direct], remote_addr,
				icmph->type, icmph->code);
		} else {
			pr_info("[IP] %s, len=%d, (%pI4 %s %pI4)\n",
				DATA_DIRECT_STR[data_direct],
				ntohs(iph->tot_len),
				local_addr, DATA_DIRECT_ARROW[data_direct], remote_addr);
		}
		return NF_ACCEPT;
	}
}


static unsigned int packet_v6_filter(void *priv, struct sk_buff *skb, const struct nf_hook_state *state, int data_direct)
{
	if (!skb) {
		return NF_ACCEPT;
	} else {
		struct ipv6hdr *iph;
		struct tcphdr *tcph;
		struct udphdr *udph;
		struct icmp6hdr *icmph;
		struct in6_addr *local_addr;
		struct in6_addr *remote_addr;
		int local_port = 0;
		int remote_port = 0;
		int uid = 0;

		if ((tcp_output_debug & TCP_IPV6_LOG_ENABLE) == 0) {
			return NF_ACCEPT;
		}

		/*if (state != NULL && state->sk != NULL) {
			uid = state->sk->sk_uid.val;
		}*/

		iph = ipv6_hdr(skb);
		if (iph == NULL) {
			return NF_ACCEPT;
		}

		if (data_direct == DATA_DIRECT_SEND) {
			local_addr = &iph->saddr;
			remote_addr = &iph->daddr;
		} else {
			local_addr = &iph->daddr;
			remote_addr = &iph->saddr;
		}

		if (local_addr == NULL || remote_addr == NULL) {
			return NF_ACCEPT;
		}

		if (IN6_IS_ADDR_UNSPECIFIED(remote_addr)
			|| IN6_IS_ADDR_LOOPBACK(remote_addr)
			|| IN6_IS_ADDR_V4MAPPED_LOOPBACK(remote_addr)) {
			return NF_ACCEPT;
		}

		if (iph->nexthdr == IPPROTO_TCP) {
                	if (skb->sk != NULL) {
			    uid = skb->sk->sk_uid.val;
		        }                  
			tcph = tcp_hdr(skb);
			if (tcph == NULL) {
				return NF_ACCEPT;
			}
			if (data_direct == DATA_DIRECT_SEND) {
				local_port = tcph->source;
				remote_port = tcph->dest;
			} else {
				local_port = tcph->dest;
				remote_port = tcph->source;
			}
			if (!can_print_packet(uid, ntohs(remote_port), data_direct)) {
				return NF_ACCEPT;
			}
			pr_info("[IPV6] %s %s uid = %d, len=%d, (%pI6:%hu %s %pI6:%hu)\n",
				TRANS_TYPE_STR[TRANS_TYPE_TCP], DATA_DIRECT_STR[data_direct],
				uid, ntohs(iph->payload_len),
				local_addr, ntohs(local_port),
				DATA_DIRECT_ARROW[data_direct],
				remote_addr, ntohs(remote_port));
		} else if (iph->nexthdr == IPPROTO_UDP) {
                	if (skb->sk != NULL) {
			    uid = skb->sk->sk_uid.val;
		        }
			udph = udp_hdr(skb);
			if (udph == NULL) {
				return NF_ACCEPT;
			}
			if (data_direct == DATA_DIRECT_SEND) {
				local_port = udph->source;
				remote_port = udph->dest;
			} else {
				local_port = udph->dest;
				remote_port = udph->source;
			}
			if (!can_print_packet(uid, ntohs(remote_port), data_direct)) {
				return NF_ACCEPT;
			}
			pr_info("[IPV6] %s %s uid = %d, len=%d, (%pI6:%hu %s %pI6:%hu)\n",
				TRANS_TYPE_STR[TRANS_TYPE_UDP], DATA_DIRECT_STR[data_direct],
				uid, ntohs(iph->payload_len),
				local_addr, ntohs(local_port),
				DATA_DIRECT_ARROW[data_direct],
				remote_addr, ntohs(remote_port));
		} else if (iph->nexthdr == IPPROTO_ICMPV6) {
                	if (skb->sk != NULL) {
			    uid = skb->sk->sk_uid.val;
		        }
			icmph = icmp6_hdr(skb);
			if (icmph == NULL) {
				return NF_ACCEPT;
			}
			pr_info("[IPV6] %s %s uid = %d, len=%d, (%pI6 %s %pI6), T: %u, C: %u\n",
				TRANS_TYPE_STR[TRANS_TYPE_ICMP], DATA_DIRECT_STR[data_direct],
				uid, ntohs(iph->payload_len),
				local_addr, DATA_DIRECT_ARROW[data_direct], remote_addr,
				icmph->icmp6_type, icmph->icmp6_code);
		} else {
			pr_info("[IPV6] %s, len=%d, (%pI6 %s %pI6)\n",
				DATA_DIRECT_STR[data_direct],
				ntohs(iph->payload_len),
				local_addr, DATA_DIRECT_ARROW[data_direct], remote_addr);
		}
		return NF_ACCEPT;
	}
}

static unsigned int packet_v4_in_filter(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
    return packet_v4_filter (priv, skb, state, DATA_DIRECT_RECV);
}

static unsigned int packet_v6_in_filter(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
    return packet_v6_filter (priv, skb, state, DATA_DIRECT_RECV);
}

static unsigned int packet_v4_out_filter(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
    return packet_v4_filter (priv, skb, state, DATA_DIRECT_SEND);
}

static unsigned int packet_v6_out_filter(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
    return packet_v6_filter (priv, skb, state, DATA_DIRECT_SEND);
}

static const struct nf_hook_ops packet_in_nf_ops[] = {
    {
        .hook = (nf_hookfn *)packet_v4_in_filter,
        .pf = NFPROTO_IPV4,
        .hooknum = NF_INET_LOCAL_IN,
        .priority = NF_IP_PRI_FIRST,
    },
    {
        .hook = (nf_hookfn *)packet_v6_in_filter,
        .pf = NFPROTO_IPV6,
        .hooknum = NF_INET_LOCAL_IN,
        .priority = NF_IP_PRI_FIRST,
    },
};

static const struct nf_hook_ops packet_out_nf_ops[] = {
    {
        .hook = (nf_hookfn *)packet_v4_out_filter,
        .pf = NFPROTO_IPV4,
        .hooknum = NF_INET_LOCAL_OUT,
        .priority = NF_IP_PRI_FIRST,
    },
    {
        .hook = (nf_hookfn *)packet_v6_out_filter,
        .pf = NFPROTO_IPV6,
        .hooknum = NF_INET_LOCAL_OUT,
        .priority = NF_IP_PRI_FIRST,
    },
};
int __init zte_pm_debug_vendor_init(void)
{
	static bool registered;

	if (registered)
		return 0;
	registered = true;
    //zsw added
	if (tcp_output_debug != 0) {
		if (!gIsNfNetHookRegistered) {
			nf_register_net_hooks(&init_net, packet_in_nf_ops,  ARRAY_SIZE(packet_in_nf_ops));
			nf_register_net_hooks(&init_net, packet_out_nf_ops, ARRAY_SIZE(packet_out_nf_ops));
			gIsNfNetHookRegistered = true;
		}
	}
	return platform_driver_register(&zte_pm_debug_driver);
}
int notify_tcp_param_change(const char *val, const struct kernel_param *kp)
{
        int res = param_set_int(val, kp); // Use helper for write variable
        if(res == 0) {
			if (tcp_output_debug != 0) {
				pr_info("tcp debug enable, call nf_register_net_hooks");
				if (!gIsNfNetHookRegistered) {
					nf_register_net_hooks(&init_net, packet_in_nf_ops,  ARRAY_SIZE(packet_in_nf_ops));
					nf_register_net_hooks(&init_net, packet_out_nf_ops, ARRAY_SIZE(packet_out_nf_ops));
					gIsNfNetHookRegistered = true;
				}
			} else {
				pr_info("tcp debug disable, call nf_unregister_net_hooks");
				if (gIsNfNetHookRegistered) {
					nf_unregister_net_hooks(&init_net, packet_in_nf_ops,  ARRAY_SIZE(packet_in_nf_ops));
					nf_unregister_net_hooks(&init_net, packet_out_nf_ops, ARRAY_SIZE(packet_out_nf_ops));
					gIsNfNetHookRegistered = false;
				}
			}
			return 0;
        }
        return -1;
}
const struct kernel_param_ops tcp_param_ops =
{
        .set = &notify_tcp_param_change,
        .get = &param_get_int,
};
module_param_cb(tcp_output_debug, &tcp_param_ops, &tcp_output_debug, S_IRUGO | S_IWUSR | S_IWGRP);
late_initcall(zte_pm_debug_vendor_init);
static void __exit zte_pm_debug_vendor_exit(void)
{
	platform_driver_unregister(&zte_pm_debug_driver);
}


module_exit(zte_pm_debug_vendor_exit);
