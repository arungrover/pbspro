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
 * @file	issue_request.c
 *
 * @brief
 * 		Function to allow the server to issue requests to to other batch
 * 		servers, scheduler, MOM, or even itself.
 *
 * 		The encoding of the data takes place in other routines, see
 * 		the API routines in libpbs.a
 *
 * Functions included are:
 *	relay_to_mom()
 *	reissue_to_svr()
 *	issue_to_svr()
 *	release_req()
 *	add_mom_deferred_list()
 *	issue_Drequest()
 *	process_Dreply()
 *	process_DreplyTPP()
 */
#include <pbs_config.h>   /* the master config generated by configure */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include "dis.h"
#include "libpbs.h"
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "credential.h"
#include "batch_request.h"
#include "log.h"
#include "job.h"
#include "work_task.h"
#include "net_connect.h"
#include "pbs_nodes.h"
#include "svrfunc.h"
#include "server.h"
#include <libutil.h>
#include "tpp.h"


/* Global Data Items: */
extern pbs_list_head task_list_event;
extern time_t	time_now;
extern char	*msg_issuebad;
extern char     *msg_norelytomom;
extern char	*msg_err_malloc;

extern int max_connection;

/**
 *
 * @brief
 *	Wrapper program to relay_to_mom2() with the 'pwt' argument
 *	passed as NULL.
 */
int
relay_to_mom(job *pjob, struct batch_request *request,
	     void (*func)(struct work_task *))
{
	return (relay_to_mom2(pjob, request, func, NULL));
}

/**
 * @brief
 * 		Relay a (typically existing) batch_request to MOM
 *
 *		Make connection to MOM and issue the request.  Called with
 *		network address rather than name to save look-ups.
 *
 *		Unlike issue_to_svr(), a failed connection is not retried.
 *		The calling routine typically handles this problem.
 *
 * @param[in,out]	pjob - pointer to job
 * @param[in]	request - the request to send
 * @param[in]	func - function pointer taking work_task structure as argument.
 * @param[out]  ppwt - the work task maintained by server
 *			to handle deferred replies from request.
 *
 * @return	int
 * @retval	0	- success
 * @retval	non-zero	- error code
 */

int
relay_to_mom2(job *pjob, struct batch_request *request,
	     void (*func)(struct work_task *), struct work_task **ppwt)
{
	int	rc;
	int	conn;	/* a client style connection handle */
	pbs_net_t    momaddr;
	unsigned int momport;
	struct work_task *pwt;
	int prot = PROT_TPP;
	mominfo_t *pmom = 0;
	pbs_list_head	*mom_tasklist_ptr = NULL;

	momaddr = pjob->ji_qs.ji_un.ji_exect.ji_momaddr;
	momport = pjob->ji_qs.ji_un.ji_exect.ji_momport;

	if ((pmom = tfind2((unsigned long) momaddr, momport, &ipaddrs)) == NULL)
		return (PBSE_NORELYMOM);

	mom_tasklist_ptr = &(((mom_svrinfo_t *) (pmom->mi_data))->msr_deferred_cmds);

	conn = svr_connect(momaddr, momport, process_Dreply, ToServerDIS, prot);
	if (conn < 0) {
		log_event(PBSEVENT_ERROR, PBS_EVENTCLASS_REQUEST, LOG_WARNING, "", msg_norelytomom);
		return (PBSE_NORELYMOM);
	}

	request->rq_orgconn = request->rq_conn;	/* save client socket */
	pbs_errno = 0;
	rc = issue_Drequest(conn, request, func, &pwt, prot);
	if ((rc == 0) && (func != release_req)) {
		/* work-task entry job related on TPP based connection, link to the job's list */
		append_link(&pjob->ji_svrtask, &pwt->wt_linkobj, pwt);
		if (prot == PROT_TPP)
			append_link(mom_tasklist_ptr, &pwt->wt_linkobj2, pwt); /* if tpp, link to mom list as well */
	}

	if (ppwt != NULL)
		*ppwt = pwt;

	/*
	 * We do not want req_reject() to send non PBSE error numbers.
	 * Check for internal errors and when found return PBSE_SYSTEM.
	 */
	if ((rc != 0) && (pbs_errno == 0))
		return (PBSE_SYSTEM);
	else
		return (rc);
}

