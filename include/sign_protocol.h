#ifndef H_LAPIS_SIGN_PROTOCOL
#define H_LAPIS_SIGN_PROTOCOL
#include <stdint.h>
#include "protocol.h"
/* Maintained minimal Java 1.21.8 compatibility snapshot. See docs/signs.md. */
_Static_assert(LAPIS_PROTOCOL_VERSION == 772, "Update sign states, entity type and packets for this protocol");
#define SIGN_ENTITY_TYPE 7
static const uint16_t sign_standing_states[16] = {
  4367,4369,4371,4373,4375,4377,4379,4381,4383,4385,4387,4389,4391,4393,4395,4397
};
static const uint16_t sign_wall_states[4] = {4859,4861,4863,4865};
#endif
