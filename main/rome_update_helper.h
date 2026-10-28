#pragma once

#ifdef TOOLS_ENABLED
// User arguments (after --) must start with --rome-update-helper.
// Returns true when handled; the caller must exit without creating a MainLoop.
// Optional --cancel-file <absolute ticket path>: the manager creates the ticket
// before launch and removes it if quit/save is cancelled. A missing ticket aborts
// before installation. The helper cleans up its unchanged ticket when it exits.
// Optional --status-file <absolute path>: JSON with stage (validating, waiting,
// installing, done, failed, cancelled), code and a Spanish message. "waiting"
// means everything is prepared and the editor may close.
// The helper is always the newer executable, launched by an older manager: keep
// accepting every argument older managers pass, and add new ones as optional.
bool rome_run_update_helper_if_requested(int &r_exit_code);
#endif