/**
 * @brief
 * 		reissue_to_svr - recall issue_to_svr() after a delay to retry sending
 *		a request that failed for a temporary reason
 *
 * @see
 *  issue_to_svr
 *
 * @param[in]	pwt - pointer to work structure
 *
 * @return	void
 */
static void
reissue_to_svr(struct work_task *pwt)
{
	struct batch_request *preq = pwt->wt_parm1;
	int issue_to_svr(char *, struct batch_request *,
		void(*)(struct work_task *));

	/* if not timed-out, retry send to remote server */

	if (((time_now - preq->rq_time) > PBS_NET_RETRY_LIMIT) ||
		(issue_to_svr(preq->rq_host, preq, (void(*)(struct work_task *))pwt->wt_parm2) == -1)) {

		/* either timed-out or got hard error, tell post-function  */
		pwt->wt_aux = -1;	/* seen as error by post function  */
		pwt->wt_event = -1;	/* seen as connection by post func */
		((void (*)())pwt->wt_parm2)(pwt);
	}
	return;
}

/**
 * @brief
 * 		issue_to_svr - issue a batch request to a server
 *		This function parses the server name, looks up its host address,
 *		makes a connection and called issue_request (above) to send
 *		the request.
 *
 * @param[in]	servern - name of host sending request
 * @param[in,out]	preq - batch request to send
 * @param[in]	replyfunc - Call back func gor reply
 *
 * @return	int
 * @retval	0 - success,
 * @retval -1 - permanent error (no such host)
 *
 * @note
 *	On temporary error, establish a work_task to retry after a delay.
 */

int
issue_to_svr(char *servern, struct batch_request *preq, void (*replyfunc)(struct work_task *))
{
	int	  do_retry = 0;
	int	  handle;
	pbs_net_t svraddr;
	char	 *svrname;
	unsigned int  port = pbs_server_port_dis;
	struct work_task *pwt;
	extern int pbs_failover_active;
	extern char primary_host[];
	extern char server_host[];

	(void)strcpy(preq->rq_host, servern);
	preq->rq_fromsvr = 1;
	preq->rq_perm = ATR_DFLAG_MGRD | ATR_DFLAG_MGWR | ATR_DFLAG_SvWR;
	svrname = parse_servername(servern, &port);

	if ((pbs_failover_active != 0) && (svrname != NULL)) {
		/* we are the active secondary server in a failover config    */
		/* if the message is going to the primary,then redirect to me */
		size_t len;

		len = strlen(svrname);
		if (strncasecmp(svrname, primary_host, len) == 0) {
			if ((primary_host[(int)len] == '\0') ||
				(primary_host[(int)len] == '.'))
				svrname = server_host;
		}
	}
	svraddr = get_hostaddr(svrname);
	if (svraddr == (pbs_net_t)0) {
		if (pbs_errno == PBS_NET_RC_RETRY)
			/* Non fatal error - retry */
			do_retry = 1;
	} else {
		handle = svr_connect(svraddr, port, process_Dreply, ToServerDIS, PROT_TCP);
		if (handle >= 0)
			return (issue_Drequest(handle, preq, replyfunc, 0, 0));
		else if (handle == PBS_NET_RC_RETRY)
			do_retry = 1;
	}

	/* if reached here, it didn`t go, do we retry? */

	if (do_retry) {
		pwt = set_task(WORK_Timed, (long)(time_now+(2*PBS_NET_RETRY_TIME)),
			reissue_to_svr, (void *)preq);
		pwt->wt_parm2 = (void *)replyfunc;
		return (0);
	} else
		return (-1);
}


