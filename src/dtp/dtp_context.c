/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * This file is part of daos_transport. It implements the dtp context related
 * APIs.
 */

#include <dtp_internal.h>

int
dtp_context_create(void *arg, dtp_context_t *dtp_ctx)
{
	struct dtp_hg_context	*hg_ctx = NULL;
	int			rc = 0;

	if (dtp_ctx == NULL) {
		D_ERROR("invalid parameter of NULL dtp_ctx.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_ALLOC_PTR(hg_ctx);
	if (hg_ctx == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	pthread_rwlock_wrlock(&dtp_gdata.dg_rwlock);
	rc = dtp_hg_ctx_init(hg_ctx, dtp_gdata.dg_ctx_num);
	if (rc != 0) {
		D_ERROR("dtp_hg_ctx_init failed rc: %d.\n", rc);
		D_FREE_PTR(hg_ctx);
		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);
		D_GOTO(out, rc);
	}

	dtp_gdata.dg_ctx_num++;
	daos_list_add_tail(&hg_ctx->dhc_link, &dtp_gdata.dg_ctx_list);
	pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);

	*dtp_ctx = (dtp_context_t)hg_ctx;

out:
	return rc;
}

int
dtp_context_destroy(dtp_context_t dtp_ctx, int force)
{
	struct dtp_hg_context	*hg_ctx;
	int			rc = 0;

	if (dtp_ctx == DTP_CONTEXT_NULL) {
		D_ERROR("invalid parameter (NULL dtp_ctx).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	/* TODO: check force */

	hg_ctx = (struct dtp_hg_context *)dtp_ctx;
	rc = dtp_hg_ctx_fini(hg_ctx);
	if (rc == 0) {
		pthread_rwlock_wrlock(&dtp_gdata.dg_rwlock);
		dtp_gdata.dg_ctx_num--;
		daos_list_del_init(&hg_ctx->dhc_link);
		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);
		D_FREE_PTR(hg_ctx);
	} else {
		D_ERROR("dtp_hg_ctx_fini failed rc: %d.\n", rc);
	}

out:
	return rc;
}

int
dtp_context_idx(dtp_context_t dtp_ctx, int *ctx_idx)
{
	struct dtp_hg_context	*hg_ctx;
	int			rc = 0;

	if (dtp_ctx == DTP_CONTEXT_NULL || ctx_idx == NULL) {
		D_ERROR("invalid parameter, dtp_ctx: %p, ctx_idx: %p.\n",
			dtp_ctx, ctx_idx);
		D_GOTO(out, rc = -DER_INVAL);
	}

	hg_ctx = (struct dtp_hg_context *)dtp_ctx;
	*ctx_idx = hg_ctx->dhc_idx;

out:
	return rc;
}

int
dtp_context_num(int *ctx_num)
{
	if (ctx_num == NULL) {
		D_ERROR("invalid parameter of NULL ctx_num.\n");
		return -DER_INVAL;
	}

	*ctx_num = dtp_gdata.dg_ctx_num;
	return 0;
}

bool
dtp_context_empty(int locked)
{
	bool rc = false;

	if (locked == 0)
		pthread_rwlock_rdlock(&dtp_gdata.dg_rwlock);

	rc = daos_list_empty(&dtp_gdata.dg_ctx_list);

	if (locked == 0)
		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);

	return rc;
}

int
dtp_progress(dtp_context_t dtp_ctx, unsigned int timeout,
	     unsigned int *creds, dtp_progress_cond_cb_t cond_cb, void *arg)
{
	struct dtp_hg_context	*hg_ctx;
	unsigned int		credits, actual;
	struct timeval		tv;
	uint64_t		now, end;
	int			rc = 0;

	if (dtp_ctx == DTP_CONTEXT_NULL) {
		D_ERROR("invalid parameter (NULL dtp_ctx).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (creds != NULL && *creds <= 0) {
		D_ERROR("invalid parameter, creds: %d.\n", *creds);
		D_GOTO(out, rc = -DER_INVAL);
	}

	credits = (creds == NULL) ? UINT32_MAX : *creds;
	actual = credits;
	hg_ctx = (struct dtp_hg_context *)dtp_ctx;
	if (timeout == 0 || cond_cb == NULL) {
		rc = dtp_hg_progress(hg_ctx, &actual, timeout);
		if (rc != 0 && rc != -DER_TIMEDOUT)
			D_ERROR("dtp_hg_progress failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}

	rc = gettimeofday(&tv, NULL);
	if (rc != 0)
		D_GOTO(out, rc);
	now = tv.tv_sec * 1000 * 1000 + tv.tv_usec;
	end = now + timeout * 1000 * 1000;
	while (1) {
		rc = dtp_hg_progress(hg_ctx, &actual, 1);
		if (rc == 0) {
			D_ASSERT(actual <= credits);
			if (actual > 0)
				break;
		} else if (rc != -DER_TIMEDOUT) {
			D_ERROR("dtp_hg_progress failed, rc: %d.\n", rc);
			break;
		}

		/*
		 * now cannot get the number of remaining credits from mercury,
		 * so pass credits to cond_cb.
		 */
		if (cond_cb(arg, credits) != 0)
			break;

		rc = gettimeofday(&tv, NULL);
		if (rc != 0)
			D_GOTO(out, rc);
		now = tv.tv_sec * 1000 * 1000 + tv.tv_usec;
		if (now >= end) {
			rc = -DER_TIMEDOUT;
			break;
		}
	};

out:
	return rc;
}
