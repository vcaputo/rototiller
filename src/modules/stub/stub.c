#include "til.h"
#include "til_fb.h"

/* Sample module fills the frame with white pixels in a non-threaded manner,
 * replace the body of stub_render_fragment() with your own algorithm.
 *
 * To finalize a module implementation derived from this stub, perform a global
 * substitution of "stub" with your module's name, including copying into
 * src/modules/$name, and updating all the build system and til.c references.
 *
 * A quick way to see what's involved in introducing a new module is to just
 * `git show` the commit adding this stub module to rototiller.
 *
 * XXX: Note that since this module has the TIL_MODULE_EXPERIMENTAL flag set, it
 * won't appear in the modules list or participate in randomizers.  You can stil
 * access it explicitly by name via the ":" prefix override, e.g.:
 * rototiller --module=:stub
 *
 * Or just remove the TIL_MODULE_EXPERIMENTAL flag during development so it's
 * treated normally.
 */

static void stub_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	til_fb_fragment_t	*fragment = *fragment_ptr;

	for (unsigned y = fragment->y; y < fragment->y + fragment->height; y++) {
		for (unsigned x = fragment->x; x < fragment->x + fragment->width; x++) {
			til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, 0xffffffff);
		}
	}
}


til_module_t	stub_module = {
	.render_fragment = stub_render_fragment,
	.name = "stub",
	.description = "Minimal stub sample module",
	.author = "Your Name <your@email.address>",
	.flags = TIL_MODULE_EXPERIMENTAL,	/* XXX: remove this line to make module generally available */
};