/**
 * @brief
 * 			release_req - this is the basic function to call after we are
 *			through with an internally generated request to another server.
 *			It frees the request structure and closes the connection (handle).
 *
 *			In the work task entry, wt_event is the connection handle and
 *			wt_parm1 is a pointer to the request structure.
 *
 * @note
 *			THIS SHOULD NOT BE USED IF AN EXTERNAL (CLIENT) REQUEST WAS "relayed".
 *			The request/reply structure is still needed to reply to the client.
 *
 * @param[in]	pwt - pointer to work structure
 *
 * @return void
 */

void
release_req(struct work_task *pwt)
{
	free_br((struct batch_request *)pwt->wt_parm1);
	if (pwt->wt_event != -1 && pwt->wt_aux2 != PROT_TPP)
		svr_disconnect(pwt->wt_event);
}


/**
 *	@brief
 *		add a task to the moms deferred command list
 *		of commands issued to the server
 *
 *		Used only in case of TPP
 *
 * @param[in] stream - stream on which command is being sent
 * @param[in] minfo  - The mominfo_t pointer for the mom
 * @param[in] func   - Call back func when mom responds
 * @param[in] msgid  - String unique identifying the command from others
 * @param[in] parm1  - Fist parameter to the work task to be set
 * @param[in] parm2  - Second parameter to the work task to be set
 *
 * @return Work task structure that was allocated and added to moms deferred cmd list
 * @retval NULL  - Failure
 * @retval !NULL - Success
 *
 */
struct work_task *
add_mom_deferred_list(int stream, mominfo_t *minfo, void (*func)(), char *msgid, void *parm1, void *parm2)
{
	struct work_task *ptask = NULL;

	/* WORK_Deferred_cmd is very similar to WORK_Deferred_reply.
	 * However in case of WORK_Deferred_reply, the wt_parm1 is assumed to
	 * contain a batch_request structure. In cases where there is no
	 * batch_request structure associated, we use the WORK_Deferred_cmd
	 * event type to differentiate it in process_DreplyTPP.
	 */
	ptask = set_task(WORK_Deferred_cmd, (long) stream, func, parm1);
	if (ptask == NULL) {
		log_err(errno, __func__, "could not set_task");
		return NULL;
	}
	ptask->wt_aux2 = PROT_TPP; /* set to tpp */
	ptask->wt_parm2 = parm2;
	ptask->wt_event2 = msgid;

	/* remove this task from the event list, as we will be adding to deferred list anyway
	 * and there is no child process whose exit needs to be reaped
	 */
	delete_link(&ptask->wt_linkevent);

	/* append to the moms deferred command list */
	append_link(&(((mom_svrinfo_t *) (minfo->mi_data))->msr_deferred_cmds), &ptask->wt_linkobj2, ptask);
	return ptask;
}

/**
 * @brief
 * 		issue a batch request to another server or to a MOM
 *		or even to ourself!
 *
 *		If the request is meant for this every server, then
 *		Set up work-task of type WORK_Deferred_Local with a dummy
 *		connection handle (PBS_LOCAL_CONNECTION).
 *
 *		Dispatch the request to be processed.  [reply_send() will
 *		dispatch the reply via the work task entry.]
 *
 *		If the request is to another server/MOM, then
 *		Set up work-task of type WORK_Deferred_Reply with the
 *		connection handle as the event.
 *
 *		Encode and send the request.
 *
 *		When the reply is ready,  process_reply() will decode it and
 *		dispatch the work task.
 *
 * @note
 *		IT IS UP TO THE FUNCTION DISPATCHED BY THE WORK TASK TO CLOSE THE
 *		CONNECTION (connection handle not socket) and FREE THE REQUEST
 *		STRUCTURE.  The connection (non-negative if open) is in wt_event
 *		and the pointer to the request structure is in wt_parm1.
 *
 * @param[in] conn	- connection index
 * @param[in] request	- batch request to send
 * @param[in] func	- The callback function to invoke to handle the batch reply
 * @param[out] ppwt	- Return work task to be maintained by server to handle deferred replies
 * @param[in] prot	- PROT_TCP or PROT_TPP
 *
 * @return  Error code
 * @retval   0 - Success
 * @retval  -1 - Failure
 *
 */
