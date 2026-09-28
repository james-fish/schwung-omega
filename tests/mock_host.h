/* mock_host.h — native mock host_api_v1_t for the offline test harness. */
#ifndef OMEGA_MOCK_HOST_H
#define OMEGA_MOCK_HOST_H

#include "omega.h"

/* Returns a populated mock host (44100 sr, 128 fpb, stub callbacks). */
host_api_v1_t make_mock_host(void);

#endif /* OMEGA_MOCK_HOST_H */
