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
 * @file	pbs_stathook.c
 * @brief
 *	Return the status of a hook.
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include "libpbs.h"
#include "pbs_ecl.h"


/**
 * @brief
 *	Return the status of a hook.
 *
 * @param[in] c - communication handle
 * @param[in] id - object name
 * @param[in] attrib - pointer to attrl structure(list)
 * @param[in] extend - extend string for req
 *
 * @return	structure handle
 * @retval	pointer to attr list	success
 * @retval	NULL			error
 *
 */
struct batch_status *
__pbs_stathook(int c, char *id, struct attrl *attrib, char *extend)
{
	int		    hook_obj;

	if (extend != NULL) {
		if (strcmp(extend, PBS_HOOK) == 0) {
			hook_obj = MGR_OBJ_PBS_HOOK;
		} else if (strcmp(extend, SITE_HOOK) == 0) {
			hook_obj = MGR_OBJ_SITE_HOOK;
		} else {
			return NULL;	/* bad extend value */
		}
	} else {
		hook_obj = MGR_OBJ_SITE_HOOK;
	}

	return PBSD_status_aggregate(c, PBS_BATCH_StatusHook, id, attrib, extend, hook_obj, NULL);
}