int
issue_Drequest(int conn, struct batch_request *request, void (*func)(), struct work_task **ppwt, int prot)
{
	struct attropl   *patrl;
	struct work_task *ptask;
	struct svrattrl  *psvratl;
	int		  rc;
	int		  sock = -1;
	enum work_type	  wt;
	char 		 *msgid = NULL;

	request->tppcmd_msgid = NULL;

	if (conn == PBS_LOCAL_CONNECTION) {
		wt   = WORK_Deferred_Local;
		request->rq_conn = PBS_LOCAL_CONNECTION;
	} else if (prot == PROT_TPP) {
		sock = conn;
		request->rq_conn = conn;
		wt   = WORK_Deferred_Reply;
	} else {
		sock = conn;
		request->rq_conn = sock;
		wt   = WORK_Deferred_Reply;
		DIS_tcp_funcs();
	}

	ptask = set_task(wt, (long) conn, func, (void *) request);
	if (ptask == NULL) {
		log_err(errno, __func__, "could not set_task");
		if (ppwt != 0)
			*ppwt = 0;
		return (-1);
	}

	if (conn == PBS_LOCAL_CONNECTION) {

		/* the request should be issued to ourself */

		dispatch_request(PBS_LOCAL_CONNECTION, request);
		if (ppwt != 0)
			*ppwt = ptask;
		return (0);
	}

	/* the request is bound to another server, encode/send the request */
	switch (request->rq_type) {

		case PBS_BATCH_DeleteJob:
			rc =   PBSD_mgr_put(conn,
				PBS_BATCH_DeleteJob,
				MGR_CMD_DELETE,
				MGR_OBJ_JOB,
				request->rq_ind.rq_delete.rq_objname,
				NULL,
				request->rq_extend,
				prot,
				&msgid);
			break;

		case PBS_BATCH_HoldJob:
			attrl_fixlink(&request->rq_ind.rq_hold.rq_orig.rq_attr);
			psvratl = (struct svrattrl *)GET_NEXT(
				request->rq_ind.rq_hold.rq_orig.rq_attr);
			patrl = &psvratl->al_atopl;
			rc =  PBSD_mgr_put(conn,
				PBS_BATCH_HoldJob,
				MGR_CMD_SET,
				MGR_OBJ_JOB,
				request->rq_ind.rq_hold.rq_orig.rq_objname,
				patrl,
				NULL,
				prot,
				&msgid);
			break;

		case PBS_BATCH_MessJob:
			rc =  PBSD_msg_put(conn,
				request->rq_ind.rq_message.rq_jid,
				request->rq_ind.rq_message.rq_file,
				request->rq_ind.rq_message.rq_text,
				NULL,
				prot,
				&msgid);
			break;

		case PBS_BATCH_RelnodesJob:
			rc =  PBSD_relnodes_put(conn,
				request->rq_ind.rq_relnodes.rq_jid,
				request->rq_ind.rq_relnodes.rq_node_list,
				NULL,
				prot,
				&msgid);
			break;

		case PBS_BATCH_PySpawn:
			rc =  PBSD_py_spawn_put(conn,
				request->rq_ind.rq_py_spawn.rq_jid,
				request->rq_ind.rq_py_spawn.rq_argv,
				request->rq_ind.rq_py_spawn.rq_envp,
				prot,
				&msgid);
			break;

		case PBS_BATCH_ModifyJob:
			attrl_fixlink(&request->rq_ind.rq_modify.rq_attr);
			patrl = (struct attropl *)&((struct svrattrl *)GET_NEXT(
				request->rq_ind.rq_modify.rq_attr))->al_atopl;
			rc = PBSD_mgr_put(conn,
				PBS_BATCH_ModifyJob,
				MGR_CMD_SET,
				MGR_OBJ_JOB,
				request->rq_ind.rq_modify.rq_objname,
				patrl,
				NULL,
				prot,
				&msgid);
			break;

		case PBS_BATCH_ModifyJob_Async:
			attrl_fixlink(&request->rq_ind.rq_modify.rq_attr);
			patrl = (struct attropl *)&((struct svrattrl *)GET_NEXT(
				request->rq_ind.rq_modify.rq_attr))->al_atopl;
			rc = PBSD_mgr_put(conn,
				PBS_BATCH_ModifyJob_Async,
				MGR_CMD_SET,
				MGR_OBJ_JOB,
				request->rq_ind.rq_modify.rq_objname,
				patrl,
				NULL,
				prot,
				&msgid);
			break;

		case PBS_BATCH_Rerun:
			if (prot == PROT_TPP) {
				rc = is_compose_cmd(sock, IS_CMD, &msgid);
				if (rc != 0)
					break;
			}
			rc = encode_DIS_ReqHdr(sock, PBS_BATCH_Rerun, pbs_current_user);
			if (rc != 0)
				break;
			rc = encode_DIS_JobId(sock, request->rq_ind.rq_rerun);
			if (rc != 0)
				break;
			rc = encode_DIS_ReqExtend(sock, 0);
			if (rc != 0)
				break;
			rc = dis_flush(sock);
			break;


		case PBS_BATCH_RegistDep:
			if (prot == PROT_TPP) {
				rc = is_compose_cmd(sock, IS_CMD, &msgid);
				if (rc != 0)
					break;
			}
			rc = encode_DIS_ReqHdr(sock,
				PBS_BATCH_RegistDep, pbs_current_user);
			if (rc != 0)
				break;
			rc = encode_DIS_Register(sock, request);
			if (rc != 0)
				break;
			rc = encode_DIS_ReqExtend(sock, 0);
			if (rc != 0)
				break;
			rc = dis_flush(sock);
			break;

		case PBS_BATCH_SignalJob:
			rc =  PBSD_sig_put(conn,
				request->rq_ind.rq_signal.rq_jid,
				request->rq_ind.rq_signal.rq_signame,
				NULL,
				prot,
				&msgid);
			break;

		case PBS_BATCH_StatusJob:
			rc =  PBSD_status_put(conn,
				PBS_BATCH_StatusJob,
				request->rq_ind.rq_status.rq_id,
				NULL, NULL,
				prot,
				&msgid);
			break;

		case PBS_BATCH_TrackJob:
			if (prot == PROT_TPP) {
				rc = is_compose_cmd(sock, IS_CMD, &msgid);
				if (rc != 0)
					break;
			}
			rc = encode_DIS_ReqHdr(sock, PBS_BATCH_TrackJob, pbs_current_user);
			if (rc != 0)
				break;
			rc = encode_DIS_TrackJob(sock, request);
			if (rc != 0)
				break;
			rc = encode_DIS_ReqExtend(sock, request->rq_extend);
			if (rc != 0)
				break;
			rc = dis_flush(sock);
			break;

		case PBS_BATCH_CopyFiles:
			if (prot == PROT_TPP) {
				rc = is_compose_cmd(sock, IS_CMD, &msgid);
				if (rc != 0)
					break;
			}
			rc = encode_DIS_ReqHdr(sock,
				PBS_BATCH_CopyFiles, pbs_current_user);
			if (rc != 0)
				break;
			rc = encode_DIS_CopyFiles(sock, request);
			if (rc != 0)
				break;
			rc = encode_DIS_ReqExtend(sock, get_job_credid(request->rq_ind.rq_cpyfile.rq_jobid));
			if (rc != 0)
				break;
			rc = dis_flush(sock);
			break;

		case PBS_BATCH_CopyFiles_Cred:
			if (prot == PROT_TPP) {
				rc = is_compose_cmd(sock, IS_CMD, &msgid);
				if (rc != 0)
					break;
			}
			rc = encode_DIS_ReqHdr(sock,
				PBS_BATCH_CopyFiles_Cred, pbs_current_user);
			if (rc != 0)
				break;
			rc = encode_DIS_CopyFiles_Cred(sock, request);
			if (rc != 0)
				break;
			rc = encode_DIS_ReqExtend(sock, 0);
			if (rc != 0)
				break;
			rc = dis_flush(sock);
			break;

		case PBS_BATCH_DelFiles:
			if (prot == PROT_TPP) {
				rc = is_compose_cmd(sock, IS_CMD, &msgid);
				if (rc != 0)
					break;
			}
			rc = encode_DIS_ReqHdr(sock,
				PBS_BATCH_DelFiles, pbs_current_user);
			if (rc != 0)
				break;
			rc = encode_DIS_CopyFiles(sock, request);
			if (rc != 0)
				break;
			rc = encode_DIS_ReqExtend(sock, 0);
			if (rc != 0)
				break;
			rc = dis_flush(sock);
			break;

		case PBS_BATCH_DelFiles_Cred:
			if (prot == PROT_TPP) {
				rc = is_compose_cmd(sock, IS_CMD, &msgid);
				if (rc != 0)
					break;
			}
			rc = encode_DIS_ReqHdr(sock,
				PBS_BATCH_DelFiles_Cred, pbs_current_user);
			if (rc != 0)
				break;
			rc = encode_DIS_CopyFiles_Cred(sock, request);
			if (rc != 0)
				break;
			rc = encode_DIS_ReqExtend(sock, 0);
			if (rc != 0)
				break;
			rc = dis_flush(sock);
			break;

		case PBS_BATCH_FailOver:
			/* we should never do this on tpp based connection */
			rc = put_failover(sock, request);
			break;

		case PBS_BATCH_Cred:
			rc = PBSD_cred(conn,
				request->rq_ind.rq_cred.rq_credid,
				request->rq_ind.rq_cred.rq_jobid,
				request->rq_ind.rq_cred.rq_cred_type,
				request->rq_ind.rq_cred.rq_cred_data,
				request->rq_ind.rq_cred.rq_cred_validity,
				prot,
				&msgid);
			break;

		default:
			(void)sprintf(log_buffer, msg_issuebad, request->rq_type);
			log_err(-1, __func__, log_buffer);
			delete_task(ptask);
			rc = -1;
			break;
	}

	if (rc) {
		sprintf(log_buffer,
			"issue_Drequest failed, error=%d on request %d",
			rc, request->rq_type);
		log_err(-1, __func__, log_buffer);
		if (msgid)
			free(msgid);
		delete_task(ptask);
	} else if (ppwt != 0) {
		if (prot == PROT_TPP) {
			tpp_add_close_func(sock, process_DreplyTPP); /* register a close handler */

			ptask->wt_event2 = msgid;
			/*
			 * since its delayed task for tpp based connection
			 * remove it from the task_event list
			 * caller will add to moms deferred cmd list
			 */
			delete_link(&ptask->wt_linkevent);
		}
		ptask->wt_aux2 = prot;
		*ppwt = ptask;
	}

	return (rc);
}

