/*
HOST_TOUCH.C

The touch controls' host services (port/android/host/host_touch.c, which
draws the overlay and reads its controls) for the desktop. The macOS build
has no touch screen and no touch overlay (docs/macos-controls.md), so the
guest is told that nothing is touched, nothing is looked at, nothing rumbles
and no gesture edges need avoiding. The guest asks for these every frame
(port/linux/src/touch_input.c), so each answers as an absent touch screen does.
*/

#include "macos_host.h"

void host_touch_scene(int scene)
{
	(void)scene;
}

void host_touch_bindings(const int *controls)
{
	(void)controls;
}

/* the controller's state: seven words, all released */
void host_touch_read(int *state)
{
	int index;

	for (index = 0; index < 7; index++)
		state[index] = 0;
}

/* the look's deltas: four floats, none moved */
void host_touch_look_read(float *delta)
{
	int index;

	for (index = 0; index < 4; index++)
		delta[index] = 0.0f;
}

void host_touch_rumble(unsigned int low, unsigned int high)
{
	(void)low;
	(void)high;
}

/* the system's gesture insets (left, top, right, bottom): none */
void host_gesture_insets(int *insets)
{
	int index;

	for (index = 0; index < 4; index++)
		insets[index] = 0;
}
