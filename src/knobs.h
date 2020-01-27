#ifndef _KNOBS_H
#define _KNOBS_H

#include <assert.h>

/* A knob exposes a binding for some float in a module's context
 * which can be varied at runtime between frames to influence
 * the output.  There's some overlap with settings, but settings
 * are more intended for configuration applied at context
 * creation, which won't vary frame-to-frame, but may influence
 * the initial value and/or automatic behavior of knobs for
 * instance, or even which knobs are available.
 *
 * At this time knobs will only apply to floats, accompanied by
 * some rudimentary bounds.
 *
 * Integer types would probably be useful, and maybe a precision
 * specifier, those can be added in the future as needed, but I'd
 * like to keep it simple for now and see what kind of problems
 * emerge.
 *
 * The current expectation is that a module context struct will
 * incorporate an array of knobs, replacing loose floats already
 * being automatically varied within the module frame-to-frame.
 *
 * The module will then use the knob_auto_* helpers below to
 * access and manipulate the values, instead of directly
 * accessing the loose floats as before.
 *
 * External manipulators of the knobs will use the knob_*
 * helpers, instead of the knob_auto_* helpers, to
 * access+manipulate the knobs.  These helpers are basically just
 * to get external and internal manipulators to agree on which
 * side owns control via the managed field.
 */
typedef struct knob_t {
	const char	*name;		/* Short API-oriented name */
	const char	*desc;		/* Longer UI-oriented name */
	const float	min, max;	/* Value bounds */
	float		value;		/* Value knob affects */
	unsigned	managed:1;	/* Set when knob control of value is active,
					 * suppress automagic control of value when set.
					 */
} knob_t;


/* helper for modules automating knob controls, use this to
 * change values intead of direct manipulation to respect active.
 * returns new (or unchanged) value
 */
static inline float knob_auto_set(knob_t *knob, float value)
{
	assert(knob);

	if (knob->managed)
		return knob->value;

	return knob->value = value;
}

/* identical to knob_auto_set, except adds to existing value.
 */
static inline float knob_auto_add(knob_t *knob, float value)
{
	assert(knob);

	return knob_auto_set(knob, knob->value + value);
}


/* identical to knob_auto* variants, except intended for
 * external knob-twisters, i.e. the "managed" knob entrypoints.
 */
static inline float knob_set(knob_t *knob, float value)
{
	assert(knob);

	knob->managed = 1;

	return knob->value = value;
}


static inline float knob_add(knob_t *knob, float value)
{
	assert(knob);

	return knob_set(knob, knob->value + value);
}

#endif
