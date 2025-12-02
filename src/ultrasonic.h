#ifndef ULTRASONIC_H
#define ULTRASONIC_H

#ifdef __cplusplus
extern "C" {
#endif

// Initialize trigger + echo pins
void ultrasonic_init();

// Returns distance in cm
//   - Valid range: 2 cm → 400 cm
//   - Returns -1 on timeout or invalid reading
int ultrasonic_get_distance_cm();

#ifdef __cplusplus
}
#endif

#endif // ULTRASONIC_H
