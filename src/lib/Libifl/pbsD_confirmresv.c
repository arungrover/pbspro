/*
 * Copyright (C) 1994-2021 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of both the OpenPBS software ("OpenPBS")
 * and the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * OpenPBS is free software. You can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * OpenPBS is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * PBS Pro is commercially licensed software that shares a common core with
 * the OpenPBS software.  For a copy of the commercial license terms and
 * conditions, go to: (http://www.pbspro.com/agreement.html) or contact the
 * Altair Legal Department.
 *
 * Altair's dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of OpenPBS and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair's trademarks, including but not limited to "PBS™",
 * "OpenPBS®", "PBS Professional®", and "PBS Pro™" and Altair's logos is
 * subject to Altair's trademark licensing policies.
 */

/**
 * @file	pbs_confirmresv.c
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <string.h>
#include <stdio.h>
#include "libpbs.h"
#include "dis.h"
#include "pbs_ecl.h"


/**
 * @brief
 * 	-pbs_confirmresv - this function is for exclusive use by the Scheduler
 *	to confirm an advanced reservation.
 *
 * @param[in] rid 	Reservaion ID
 * @param[in] location  string of vnodes/resources to be allocated to the resv.
 * @param[in] start 	start time of reservation if non-zero
 *
 * @return	int
 * @retval	0	Success
 * @retval	!0	error
 *
 */

int
__pbs_confirmresv(int c, char *rid, char *location, unsigned long start,
	char *extend)
{
	int	rc;
	struct batch_reply   *reply;

	if ((rid == NULL)      || (*rid == '\0')  ||
		(location == NULL) || (*location == '\0'))
		return (pbs_errno = PBSE_IVALREQ);

	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return pbs_errno;

	/* lock pthread mutex here for this connection */
	/* blocking call, waits for mutex release */
	if (pbs_client_thread_lock_connection(c) != 0)
		return pbs_errno;

	/* setup DIS support routines for following DIS calls */

	DIS_tcp_funcs();

	/* send run request */

	if ((rc = encode_DIS_ReqHdr(c, PBS_BATCH_ConfirmResv, pbs_current_user)) ||
		(rc = encode_DIS_Run(c, rid, location, start)) ||
		(rc = encode_DIS_ReqExtend(c, extend))) {
		if (set_conn_errtxt(c, dis_emsg[rc]) != 0) {
			pbs_errno = PBSE_SYSTEM;
		} else {
			pbs_errno = PBSE_PROTOCOL;
		}
		(void)pbs_client_thread_unlock_connection(c);
		return pbs_errno;
	}

	if (dis_flush(c)) {
		pbs_errno = PBSE_PROTOCOL;
		(void)pbs_client_thread_unlock_connection(c);
		return pbs_errno;
	}

	/* get reply */

	reply = PBSD_rdrpy(c);
	rc = get_conn_errno(c);

	PBSD_FreeReply(reply);

	/* unlock the thread lock and update the thread context data */
	if (pbs_client_thread_unlock_connection(c) != 0)
		return pbs_errno;

	return rc;
}
