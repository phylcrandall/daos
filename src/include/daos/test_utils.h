/*
 * (C) Copyright 2018 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/**
 * Convenient unit test utility methods
 */

#ifndef __DAOS_TEST_UTILS_H__
#define __DAOS_TEST_UTILS_H__

#include <daos/drpc.h>
#include <daos/drpc.pb-c.h>

/*
 * drpc unit test utilities
 */

/**
 * Creates a new drpc context with a specified socket fd. Not tied to anything
 * in the real file system.
 *
 * \param	fd	desired socket fd
 *
 * \return	Newly allocated struct drpc
 */
struct drpc *new_drpc_with_fd(int fd);

/**
 * Frees a drpc context and cleans up. Not tied to anything in the real file
 * system.
 *
 * \param	ctx	drpc ctx to free
 */
void free_drpc(struct drpc *ctx);

/**
 * Generates a valid Drpc__Call structure.
 *
 * \return	Newly allocated Drpc__Call
 */
Drpc__Call *new_drpc_call(void);

/**
 * Using mocks in test_mocks.h, sets up recvmsg mock to populate a valid
 * serialized Drpc__Call as the message received.
 */
void mock_valid_drpc_call_in_recvmsg(void);

/**
 * Generates a valid Drpc__Response structure.
 *
 * \return	Newly allocated Drpc__Response
 */
Drpc__Response *new_drpc_response(void);

#endif /* __DAOS_TEST_UTILS_H__ */
