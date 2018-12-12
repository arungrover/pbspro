/*
 * Copyright (C) 1994-2018 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * For a copy of the commercial license terms and conditions,
 * go to: (http://www.pbspro.com/UserArea/agreement.html)
 * or contact the Altair Legal Department.
 *
 * Altair’s dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of PBS Pro and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair’s trademarks, including but not limited to "PBS™",
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's
 * trademark licensing policies.
 *
 */
#include <pbs_config.h>   /* the master config generated by configure */
/**
 * @file	chk_file_sec.c
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <assert.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "portability.h"
#include "log.h"
#include "libutil.h"
#include "pbs_ifl.h"


#ifndef	S_ISLNK
#define	S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#endif

#ifdef WIN32

/**
 * @brief
 *	test stat of directory by checking permission mask.
 *
 * @param[in]	sp - pointer to stat structure
 * @param[in]	isdir - value indicating directory or not
 * @param[in]	sticky - value indicating whether to allow write on directory
 * @param[in] 	disallow - value indicating whether admin and owner given access permission
 * @param[in] 	path - directory path
 * @param[in] 	errmsg - appropriate error msg 
 *
 * @return	int
 * @retval	0		success
 * @retval	error code	error
 *
 */
static int
teststat(struct stat *sp, int isdir, int sticky, int disallow,
	char *path, char *errmsg)
{

	int	rc = 0;

	if (isdir && !(S_ISDIR(sp->st_mode))) {
		/* Target is supposed to be a directory, but is not. */
		rc = ENOTDIR;
	} else if (!isdir && (S_ISDIR(sp->st_mode))) {
		/* Target is not supposed to be a directory, but is. */
		rc = EISDIR;
	} else {
		rc=perm_granted_admin_and_owner(path, disallow, NULL, errmsg);
	}
	return rc;
}

#else	/******* UNIX only code here ************ */

/**
 * @brief
 *      test stat of directory by checking permission mask.
 *
 * @param[in]   sp - pointer to stat structure
 * @param[in]   isdir - value indicating directory or not
 * @param[in]   sticky - value indicating whether to allow write on directory
 * @param[in]   disallow - value indicating whether admin and owner given access permission
 *
 * @return      int
 * @retval      0               success
 * @retval      error code      error
 *
 */

static int
teststat(struct stat *sp, int isdir, int sticky, int disallow)
{
	int	rc = 0;

	if ((~disallow & S_IWUSR) && (sp->st_uid > 10)) {
		/* Owner write is allowed, and UID is greater than 10. */
		rc = EPERM;
	} else if ((~disallow & S_IWGRP) && (sp->st_gid > 9)) {
		/* Group write is allowed, and GID is greater than 9. */
		rc = EPERM;
	} else if ((~disallow & S_IWOTH) &&
		(!S_ISDIR(sp->st_mode) || !(sp->st_mode & S_ISVTX) || !sticky)) {
		/*
		 * Other write is allowed, and at least one of the following
		 * is true:
		 * - target is not a directory
		 * - target does not have sticky bit set
		 * - the value of the sticky argument we were passed was zero
		 */
		rc = EPERM;
	} else if (isdir && !S_ISDIR(sp->st_mode)) {
		/* Target is supposed to be a directory, but is not. */
		rc = ENOTDIR;
	} else if (!isdir && S_ISDIR(sp->st_mode)) {
		/* Target is not supposed to be a directory, but is. */
		rc = EISDIR;
	} else if ((S_IRWXU|S_IRWXG|S_IRWXO) & disallow & sp->st_mode) {
		/* Disallowed permission bits are set in the mode mask. */
		rc =  EACCES;
	}
	return rc;
}

/**
 * @brief
 *      test stat of directory by checking permission mask.
 *
 * @param[in]   sp - pointer to stat structure
 * @param[in]   isdir - value indicating directory or not
 * @param[in]   sticky - value indicating whether to allow write on directory
 * @param[in]   disallow - value indicating whether admin and owner given access permission
 *
 * @return      int
 * @retval      0               success
 * @retval      error code      error
 *
 */

static int
tempstat(struct stat *sp, int isdir, int sticky, int disallow)
{
	int	rc = 0;

	if ((~disallow & S_IWUSR) && (sp->st_uid > 10)) {
		/* Owner write is allowed, and UID is greater than 10. */
		rc = EPERM;
	} else if ((~disallow & S_IWGRP) && (sp->st_gid > 9)) {
		/* Group write is allowed, and GID is greater than 9. */
		rc = EPERM;
	} else if (~disallow & S_IWOTH) {
		/*
		 * Other write is allowed, and at least one of the following
		 * is true:
		 * - target is not a directory
		 * - the value of the sticky argument we were passed was zero
		 */
		if (!S_ISDIR(sp->st_mode) || !sticky)
			rc = EPERM;
		/*
		 ** - sticky bit is off and other write is on
		 */
		if (!(sp->st_mode & S_ISVTX) && (sp->st_mode & S_IWOTH))
			rc = EPERM;
	} else if (isdir && !S_ISDIR(sp->st_mode)) {
		/* Target is supposed to be a directory, but is not. */
		rc = ENOTDIR;
	} else if (!isdir && S_ISDIR(sp->st_mode)) {
		/* Target is not supposed to be a directory, but is. */
		rc = EISDIR;
	} else if ((S_IRWXU|S_IRWXG|S_IRWXO) & disallow & sp->st_mode) {
		/* Disallowed permission bits are set in the mode mask. */
		rc =  EACCES;
	}
	return rc;
}

