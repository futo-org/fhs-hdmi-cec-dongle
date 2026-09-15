#ifndef CEC_DIAG_H
#define CEC_DIAG_H

/* Bench instrumentation for the CEC line.
 *
 * Everything here borrows the pin back from the peripheral and measures it
 * directly: pull-up probe, edge capture, activity summary. It exists because
 * this bench has no scope, and it is what found the two faults during
 * bring-up (a floating line, then a TV that would not listen). For fault
 * finding, not for normal operation.
 */

/* Run one `diag` subcommand. Prints usage if it does not recognise one. */
void cec_diag_command(const char *args);

#endif
