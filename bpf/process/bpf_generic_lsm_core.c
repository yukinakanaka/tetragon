// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright Authors of Cilium */

#include "vmlinux.h"
#include "api.h"

#define GENERIC_LSM

#include "compiler.h"
#include "bpf_event.h"
#include "bpf_task.h"
#ifdef __V511_BPF_PROG
#include "bpf_lsm_ima.h"
#endif
#include "retprobe_map.h"
#include "types/operations.h"
#include "types/basic.h"

char _license[] __attribute__((section("license"), used)) = "Dual BSD/GPL";

int generic_lsm_setup_event(void *ctx);
int generic_lsm_process_event(void *ctx);
int generic_lsm_process_filter(void *ctx);
int generic_lsm_filter_arg(void *ctx);
int generic_lsm_actions(void *ctx);
int generic_lsm_path(void *ctx);

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 13);
	__type(key, __u32);
	__array(values, int(void *));
} lsm_calls SEC(".maps") = {
	.values = {
		[TAIL_CALL_SETUP] = (void *)&generic_lsm_setup_event,
		[TAIL_CALL_PROCESS] = (void *)&generic_lsm_process_event,
		[TAIL_CALL_FILTER] = (void *)&generic_lsm_process_filter,
		[TAIL_CALL_ARGS] = (void *)&generic_lsm_filter_arg,
		[TAIL_CALL_ACTIONS] = (void *)&generic_lsm_actions,
#ifndef __V61_BPF_PROG
		[TAIL_CALL_PATH] = (void *)&generic_lsm_path,
#endif
	},
};

#include "generic_maps.h"
#include "generic_calls.h"

#define MAIN "lsm/generic_lsm_core"

__attribute__((section((MAIN)), used)) int
generic_lsm_event(struct pt_regs *ctx)
{
	return generic_start_process_filter(ctx, (struct bpf_map_def *)&lsm_calls);
}

__attribute__((section("lsm"), used)) int
generic_lsm_setup_event(void *ctx)
{
	return generic_process_event_and_setup(ctx, (struct bpf_map_def *)&lsm_calls);
}

__attribute__((section("lsm"), used)) int
generic_lsm_process_event(void *ctx)
{
	return generic_process_event(ctx, (struct bpf_map_def *)&lsm_calls);
}

__attribute__((section("lsm"), used)) int
generic_lsm_process_filter(void *ctx)
{
	int ret;

	ret = generic_process_filter();
	if (ret == PFILTER_CONTINUE)
		tail_call(ctx, &lsm_calls, TAIL_CALL_FILTER);
	else if (ret == PFILTER_ACCEPT)
		tail_call(ctx, &lsm_calls, TAIL_CALL_SETUP);
	return PFILTER_REJECT;
}

__attribute__((section("lsm"), used)) int
generic_lsm_filter_arg(void *ctx)
{
	return generic_filter_arg(ctx, (struct bpf_map_def *)&lsm_calls, true);
}

__attribute__((section("lsm"), used)) int
generic_lsm_actions(void *ctx)
{
	bool postit = generic_actions(ctx, (struct bpf_map_def *)&lsm_calls);

	struct msg_generic_kprobe *e;
	int zero = 0;

	e = map_lookup_elem(&process_call_heap, &zero);
	if (!e)
		return 0;

	e->lsm.post = postit;
#ifdef __V511_BPF_PROG
	// Set dummy hash entry for ima program
	if (e && e->common.flags & MSG_COMMON_FLAG_IMA_HASH && e->lsm.post) {
		struct ima_hash hash;

		__u64 pid_tgid = get_current_pid_tgid();

		memset(&hash, 0, sizeof(struct ima_hash));
		hash.state = 1;
		map_update_elem(&ima_hash_map, &pid_tgid, &hash, BPF_ANY);
	}
#endif

	// If NoPost action is set, check for Override action here
	if (!e->lsm.post)
		return try_override(ctx, (struct bpf_map_def *)&override_tasks);

	return 0;
}

#ifndef __V61_BPF_PROG
__attribute__((section("lsm"), used)) int
generic_lsm_path(void *ctx)
{
	return generic_path(ctx, (struct bpf_map_def *)&lsm_calls);
}
#endif