#endif  /** END of UNIX only code **/

/**
 * @brief
 * 	chk_file_sec() - Check file/directory security
 *      Part of the PBS System Security "Feature"
 *
 * @par	To be secure, all directories (and final file) in path must be:
 *		owned by uid < 10
 *		owned by group < 10 if group writable
 *		not have world writable unless stick bit set & this is allowed.
 *
 * @param[in]	path - path to check
 * @param[in]	isdir - 1 = path is directory, 0 = file
 * @param[in]	sticky - allow write on directory if sticky set
 * @param[in]	disallow - perm bits to disallow	
 * @param[in] 	fullpath - check full path
 * 
 * @return	int	
 * @retval	0 			if ok
 * @retval	errno value 		if not ok, including:
 *              			EPERM if not owned by root
 *              			ENOTDIR if not file/directory as specified
 *              			EACCESS if permissions are not ok
 */
int
chk_file_sec(char *path, int isdir, int sticky, int disallow, int fullpath)
{
	int		rc = 0;
	struct	stat	sbuf;
	char 	*real = NULL;

	assert(path != NULL);
	assert(path[0] != '\0');

	if ((real = realpath(path, NULL)) == NULL) {
		rc = errno;
		goto chkerr;
	}

#ifdef WIN32
	if ( ( (real[0] == '/') || \
		((real[1] == ':') && (real[2] == '/')) ) && fullpath ) {
		char *slash;

		if (real[0] == '/')
			slash = strchr(&real[1], '/');
		else
			slash = strchr(&real[3], '/');

		while (slash != NULL) {
			*slash = '\0';		/* temp end of string */

			if (lstat(real, &sbuf) == -1) {
				rc = errno;
				goto chkerr;
			}

			assert(S_ISLNK(sbuf.st_mode) == 0);
			rc = teststat(&sbuf, 1, sticky, WRITES_MASK, real,
				log_buffer);
			if (rc != 0)
				goto chkerr;

			*slash = '/';
			slash = strchr(slash+1, '/');
		}
	}
#else
	if ((path[0] == '/') && fullpath) {
		/* check full path starting at root */
		char	*slash = strchr(&real[1], '/');

		while (slash != NULL) {
			*slash = '\0';		/* temp end of string */

			if (lstat(real, &sbuf) == -1) {
				rc = errno;
				goto chkerr;
			}

			assert(S_ISLNK(sbuf.st_mode) == 0);
#if	defined(_SX)
			rc = teststat(&sbuf, 1, sticky, S_IWOTH);
#else
			rc = teststat(&sbuf, 1, sticky, S_IWGRP|S_IWOTH);
#endif
			if (rc != 0)
				goto chkerr;

			*slash = '/';
			slash = strchr(slash+1, '/');
		}
	}
#endif

	if (lstat(real, &sbuf) == -1) {
		rc = errno;
		goto chkerr;
	}

	assert(S_ISLNK(sbuf.st_mode) == 0);

#ifdef WIN32
	rc = teststat(&sbuf, isdir, sticky, disallow, real, log_buffer);
#else
	rc = teststat(&sbuf, isdir, sticky, disallow);
#endif

chkerr:
	if (rc != 0) {
		char *error_buf;

		pbs_asprintf(&error_buf,
			"Security violation \"%s\" resolves to \"%s\"",
			path, real);
		log_err(rc, __func__, error_buf);
#ifdef WIN32
		if (strlen(log_buffer) > 0)
			log_err(rc, __func__, log_buffer);
#endif
		free(error_buf);
	}

	free(real);
	return (rc);
}

/**
 * @brief
 * 	tmp_file_sec() - Check file/directory security
 *      Part of the PBS System Security "Feature"
 *
 * @par	To be secure, all directories (and final file) in path must be:
 *		owned by uid < 10
 *		owned by group < 10 if group writable
 *		not have world writable unless stick bit set & this is allowed.
 *
 * @param[in]   path - path to check
 * @param[in]   isdir - 1 = path is directory, 0 = file
 * @param[in]   sticky - allow write on directory if sticky set
 * @param[in]   disallow - perm bits to disallow
 * @param[in]   fullpath - check full path
 *
 * @return      int
 * @retval      0                       if ok
 * @retval      errno value             if not ok, including:
 *                                      EPERM if not owned by root
 *                                      ENOTDIR if not file/directory as specified
 *                                      EACCESS if permissions are not ok
 */