/**
 * @brief
 * 		process the reply received for a request issued to
 *		another server via issue_request() over TCP
 *
 * @param[in] sock - TCP socket over which reply arrived
 *
 * @return	void
 */
void
process_Dreply(int sock)
{
	struct work_task	*ptask;
	int			 rc;
	struct batch_request	*request;

	/* find the work task for the socket, it will point us to the request */
	ptask = (struct work_task *)GET_NEXT(task_list_event);
	while (ptask) {
		if ((ptask->wt_type == WORK_Deferred_Reply) && (ptask->wt_event == sock))
			break;
		ptask = (struct work_task *)GET_NEXT(ptask->wt_linkevent);
	}
	if (!ptask) {
		close_conn(sock);
		return;
	}

	request = ptask->wt_parm1;

	/* read and decode the reply */
	/* set long timeout on I/O   */

	pbs_tcp_timeout = PBS_DIS_TCP_TIMEOUT_LONG;

	if ((rc = DIS_reply_read(sock, &request->rq_reply, 0)) != 0) {
		close_conn(sock);
		request->rq_reply.brp_code = rc;
		request->rq_reply.brp_choice = BATCH_REPLY_CHOICE_NULL;
	}
	pbs_tcp_timeout = PBS_DIS_TCP_TIMEOUT_SHORT;	/* short timeout */

	/* now dispatch the reply to the routine in the work task */

	dispatch_task(ptask);
	return;
}

