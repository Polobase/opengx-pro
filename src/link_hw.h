/* Board access used by the link modules: PB1/PB2, soft-PWM duties, delay_loop, system reset.
 * Host unit tests (HOST_TEST) replace board.h with a mock of the same names. */
#pragma once

#ifdef HOST_TEST
#include "mock_board.h"
#else
#include "board.h"
#endif
