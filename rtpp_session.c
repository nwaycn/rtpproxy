/*
 * Copyright (c) 2004-2006 Maxim Sobolev <sobomax@FreeBSD.org>
 * Copyright (c) 2006-2007 Sippy Software, Inc., http://www.sippysoft.com
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id$
 *
 */

#include <sys/types.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rtpp_defines.h"
#include "rtpp_log.h"
#include "rtpp_record.h"
#include "rtpp_session.h"

void
init_hash_table(struct cfg *cf)
{
    int i;

    for (i = 0; i < 256; i++) {
	cf->rand_table[i] = random();
    }
}

static uint8_t
hash_string(struct cfg *cf, char *bp, char *ep)
{
    uint8_t res;

    for (res = cf->rand_table[0]; bp[0] != '\0' && bp != ep; bp++) {
	res = cf->rand_table[res ^ bp[0]];
    }
    return res;
}

void
hash_table_append(struct cfg *cf, struct rtpp_session *sp)
{
    uint8_t hash;
    struct rtpp_session *tsp;

    assert(sp->rtcp != NULL);

    hash = hash_string(cf, sp->call_id, NULL);

    rtpp_log_write(RTPP_LOG_DBUG, cf->glog, "hash_table_append: hash(%s) = %d", sp->call_id, hash);

    tsp = cf->hash_table[hash];
    if (tsp == NULL) {
	cf->hash_table[hash] = sp;
	sp->prev = sp->next = NULL;
	return;
    }
    while (tsp->next != NULL) {
	tsp = tsp->next;
    }
    tsp->next = sp;
    sp->prev = tsp;
    sp->next = NULL;
}

static void
hash_table_remove(struct cfg *cf, struct rtpp_session *sp)
{
    uint8_t hash;

    assert(sp->rtcp != NULL);

    if (sp->prev != NULL) {
	sp->prev->next = sp->next;
	if (sp->next != NULL) {
	    sp->next->prev = sp->prev;
	}
	return;
    }
    hash = hash_string(cf, sp->call_id, NULL);
    /* Make sure we are removing the right session */
    assert(cf->hash_table[hash] == sp);
    cf->hash_table[hash] = sp->next;
    if (sp->next != NULL) {
	sp->next->prev = NULL;
    }
}

struct rtpp_session *
hash_table_findfirst(struct cfg *cf, char *call_id)
{
    uint8_t hash;
    struct rtpp_session *sp;

    hash = hash_string(cf, call_id, NULL);
    for (sp = cf->hash_table[hash]; sp != NULL; sp = sp->next) {
	if (strcmp(sp->call_id, call_id) == 0) {
	    break;
	}
    }
    return (sp);
}

struct rtpp_session *
hash_table_findnext(struct rtpp_session *psp)
{
    struct rtpp_session *sp;

    for (sp = psp->next; sp != NULL; sp = sp->next) {
	if (strcmp(sp->call_id, psp->call_id) == 0) {
	    break;
	}
    }
    return (sp);
}

void
append_session(struct cfg *cf, struct rtpp_session *sp, int index)
{

    if (sp->fds[index] != -1) {
	cf->sessions[cf->nsessions] = sp;
	cf->pfds[cf->nsessions].fd = sp->fds[index];
	cf->pfds[cf->nsessions].events = POLLIN;
	cf->pfds[cf->nsessions].revents = 0;
	sp->sidx[index] = cf->nsessions;
	cf->nsessions++;
    } else {
	sp->sidx[index] = -1;
    }
}

void
remove_session(struct cfg *cf, struct rtpp_session *sp)
{
    int i;

    rtpp_log_write(RTPP_LOG_INFO, sp->log, "RTP stats: %lu in from callee, %lu "
      "in from caller, %lu relayed, %lu dropped", sp->pcount[0], sp->pcount[1],
      sp->pcount[2], sp->pcount[3]);
    rtpp_log_write(RTPP_LOG_INFO, sp->log, "RTCP stats: %lu in from callee, %lu "
      "in from caller, %lu relayed, %lu dropped", sp->rtcp->pcount[0],
      sp->rtcp->pcount[1], sp->rtcp->pcount[2], sp->rtcp->pcount[3]);
    rtpp_log_write(RTPP_LOG_INFO, sp->log, "session on ports %d/%d is cleaned up",
      sp->ports[0], sp->ports[1]);
    for (i = 0; i < 2; i++) {
	if (sp->addr[i] != NULL)
	    free(sp->addr[i]);
	if (sp->rtcp->addr[i] != NULL)
	    free(sp->rtcp->addr[i]);
	if (sp->fds[i] != -1) {
	    close(sp->fds[i]);
	    assert(cf->sessions[sp->sidx[i]] == sp);
	    cf->sessions[sp->sidx[i]] = NULL;
	    assert(cf->pfds[sp->sidx[i]].fd == sp->fds[i]);
	    cf->pfds[sp->sidx[i]].fd = -1;
	    cf->pfds[sp->sidx[i]].events = 0;
	}
	if (sp->rtcp->fds[i] != -1) {
	    close(sp->rtcp->fds[i]);
	    assert(cf->sessions[sp->rtcp->sidx[i]] == sp->rtcp);
	    cf->sessions[sp->rtcp->sidx[i]] = NULL;
	    assert(cf->pfds[sp->rtcp->sidx[i]].fd == sp->rtcp->fds[i]);
	    cf->pfds[sp->rtcp->sidx[i]].fd = -1;
	    cf->pfds[sp->rtcp->sidx[i]].events = 0;
	}
	if (sp->rrcs[i] != NULL)
	    rclose(sp, sp->rrcs[i]);
	if (sp->rtcp->rrcs[i] != NULL)
	    rclose(sp, sp->rtcp->rrcs[i]);
	if (sp->rtps[i] != NULL) {
	    cf->rtp_servers[sp->sridx] = NULL;
	    rtp_server_free(sp->rtps[i]);
	}
    }
    hash_table_remove(cf, sp);
    if (sp->call_id != NULL)
	free(sp->call_id);
    if (sp->tag != NULL)
	free(sp->tag);
    rtpp_log_close(sp->log);
    free(sp->rtcp);
    rtp_resizer_free(&sp->resizers[0]);
    rtp_resizer_free(&sp->resizers[1]);
    free(sp);
    cf->sessions_active--;
}

int
compare_session_tags(char *tag1, char *tag0, unsigned *medianum_p)
{
    size_t len0 = strlen(tag0);

    if (!strncmp(tag1, tag0, len0)) {
	if (tag1[len0] == ';') {
	    if (medianum_p != NULL)
		*medianum_p = strtoul(tag1 + len0 + 1, NULL, 10);
	    return 2;
	}
	if (tag1[len0] == '\0')
	    return 1;
	return 0;
    }
    return 0;
}