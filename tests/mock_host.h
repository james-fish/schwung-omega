/* mock_host.h — native mock host_api_v1_t for the offline test harness. */
#ifndef OMEGA_MOCK_HOST_H
#define OMEGA_MOCK_HOST_H

#include "omega.h"

/* Returns a populated mock host (44100 sr, 128 fpb, stub callbacks). Also RESETS
 * the drivable transport (beat=0, bpm=120) so each test starts from a known
 * state. */
host_api_v1_t make_mock_host(void);

/* Variant with BOTH transport callbacks NULL (get_beat_position + get_bpm), to
 * exercise the groove tempo clock's last-resort 120 constant path (GRV-02). */
host_api_v1_t make_mock_host_null_transport(void);

/* Drive the mock transport (GRV-02). All are off the audio thread. */
void mock_host_set_beat(double beat);       /* set absolute beat position */
void mock_host_advance_beat(double dbeat);  /* advance beat by dbeat quarter notes */
void mock_host_set_bpm(float bpm);          /* set the BPM mock_get_bpm reports */

#endif /* OMEGA_MOCK_HOST_H */
