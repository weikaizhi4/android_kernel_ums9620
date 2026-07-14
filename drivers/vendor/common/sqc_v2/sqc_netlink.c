#define pr_fmt(fmt)	"SQC_NOTIFY: %s: " fmt, __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <net/sock.h>
#include <linux/netlink.h>

#include "sqc_netlink.h"


#define NETLINK_TEST     30
#define MSG_LEN          125
#define USER_PORT        8106

struct sqc_netlink_msg {
	int chg_id;
	int msg_type;
	int msg_value;
};

struct sock *nlsk = NULL;
extern struct net init_net;

int sqc_notify_daemon_changed(int chg_id, int msg_type, int msg_val)
{
	struct sk_buff *nl_skb = NULL;
	struct nlmsghdr *nlh = NULL;
	struct sqc_netlink_msg sqc_msg = {0, };
	int ret = 0;

	/* 创建sk_buff 空间 */
	nl_skb = nlmsg_new(sizeof(struct sqc_netlink_msg), GFP_ATOMIC);
	if (!nl_skb) {
		pr_info("netlink alloc failure\n");
		return -1;
	}

	/* 设置netlink消息头部 */
	nlh = nlmsg_put(nl_skb, 0, 0, NETLINK_TEST, sizeof(struct sqc_netlink_msg), 0);
	if (nlh == NULL) {
		pr_info("nlmsg_put failaure \n");
		nlmsg_free(nl_skb);
		return -1;
	}

	/* 拷贝数据发送 */
	sqc_msg.chg_id = chg_id;
	sqc_msg.msg_type = msg_type;
	sqc_msg.msg_value = msg_val;
	memcpy(nlmsg_data(nlh), &sqc_msg, sizeof(struct sqc_netlink_msg));

	ret = netlink_unicast(nlsk, nl_skb, USER_PORT, MSG_DONTWAIT);

	/*nlmsg_free(nl_skb);*/

	pr_info("netlink send ok\n");

	return ret;
}
EXPORT_SYMBOL_GPL(sqc_notify_daemon_changed);

static void netlink_rcv_msg(struct sk_buff *skb)
{
	struct nlmsghdr *nlh = NULL;
	char *umsg = NULL;
	struct sqc_netlink_msg sqc_kmsg = {0, };

	if (skb->len >= nlmsg_total_size(0)) {
		nlh = nlmsg_hdr(skb);
		umsg = NLMSG_DATA(nlh);

		if (umsg) {
			memcpy(&sqc_kmsg, umsg, sizeof(struct sqc_netlink_msg));
			pr_info("msg_type: %d, msg_val: %d\n", sqc_kmsg.msg_type, sqc_kmsg.msg_value);
		}
	}
}

struct netlink_kernel_cfg cfg = {
	.input  = netlink_rcv_msg, /* set recv callback */
};

static int sqc_netlink_init(void)
{
	/* create netlink socket */
	nlsk = (struct sock *)netlink_kernel_create(&init_net, NETLINK_TEST, &cfg);
	if (nlsk == NULL) {
		pr_info("netlink_kernel_create error !\n");
		return -1;
	}
	pr_info("test_netlink_init\n");

	return 0;
}

static void sqc_netlink_exit(void)
{
	if (nlsk) {
		netlink_kernel_release(nlsk); /* release ..*/
		nlsk = NULL;
	}
	pr_info("sqc_netlink_exit!\n");
}

module_init(sqc_netlink_init);
module_exit(sqc_netlink_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("zte.charger");
MODULE_DESCRIPTION("sqc charger notify daemon");