int
tmp_file_sec(char *path, int isdir, int sticky, int disallow, int fullpath)
{
	int		rc = 0;
	struct	stat	sbuf;
	char	*real = NULL;

	assert(path != NULL);
	assert(path[0] != '\0');

	if ((real = realpath(path, NULL)) == NULL) {
		rc = errno;
		goto chkerr;
	}

#ifdef WIN32
	if ( ( (real[0] == '/') || \
		((real[1] == ':') && (real[2] == '/')) ) && fullpath ) {
		char *slash;

		if (real[0] == '/')
			slash = strchr(&real[1], '/');
		else
			slash = strchr(&real[3], '/');

		while (slash != NULL) {
			*slash = '\0';		/* temp end of string */

			if (lstat(real, &sbuf) == -1) {
				rc = errno;
				goto chkerr;
			}

			assert(S_ISLNK(sbuf.st_mode) == 0);
			rc = teststat(&sbuf, 1, sticky, WRITES_MASK, real,
				log_buffer);
			if (rc != 0)
				goto chkerr;

			*slash = '/';
			slash = strchr(slash+1, '/');
		}
	}
#else
	if ((path[0] == '/') && fullpath) {
		/* check full path starting at root */
		char	*slash = strchr(&real[1], '/');

		while (slash != NULL) {
			*slash = '\0';		/* temp end of string */

			if (lstat(real, &sbuf) == -1) {
				rc = errno;
				goto chkerr;
			}

			assert(S_ISLNK(sbuf.st_mode) == 0);

			rc = tempstat(&sbuf, 1, sticky, 0);

			if (rc != 0)
				goto chkerr;

			*slash = '/';
			slash = strchr(slash+1, '/');
		}
	}
#endif

	if (lstat(real, &sbuf) == -1) {
		rc = errno;
		goto chkerr;
	}

	assert(S_ISLNK(sbuf.st_mode) == 0);

#ifdef WIN32
	rc = teststat(&sbuf, isdir, sticky, disallow, real, log_buffer);
#else
	rc = tempstat(&sbuf, isdir, sticky, disallow);
#endif

chkerr:
	if (rc != 0) {
		char *error_buf;

		pbs_asprintf(&error_buf,
			"Security violation \"%s\" resolves to \"%s\"",
			path, real);
		log_err(rc, __func__, error_buf);
#ifdef WIN32
		if (strlen(log_buffer) > 0)
			log_err(rc, __func__, log_buffer);
#endif
		free(error_buf);
	}

	free(real);
	return (rc);
}

/**
 * @brief - This function takes an <program name> <args> as input in string format
 *	    and returns the program name
 *
 * @return - char *
 * @retval - NULL when no valid program name is found
 * @retval - a newly allocated program name
 *
 * @NOTE - Caller holds the responsibility of freeing up the return value
 */
char *
get_script_name(char *input) {
	char * tok;
	char *next_space;
	int ret_fs;
	int path_exists = 0;
	char *prev_space = NULL;
	int starts_with_quotes = 0;
	char *delim = " ";
	struct stat sbuf;

	if (input == NULL)
		return NULL;

	/* if path starts with double quotes, skip it */
	if (input[0] == '\"') {
		input++;
		starts_with_quotes = 1;
	}

	tok = strdup(input);
	if (tok == NULL)
		return NULL;

	/* get rid of double quotes at the end */
	if (starts_with_quotes && tok[strlen(tok)-1] == '\"')
		tok[strlen(tok)-1] = '\0';

	next_space = strpbrk(tok, delim);
	for (; next_space != NULL; next_space = strpbrk(next_space, delim)) {
		*next_space = '\0';
		memset (&sbuf, 0, sizeof(struct stat));
		ret_fs = stat(tok, &sbuf);
		if (ret_fs != 0) {
			if (path_exists == 1) {
				*prev_space = '\0';
				return (tok);
			}
		}
		else if (S_ISREG(sbuf.st_mode)) {
			path_exists = 1;
			prev_space = next_space;
		}
		*next_space = ' ';
		next_space += strspn(next_space, delim);
	}

	if (path_exists == 1) {
		/* set last known space as '\0' so that returned path contains no arguments */
		*prev_space = '\0';
		return tok;
	}

	/* If control is here then it would mean that "tok" must have file path*/
	memset (&sbuf, 0, sizeof(struct stat));
	ret_fs = stat(tok, &sbuf);
	if (S_ISREG(sbuf.st_mode))
		return tok;

	/* No file found */
	free(tok);
	return NULL;
}
