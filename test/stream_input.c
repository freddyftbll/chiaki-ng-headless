// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <munit.h>
#include <chiaki-cli.h>
#include <chiaki/controller.h>
#include <string.h>

static MunitResult test_button_names(const MunitParameter params[], void *user_data)
{
	(void)params;
	(void)user_data;

	munit_assert_uint32(chiaki_cli_parse_button_name("CROSS"), ==, CHIAKI_CONTROLLER_BUTTON_CROSS);
	munit_assert_uint32(chiaki_cli_parse_button_name("cross"), ==, CHIAKI_CONTROLLER_BUTTON_CROSS);
	munit_assert_uint32(chiaki_cli_parse_button_name("X"), ==, CHIAKI_CONTROLLER_BUTTON_CROSS);

	munit_assert_uint32(chiaki_cli_parse_button_name("MOON"), ==, CHIAKI_CONTROLLER_BUTTON_MOON);
	munit_assert_uint32(chiaki_cli_parse_button_name("circle"), ==, CHIAKI_CONTROLLER_BUTTON_MOON);
	munit_assert_uint32(chiaki_cli_parse_button_name("O"), ==, CHIAKI_CONTROLLER_BUTTON_MOON);

	munit_assert_uint32(chiaki_cli_parse_button_name("BOX"), ==, CHIAKI_CONTROLLER_BUTTON_BOX);
	munit_assert_uint32(chiaki_cli_parse_button_name("square"), ==, CHIAKI_CONTROLLER_BUTTON_BOX);

	munit_assert_uint32(chiaki_cli_parse_button_name("PYRAMID"), ==, CHIAKI_CONTROLLER_BUTTON_PYRAMID);
	munit_assert_uint32(chiaki_cli_parse_button_name("triangle"), ==, CHIAKI_CONTROLLER_BUTTON_PYRAMID);

	munit_assert_uint32(chiaki_cli_parse_button_name("DPAD_UP"), ==, CHIAKI_CONTROLLER_BUTTON_DPAD_UP);
	munit_assert_uint32(chiaki_cli_parse_button_name("down"), ==, CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN);
	munit_assert_uint32(chiaki_cli_parse_button_name("left"), ==, CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT);
	munit_assert_uint32(chiaki_cli_parse_button_name("right"), ==, CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT);

	munit_assert_uint32(chiaki_cli_parse_button_name("L1"), ==, CHIAKI_CONTROLLER_BUTTON_L1);
	munit_assert_uint32(chiaki_cli_parse_button_name("R1"), ==, CHIAKI_CONTROLLER_BUTTON_R1);
	munit_assert_uint32(chiaki_cli_parse_button_name("L3"), ==, CHIAKI_CONTROLLER_BUTTON_L3);
	munit_assert_uint32(chiaki_cli_parse_button_name("R3"), ==, CHIAKI_CONTROLLER_BUTTON_R3);
	munit_assert_uint32(chiaki_cli_parse_button_name("OPTIONS"), ==, CHIAKI_CONTROLLER_BUTTON_OPTIONS);
	munit_assert_uint32(chiaki_cli_parse_button_name("SHARE"), ==, CHIAKI_CONTROLLER_BUTTON_SHARE);
	munit_assert_uint32(chiaki_cli_parse_button_name("CREATE"), ==, CHIAKI_CONTROLLER_BUTTON_SHARE);
	munit_assert_uint32(chiaki_cli_parse_button_name("TOUCHPAD"), ==, CHIAKI_CONTROLLER_BUTTON_TOUCHPAD);
	munit_assert_uint32(chiaki_cli_parse_button_name("PS"), ==, CHIAKI_CONTROLLER_BUTTON_PS);

	munit_assert_uint32(chiaki_cli_parse_button_name("UNKNOWN"), ==, 0);

	return MUNIT_OK;
}

static MunitResult test_json_input(const MunitParameter params[], void *user_data)
{
	(void)params;
	(void)user_data;

	ChiakiControllerState state;
	chiaki_controller_state_set_idle(&state);

	const char *json_line = "{\"buttons\": [\"CROSS\", \"DPAD_UP\"], \"left_x\": -1234, \"left_y\": 5678, \"r2\": 240}";
	chiaki_cli_parse_json_input(&state, json_line);

	munit_assert_uint32(state.buttons, ==, (CHIAKI_CONTROLLER_BUTTON_CROSS | CHIAKI_CONTROLLER_BUTTON_DPAD_UP));
	munit_assert_int16(state.left_x, ==, -1234);
	munit_assert_int16(state.left_y, ==, 5678);
	munit_assert_uint8(state.r2_state, ==, 240);

	// Test touch parsing
	const char *touch_line = "{\"touches\": [{\"id\": 1, \"x\": 500, \"y\": 300}]}";
	chiaki_cli_parse_json_input(&state, touch_line);
	munit_assert_int8(state.touches[0].id, ==, 1);
	munit_assert_uint16(state.touches[0].x, ==, 500);
	munit_assert_uint16(state.touches[0].y, ==, 300);

	// Test idle reset
	const char *idle_line = "{\"idle\": true}";
	chiaki_cli_parse_json_input(&state, idle_line);
	munit_assert_uint32(state.buttons, ==, 0);
	munit_assert_int16(state.left_x, ==, 0);
	munit_assert_int16(state.left_y, ==, 0);
	munit_assert_uint8(state.r2_state, ==, 0);

	return MUNIT_OK;
}

static MunitResult test_lines_input(const MunitParameter params[], void *user_data)
{
	(void)params;
	(void)user_data;

	ChiakiControllerState state;
	chiaki_controller_state_set_idle(&state);

	chiaki_cli_parse_lines_input(&state, "DOWN CROSS");
	munit_assert_uint32(state.buttons, ==, CHIAKI_CONTROLLER_BUTTON_CROSS);

	chiaki_cli_parse_lines_input(&state, "DOWN R1");
	munit_assert_uint32(state.buttons, ==, (CHIAKI_CONTROLLER_BUTTON_CROSS | CHIAKI_CONTROLLER_BUTTON_R1));

	chiaki_cli_parse_lines_input(&state, "UP CROSS");
	munit_assert_uint32(state.buttons, ==, CHIAKI_CONTROLLER_BUTTON_R1);

	chiaki_cli_parse_lines_input(&state, "AXIS LX -16384");
	munit_assert_int16(state.left_x, ==, -16384);

	chiaki_cli_parse_lines_input(&state, "TRIGGER L2 180");
	munit_assert_uint8(state.l2_state, ==, 180);

	chiaki_cli_parse_lines_input(&state, "RESET");
	munit_assert_uint32(state.buttons, ==, 0);
	munit_assert_int16(state.left_x, ==, 0);
	munit_assert_uint8(state.l2_state, ==, 0);

	return MUNIT_OK;
}

MunitTest tests_stream_input[] = {
	{ "/button_names", test_button_names, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/json_input", test_json_input, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/lines_input", test_lines_input, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};
