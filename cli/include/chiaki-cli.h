// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_CHIAKI_CLI_H
#define CHIAKI_CHIAKI_CLI_H

#include <chiaki/common.h>
#include <chiaki/log.h>
#include <chiaki/controller.h>

#ifdef __cplusplus
extern "C" {
#endif

CHIAKI_EXPORT int chiaki_cli_cmd_discover(ChiakiLog *log, int argc, char *argv[]);
CHIAKI_EXPORT int chiaki_cli_cmd_wakeup(ChiakiLog *log, int argc, char *argv[]);
CHIAKI_EXPORT int chiaki_cli_cmd_stream(ChiakiLog *log, int argc, char *argv[]);

CHIAKI_EXPORT uint32_t chiaki_cli_parse_button_name(const char *name);
CHIAKI_EXPORT void chiaki_cli_parse_json_input(ChiakiControllerState *state, const char *line);
CHIAKI_EXPORT void chiaki_cli_parse_lines_input(ChiakiControllerState *state, const char *line);

#ifdef __cplusplus
}
#endif

#endif //CHIAKI_CHIAKI_CLI_H
