/* neuipc include/ipc.h */

#ifndef SWC_IPC_H
#define SWC_IPC_H

#include <stdint.h>
#include <stdbool.h>

#define SWC_IPC_MAX_MSG    4096
#define SWC_IPC_MAX_ARGS   32
#define SWC_IPC_MAX_TOPICS 16


/*
 * esponse written back to a client after a command or query
 */
typedef struct {
	bool ok;
	char msg[SWC_IPC_MAX_MSG];
} swc_ipc_status;

/*
 * handler function signature
 * args[0] is command name
 * args[1+] are whitespace delimited arguments, NULL terminated
 * userdata is whatever was passed to swc_ipc_register()
 *
 * return an swc_ipc_status. msg may be empty for pure ctl cmds
 */
typedef swc_ipc_status (*swc_ipc_handler_fn) (char **args, void *userdata);

/*
 * init the library, creating domain socket at ctl_path
 * connections can be one shot (cmd/query) or upgraded
 * to subscribers via "sub"
 *
 * returns 0 on success, -1 on error
 */
int swc_ipc_init(const char *ctl_path);

/*
 * tear down socket and free all resources
 */
void swc_ipc_finish(void);

/*
 * fd to add to wl_event_loop (or some polling mech)
 * call swc_ipc_dispatch() when it becomes readable
 */
int swc_ipc_get_fd(void);

/*
 * driver
 * accepts new connects, dispatches cmds, manages subs
 */
void swc_ipc_dispatch(void);

/*
 * register handler for a cmd or query
 * overwrites existing handler with same name
 *
 * "sub" is reserved and can't be registered
 */
void swc_ipc_register(const char *name, swc_ipc_handler_fn fn, void *userdata);

/*
 * unregister handler
 */
void swc_ipc_unregister(const char *name);

/*
 * emit event on topic. all subs recieve:
 *
 * 	"<topic> <msg>\n"
 *
 * use topic "*" to broadcast to all subs no matter their topic
 * returns the number of subs message was delivered to
 */
int swc_ipc_emit(const char *topic, const char *fmt, ...);

/*
 * macro to emit to all
 */
#define swc_ipc_broadcast(fmt, ...) swc_ipc_emit("*", fmt, ##__VA_ARGS__)

/*
 * corresponding built-in "sub" cmd wire format
 *
 * 	sub <topic> [topic2] ... [topicN]
 *
 * connection is kept open and client receives events matching
 * any of the topics. good for workspaces, etc., but multi topic will probably 
 * be usless for a lot of applications.
 *
 */

#endif /* SWC_IPC_H */