/**
 * @brief
 * 		process the reply received for a request issued to
 *		  another server via issue_request()
 *
 * 		Reads the reply from the TPP stream and executes the work task associated
 * 		with the reply message. The request for which this reply arrived
 * 		is matched by comparing the msgid of the reply with the msgid of the work
 * 		tasks stored in the msr_deferred_cmds list of the mom for this stream.
 *
 * @param[in] handle - TPP handle on which reply/close arrived
 *
 * @return void
 */
void
process_DreplyTPP(int handle)
{
	struct work_task	*ptask;
	int			 rc;
	struct batch_request	*request;
	struct batch_reply *reply;
	char 		*msgid = NULL;
	mominfo_t *pmom = 0;

	if ((pmom = tfind2((u_long) handle, 0, &streams)) == NULL)
		return;

	DIS_tpp_funcs();

	/* find the work task for the socket, it will point us to the request */
	msgid = disrst(handle, &rc);

	if (!msgid || rc) { /* tpp connection actually broke, cull all pending requests */
		while ((ptask = (struct work_task *)GET_NEXT((((mom_svrinfo_t *) (pmom->mi_data))->msr_deferred_cmds)))) {
			/* no need to compare wt_event with handle, since the
			 * task list is for this mom and so it will always match
			 */
			if (ptask->wt_type == WORK_Deferred_Reply) {
				request = ptask->wt_parm1;
				if (request) {
					request->rq_reply.brp_code = rc;
					request->rq_reply.brp_choice = BATCH_REPLY_CHOICE_NULL;
				}
			}

			ptask->wt_aux = PBSE_NORELYMOM;
			pbs_errno = PBSE_NORELYMOM;
			free(ptask->wt_event2);

			dispatch_task(ptask);
		}
	} else {
		/* we read msgid fine, so proceed to match it and process the respective task */

		/* get the task list */
		ptask = (struct work_task *)GET_NEXT((((mom_svrinfo_t *) (pmom->mi_data))->msr_deferred_cmds));

		while (ptask) {

			char *cmd_msgid = ptask->wt_event2;

			if (strcmp(cmd_msgid, msgid) == 0) {

				if (ptask->wt_type == WORK_Deferred_Reply)
					request = ptask->wt_parm1;
				else
					request = NULL;

				if (!request) {
					if ((reply = (struct batch_reply *) malloc(sizeof(struct batch_reply))) == 0) {
						delete_task(ptask);
						free(cmd_msgid);
						log_err(errno, msg_daemonname, "Out of memory creating batch reply");
						return;
					}
					(void) memset(reply, 0, sizeof(struct batch_reply));
				} else {
					reply = &request->rq_reply;
				}

				/* read and decode the reply */
				if ((rc = DIS_reply_read(handle, reply, 1)) != 0) {
					reply->brp_code = rc;
					reply->brp_choice = BATCH_REPLY_CHOICE_NULL;
					ptask->wt_aux = PBSE_NORELYMOM;
					pbs_errno = PBSE_NORELYMOM;
				} else {
					ptask->wt_aux = reply->brp_code;
					pbs_errno = reply->brp_code;
				}

				ptask->wt_parm3 = reply; /* set the reply in case callback fn uses without having a preq */

				dispatch_task(ptask);

				if (!request)
					PBSD_FreeReply(reply);

				free(cmd_msgid);

				break;
			}
			ptask = (struct work_task *) GET_NEXT(ptask->wt_linkobj2);
		}
		free(msgid); /* the msgid read should be free after use in matching */
	}
}
