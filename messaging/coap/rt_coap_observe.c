/****************************************************************************
 *
 * Copyright 2017 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/

/*
 * Summary of modifications from original source code
 * - coap_add_observer() is renamed and changed marginally.
 * - coap_remove_observer() is renamed.
 * - coap_remove_observer_by_mid() is renamed.
 * - coap_remove_observer_by_token()/coap_remove_observer_by_uri()/coap_remove_observer_by_mid() is renamed.
 * - coap_notify_observers() is renamed and the function body is replaced with rt_coap_notify_observsers_sub().
 * - coap_observe_handler() is renamed.
 */

/*
 * Copyright (c) 2013, Institute for Pervasive Computing, ETH Zurich
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
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 */

/**
 * \file
 *      CoAP module for observing resources (draft-ietf-core-observe-11).
 * \author
 *      Matthias Kovatsch <kovatsch@inf.ethz.ch>
 */

#include <stdio.h>
#include <string.h>
#include "er-coap-observe.h"

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINT6ADDR(addr) PRINTF("[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]", ((uint8_t *)addr)[0], ((uint8_t *)addr)[1], ((uint8_t *)addr)[2], ((uint8_t *)addr)[3], ((uint8_t *)addr)[4], ((uint8_t *)addr)[5], ((uint8_t *)addr)[6], ((uint8_t *)addr)[7], ((uint8_t *)addr)[8], ((uint8_t *)addr)[9], ((uint8_t *)addr)[10], ((uint8_t *)addr)[11], ((uint8_t *)addr)[12], ((uint8_t *)addr)[13], ((uint8_t *)addr)[14], ((uint8_t *)addr)[15])
#define PRINTLLADDR(lladdr) PRINTF("[%02x:%02x:%02x:%02x:%02x:%02x]", (lladdr)->addr[0], (lladdr)->addr[1], (lladdr)->addr[2], (lladdr)->addr[3], (lladdr)->addr[4], (lladdr)->addr[5])
#else
#define PRINTF(...)
#define PRINT6ADDR(addr)
#define PRINTLLADDR(addr)
#endif

/*---------------------------------------------------------------------------*/
MEMB(observers_memb, coap_observer_t, COAP_MAX_OBSERVERS);
LIST(observers_list);
/*---------------------------------------------------------------------------*/
/*- Internal API ------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
static coap_observer_t *add_observer(uint8_t *addr, uint16_t port, const uint8_t *token, size_t token_len, const char *uri, int uri_len)
{
	/* Remove existing observe relationship, if any. */
	rt_coap_remove_observer_by_uri(addr, port, uri);

	coap_observer_t *o = memb_alloc(&observers_memb);

	if (o) {
		int max = sizeof(o->url) - 1;
		if (max > uri_len) {
			max = uri_len;
		}
		memcpy(o->url, uri, max);
		o->url[max] = 0;
		uip_ipaddr_copy(&o->addr, addr);
		o->port = port;
		o->token_len = token_len;
		memcpy(o->token, token, token_len);
		o->last_mid = 0;

		PRINTF("Adding observer (%u/%u) for /%s [0x%02X%02X]\n", list_length(observers_list) + 1, COAP_MAX_OBSERVERS, o->url, o->token[0], o->token[1]);
		list_add(observers_list, o);
	}

	return o;
}

/*---------------------------------------------------------------------------*/
/*- Removal -----------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
void rt_coap_remove_observer(coap_observer_t *o)
{
	PRINTF("Removing observer for /%s [0x%02X%02X]\n", o->url, o->token[0], o->token[1]);

	memb_free(&observers_memb, o);
	list_remove(observers_list, o);
}

/*---------------------------------------------------------------------------*/
int rt_coap_remove_observer_by_client(uint8_t *addr, uint16_t port)
{
	int removed = 0;
	coap_observer_t *obs = NULL;

	for (obs = (coap_observer_t *) list_head(observers_list); obs; obs = obs->next) {
		PRINTF("Remove check client ");
		PRINT6ADDR(addr);
		PRINTF(":%u\n", port);
		if (uip_ipaddr_cmp(&obs->addr, addr) && obs->port == port) {
			rt_coap_remove_observer(obs);
			removed++;
		}
	}
	return removed;
}

/*---------------------------------------------------------------------------*/
int rt_coap_remove_observer_by_token(uint8_t *addr, uint16_t port, uint8_t *token, size_t token_len)
{
	int removed = 0;
	coap_observer_t *obs = NULL;

	for (obs = (coap_observer_t *) list_head(observers_list); obs; obs = obs->next) {
		PRINTF("Remove check Token 0x%02X%02X\n", token[0], token[1]);
		if (uip_ipaddr_cmp(&obs->addr, addr) && obs->port == port && obs->token_len == token_len && memcmp(obs->token, token, token_len) == 0) {
			rt_coap_remove_observer(obs);
			removed++;
		}
	}
	return removed;
}

/*---------------------------------------------------------------------------*/
int rt_coap_remove_observer_by_uri(uint8_t *addr, uint16_t port, const char *uri)
{
	int removed = 0;
	coap_observer_t *obs = NULL;

	for (obs = (coap_observer_t *) list_head(observers_list); obs; obs = obs->next) {
		PRINTF("Remove check URL %p\n", uri);
		if ((addr == NULL || (uip_ipaddr_cmp(&obs->addr, addr) && obs->port == port))
			&& (obs->url == uri || memcmp(obs->url, uri, strlen(obs->url)) == 0)) {
			rt_coap_remove_observer(obs);
			removed++;
		}
	}
	return removed;
}

