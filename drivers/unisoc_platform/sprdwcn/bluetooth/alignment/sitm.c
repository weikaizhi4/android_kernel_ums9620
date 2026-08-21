// SPDX-License-Identifier: GPL-2.0
#include <linux/kfifo.h>
#include <linux/slab.h>

#include "sitm.h"

#define HCI_COMMAND 1
#define HCI_ACL     2
#define HCI_SCO     3
#define HCI_EVENT   4
#define HCI_ISO     5
#define HCI_MAX_FRAME 1026
#define HCI_FIFO_SIZE 2048

struct sitm_state {
	struct kfifo fifo;
	u8 frame[HCI_MAX_FRAME + 8];
	u16 frame_len;
	u16 remaining;
	u8 type;
	u8 preamble_len;
};

static struct sitm_state *state;

static u8 hci_preamble_len(u8 type)
{
	switch (type) {
	case HCI_COMMAND:
	case HCI_SCO:
		return 3;
	case HCI_ACL:
	case HCI_ISO:
		return 4;
	case HCI_EVENT:
		return 2;
	default:
		return 0;
	}
}

static u16 hci_payload_len(const struct sitm_state *s)
{
	switch (s->type) {
	case HCI_ACL:
	case HCI_ISO:
		return s->frame[3] | (s->frame[4] << 8);
	default:
		return s->frame[s->preamble_len];
	}
}

int sitm_init(void)
{
	if (state)
		return 0;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;
	if (kfifo_alloc(&state->fifo, HCI_FIFO_SIZE, GFP_KERNEL)) {
		kfree(state);
		state = NULL;
		return -ENOMEM;
	}
	return 0;
}

void sitm_cleanup(void)
{
	if (!state)
		return;
	kfifo_free(&state->fifo);
	kfree(state);
	state = NULL;
}

static int sitm_parse(frame_complete_cb frame_complete)
{
	u8 byte;

	while (kfifo_out(&state->fifo, &byte, 1) == 1) {
		if (!state->frame_len) {
			state->preamble_len = hci_preamble_len(byte);
			if (!state->preamble_len)
				continue;
			state->type = byte;
			state->frame[0] = byte;
			state->frame_len = 1;
			state->remaining = state->preamble_len;
			continue;
		}

		if (state->frame_len >= HCI_MAX_FRAME) {
			state->frame_len = 0;
			continue;
		}
		state->frame[state->frame_len++] = byte;
		if (--state->remaining)
			continue;

		if (state->frame_len == state->preamble_len + 1) {
			state->remaining = hci_payload_len(state);
			if (state->remaining)
				continue;
		}

		if (state->type == HCI_COMMAND || state->type == HCI_ACL ||
		    state->type == HCI_ISO) {
			u8 padding = 8 - (state->frame_len & 7);

			while (padding--)
				state->frame[state->frame_len++] = 0;
		}
		frame_complete(state->frame, state->frame_len);
		state->frame_len = 0;
	}
	return 0;
}

int sitm_write(const u8 *buf, int count, frame_complete_cb frame_complete)
{
	int written;

	if (!state)
		return -ENODEV;
	written = kfifo_in(&state->fifo, buf, count);
	sitm_parse(frame_complete);
	return written;
}