/*---------------------------------------------------------------------------*/
int rt_coap_remove_observer_by_mid(uint8_t *addr, uint16_t port, uint16_t mid)
{
	int removed = 0;
	coap_observer_t *obs = NULL;

	for (obs = (coap_observer_t *) list_head(observers_list); obs; obs = obs->next) {
		PRINTF("Remove check MID %u\n", mid);
		if (uip_ipaddr_cmp(&obs->addr, addr) && obs->port == port && obs->last_mid == mid) {
			rt_coap_remove_observer(obs);
			removed++;
		}
	}
	return removed;
}

/*---------------------------------------------------------------------------*/
/*- Notification ------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
void rt_coap_notify_observers(resource_t *resource)
{
	rt_coap_notify_observers_sub(resource, NULL);
}

void rt_coap_notify_observers_sub(resource_t *resource, const char *subpath)
{
	/* build notification */
	coap_packet_t notification[1];	/* this way the packet can be treated as pointer as usual */
	coap_packet_t request[1];	/* this way the packet can be treated as pointer as usual */
	coap_observer_t *obs = NULL;
	int url_len, obs_url_len;
	char url[COAP_OBSERVER_URL_LEN];

	url_len = strlen(resource->url);
	rt_strncpy(url, resource->url, url_len);
	if (url_len < COAP_OBSERVER_URL_LEN - 1 && subpath != NULL) {
		rt_strncpy(&url[url_len], subpath, COAP_OBSERVER_URL_LEN - url_len - 1);
	}
	/* url now contains the notify URL that needs to match the observer */
	PRINTF("Observe: Notification from %s\n", url);

	rt_coap_init_message(notification, COAP_TYPE_NON, CONTENT_2_05, 0);
	/* create a "fake" request for the URI */
	rt_coap_init_message(request, COAP_TYPE_CON, COAP_GET, 0);
	rt_coap_set_header_uri_path(request, url);

	/* iterate over observers */
	url_len = strlen(url);
	for (obs = (coap_observer_t *) list_head(observers_list); obs; obs = obs->next) {
		obs_url_len = strlen(obs->url);

		/* Do a match based on the parent/sub-resource match so that it is
		   possible to do parent-node observe */
		if ((obs_url_len == url_len || (obs_url_len > url_len && (resource->flags & HAS_SUB_RESOURCES)
										&& obs->url[url_len] == '/'))
			&& strncmp(url, obs->url, url_len) == 0) {
			coap_transaction_t *transaction = NULL;

			/*TODO implement special transaction for CON, sharing the same buffer to allow for more observers */

			if ((transaction = rt_coap_new_transaction(rt_coap_get_mid(), &obs->addr, obs->port))) {
				if (obs->obs_counter % COAP_OBSERVE_REFRESH_INTERVAL == 0) {
					PRINTF("           Force Confirmable for\n");
					notification->type = COAP_TYPE_CON;
				}

				PRINTF("           Observer ");
				PRINT6ADDR(&obs->addr);
				PRINTF(":%u\n", obs->port);

				/* update last MID for RST matching */
				obs->last_mid = transaction->mid;

				/* prepare response */
				notification->mid = transaction->mid;

				resource->get_handler(request, notification, transaction->packet + COAP_MAX_HEADER_SIZE, REST_MAX_CHUNK_SIZE, NULL);

				if (notification->code < BAD_REQUEST_4_00) {
					rt_coap_set_header_observe(notification, (obs->obs_counter)++);
					/* mask out to keep the CoAP observe option length <= 3 bytes */
					obs->obs_counter &= 0xffffff;
				}
				rt_coap_set_token(notification, obs->token, obs->token_len);

				transaction->packet_len = rt_coap_serialize_message(notification, transaction->packet);

				rt_coap_send_transaction(transaction);
			}
		}
	}
}

/*---------------------------------------------------------------------------*/
void rt_coap_observe_handler(resource_t *resource, void *request, void *response)
{
	coap_packet_t *const coap_req = (coap_packet_t *) request;
	coap_packet_t *const coap_res = (coap_packet_t *) response;
	coap_observer_t *obs;

	if (coap_req->code == COAP_GET && coap_res->code < 128) {	/* GET request and response without error code */
		if (IS_OPTION(coap_req, COAP_OPTION_OBSERVE)) {
			if (coap_req->observe == 0) {
				obs = add_observer(&UIP_IP_BUF->srcipaddr, UIP_UDP_BUF->srcport, coap_req->token, coap_req->token_len, coap_req->uri_path, coap_req->uri_path_len);
				if (obs) {
					rt_coap_set_header_observe(coap_res, (obs->obs_counter)++);
					/* mask out to keep the CoAP observe option length <= 3 bytes */
					obs->obs_counter &= 0xffffff;
					/*
					 * Following payload is for demonstration purposes only.
					 * A subscription should return the same representation as a normal GET.
					 * Uncomment if you want an information about the avaiable observers.
					 */
#if 0
					static char content[16];
					rt_coap_set_payload(coap_res, content, snprintf(content, sizeof(content), "Added %u/%u", list_length(observers_list), COAP_MAX_OBSERVERS));
#endif
				} else {
					coap_res->code = SERVICE_UNAVAILABLE_5_03;
					rt_coap_set_payload(coap_res, "TooManyObservers", 16);
				}
			} else if (coap_req->observe == 1) {

				/* remove client if it is currently observe */
				rt_coap_remove_observer_by_token(&UIP_IP_BUF->srcipaddr, UIP_UDP_BUF->srcport, coap_req->token, coap_req->token_len);
			}
		}
	}
}

/*---------------------------------------------------------------------------*/
