#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "til_str.h"
#include "til_stream.h"

#include "rkt.h"
#include "rkt_scener.h"

/* Copyright (C) 2023 - Vito Caputo <vcaputo@pengaru.com> */

/* This implements a rudimentary BBS-style interface for manipulating the scenes in ctxt->scenes.
 *
 * Only a single connection is supported at this time.  It's really intended just to get _something_
 * cross-platform usable for editing the available scenes "live" at runtime in a minimum of time/effort.
 *
 * A more "modern" approach to this would be a HTTP REST API, yikes.
 */

#define RKT_SCENER_DEFAULT_MODULE	"compose"

typedef enum rkt_scener_fsm_t {
	RKT_SCENER_FSM_LISTENING,		/* port is listening, waiting for connection */
	RKT_SCENER_FSM_SENDING,			/* sending output */
	RKT_SCENER_FSM_RECVING,			/* reading input */
	RKT_SCENER_FSM_SEND_SETTINGS,		/* send rkt's settings hierarchy, including current scenes state, as args */
	RKT_SCENER_FSM_SEND_SCENES,		/* send main scenes list -> prompt */
	RKT_SCENER_FSM_RECV_SCENES,		/* waiting/reading at main scenes prompt */
	RKT_SCENER_FSM_SEND_NEWSCENE,		/* send create new scene dialog -> prompt */
	RKT_SCENER_FSM_RECV_NEWSCENE,		/* waiting/reading at the new scene prompt, creating/setting up new scene on input */
	RKT_SCENER_FSM_SEND_NEWSCENE_SETUP,	/* send whatever's necessary for next step of new_scene.settings setup */
	RKT_SCENER_FSM_SEND_NEWSCENE_SETUP_PROMPT,
	RKT_SCENER_FSM_RECV_NEWSCENE_SETUP,	/* waiting/reading at new scene setup setting prompt, finalizing and adding when complete */
	RKT_SCENER_FSM_SEND_EDITSCENE,		/* send edit scene dialog for scene @ scener->scene -> prompt */
	RKT_SCENER_FSM_RECV_EDITSCENE,		/* waiting/reading at the edit scene prompt, editing scene on input */
} rkt_scener_fsm_t;

typedef struct rkt_scener_t {
	rkt_scener_fsm_t	state, next_state;
	unsigned		scene;
	unsigned		pin_scene:1;
	int			listener;
	int			client;
	til_str_t		*input;
	til_str_t		*output;
	size_t			output_pos;

	struct {
		/* used while constructing a new scene, otherwise NULL */
		til_settings_t			*settings;
		til_setting_t			*cur_setting;
		const til_setting_desc_t	*cur_desc;
		til_setting_t			*cur_invalid;
	} new_scene;
} rkt_scener_t;


int rkt_scener_startup(rkt_context_t *ctxt)
{
	rkt_scener_t		*scener;
	rkt_setup_t		*setup;
	int			on = 1;
	struct sockaddr_in	addr;

	assert(ctxt);

	setup = ((rkt_setup_t *)ctxt->til_module_context.setup);

	scener = calloc(1, sizeof(rkt_scener_t));
	if (!scener)
		return -ENOMEM;

	scener->listener = socket(AF_INET, SOCK_STREAM, 0);
	if (scener->listener == -1)
		goto _err_free;

	if (setsockopt(scener->listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on)) == -1)
		goto _err_close;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(setup->scener_port);
	inet_aton(setup->scener_address, (struct in_addr *)&addr.sin_addr.s_addr);

	if (bind(scener->listener, (struct sockaddr *)&addr, sizeof(addr)) == -1)
		goto _err_close;

	if (listen(scener->listener, 1) == -1)
		goto _err_close;

	if (fcntl(scener->listener, F_SETFL, (int)O_NONBLOCK) == -1)
		goto _err_close;

	scener->client = -1;
	ctxt->scener = scener;

	return 0;

_err_close:
	close(scener->listener);
_err_free:
	free(scener);

	return -errno;
}


/* helper for sending output, entering next_state once sent */
static int rkt_scener_send(rkt_scener_t *scener, til_str_t *output, rkt_scener_fsm_t next_state)
{
	assert(scener);
	assert(!scener->output); /* catch entering send mid-send (or leaking output) */
	assert(output);
	assert(next_state != RKT_SCENER_FSM_SENDING);

	/* we generally send after processing input, so cleaning up for the input handlers here
	 * is ergonomic, enabling such callers to simply 'return rkt_scener_send(...);'
	 */
	scener->input = til_str_free(scener->input);

	scener->output_pos = 0;
	scener->output = output;
	scener->next_state = next_state;
	scener->state = RKT_SCENER_FSM_SENDING;

	return 0;
}


/* helper for receiving input, entering next_state once received (a line of text) */
static int rkt_scener_recv(rkt_scener_t *scener, rkt_scener_fsm_t next_state)
{
	assert(scener);
	assert(!scener->input);
	assert(next_state != RKT_SCENER_FSM_RECVING);

	scener->next_state = next_state;
	scener->state = RKT_SCENER_FSM_RECVING;

	return 0;
}


/* helper for reentering the listening state and returning -errno, for hard errors */
static int rkt_scener_err_close(rkt_scener_t *scener, int err)
{
	assert(scener);

	if (err > 0)
		err = -err;

	scener->state = RKT_SCENER_FSM_LISTENING;

	return err;
}


/* helper for sending a minimal strerror(errno) style message to the user before entering next_state */
static int rkt_scener_send_error(rkt_scener_t *scener, int error, rkt_scener_fsm_t next_state)
{
	til_str_t	*output;

	assert(scener);

	/* TODO: this should really use a static allocated output buffer to try work under ENOMEM */
	output = til_str_newf("Error: %s\n", strerror(error));
	if (!output)
		return -ENOMEM;

	return rkt_scener_send(scener, output, next_state);
}


/* helper for sending "invalid input" message w/scener->input incorporated */
static int rkt_scener_send_invalid_input(rkt_scener_t *scener, rkt_scener_fsm_t next_state)
{
	til_str_t	*output;

	assert(scener);
	assert(scener->input);

	output = til_str_new("\nInvalid input: ");
	if (!output)
		return rkt_scener_err_close(scener, ENOMEM);

	if (til_str_appendf(output, "\"%s\"\n\n", til_str_buf(scener->input, NULL)) < 0)
		return rkt_scener_err_close(scener, ENOMEM);

	scener->input = til_str_free(scener->input);

	return rkt_scener_send(scener, output, next_state);
}


/* helper for sending simple messages */ /* TODO: make variadic */
static int rkt_scener_send_message(rkt_scener_t *scener, const char *msg, rkt_scener_fsm_t next_state)
{
	til_str_t	*output;

	output = til_str_new(msg);
	if (!output)
		return rkt_scener_err_close(scener, ENOMEM);

	return rkt_scener_send(scener, output, next_state);
}


/* send welcome message */
static int rkt_scener_send_welcome(rkt_scener_t *scener, rkt_scener_fsm_t next_state)
{
	return rkt_scener_send_message(scener,
				       "\n\nWelcome to scener.\n"
				       "\n\n    Long live the scene!\n\n",
				       next_state);
}


/* send goodbye message */
static int rkt_scener_send_goodbye(rkt_scener_t *scener, rkt_scener_fsm_t next_state)
{
	return rkt_scener_send_message(scener,
				       "\n\n    The scene is dead.\n\n",
				       next_state);
}


/* handle input from the main scenes prompt */
static int rkt_scener_handle_input_scenes(rkt_context_t *ctxt)
{
	rkt_scener_t	*scener;
	size_t		len, i;
	const char	*buf;

	assert(ctxt);
	scener = ctxt->scener;
	assert(scener);
	assert(scener->input);

	buf = til_str_buf(scener->input, &len);

	/* skip any leading whitespace XXX maybe move to a helper? */
	for (i = 0; i < len; i++) {
		if (buf[i] != '\t' && buf[i] != ' ')
			break;
	}

	switch (buf[i]) {
	case '0' ... '9': { /* edit scene, parse uint */
		unsigned	s = 0;

		for (;i < len; i++) {
			if (buf[i] < '0' || buf[i] > '9')
				break;

			s *= 10;
			s += buf[i] - '0';
		}

		/* XXX: maybe skip trailing whitespace too? */
		if (i < len)
			return rkt_scener_send_invalid_input(scener, RKT_SCENER_FSM_SEND_SCENES);

		if (s >= ctxt->n_scenes)
			return rkt_scener_send_invalid_input(scener, RKT_SCENER_FSM_SEND_SCENES);

		scener->scene = s;
		scener->state = RKT_SCENER_FSM_SEND_EDITSCENE;
		break;
	}

	case 'N': /* Add new scene */
	case 'n':
		scener->state = RKT_SCENER_FSM_SEND_NEWSCENE;
		break;

	case 'S': /* Serialize current settings for the entire rkt scenes state */
	case 's':
		scener->state = RKT_SCENER_FSM_SEND_SETTINGS;
		break;

	case 'Q': /* End scener session; disconnects, but leaves rkt/rototiller intact */
	case 'q':
		/* TODO: it might make sense to dump the serialized settings on quit just as a safety-net,
		 * or ask if an export is desired (assuming track data exports from scener becomes a thing)
		 */
		return rkt_scener_send_goodbye(scener, RKT_SCENER_FSM_LISTENING);

	case '!': /* toggle pin_scene */
		scener->pin_scene = !scener->pin_scene;
		scener->state = RKT_SCENER_FSM_SEND_SCENES;
		break;

	case '\0': /* if you don't say anything to even quote as "invalid input", just go back to the scenes dialog */
		scener->state = RKT_SCENER_FSM_SEND_SCENES;
		break;

	default:
		return rkt_scener_send_invalid_input(scener, RKT_SCENER_FSM_SEND_SCENES);
	}

	/* XXX: note it's important the above non-invalid-input cases break and not return,
	 * so we discard the input.
	 */
	scener->input = til_str_free(scener->input);

	return 0;
}


static int rkt_scener_handle_input_newscene(rkt_context_t *ctxt)
{
	rkt_scener_t	*scener;
	til_settings_t	*scenes_settings;
	til_settings_t	*new_settings;
	const char	*buf;
	size_t		len;

	assert(ctxt);
	scener = ctxt->scener;
	assert(scener);
	assert(scener->input);
	assert(!scener->new_scene.settings);

	scenes_settings = ((rkt_setup_t *)ctxt->til_module_context.setup)->scenes_settings;

	/* XXX: this !len exception is to treat "" exceptionally and not add a "" bare-value @ idx 0,
	 * which will be looked up as the module name.  It would be nice to not need this, maybe
	 * if the invalid input handler could just be graceful enough this could be removed and
	 * allow the "" invalid module name to then fall-through into a module re-select with
	 * the empty name in the setting... TODO
	 */
	buf = til_str_buf(scener->input, &len);
	if (!len)
		buf = NULL;

	new_settings = til_settings_new(NULL, scenes_settings, "WIP-new-scene", buf);
	if (!new_settings)
		return rkt_scener_err_close(scener, ENOMEM);

	scener->input = til_str_free(scener->input);
	scener->new_scene.settings = new_settings;
	scener->new_scene.cur_setting = NULL;
	scener->new_scene.cur_desc = NULL;
	scener->new_scene.cur_invalid = NULL;
	scener->state = RKT_SCENER_FSM_SEND_NEWSCENE_SETUP;

	return 0;
}


static int rkt_scener_handle_input_newscene_setup(rkt_context_t *ctxt)
{
	rkt_scener_t			*scener;
	til_setting_t			*setting;
	const til_setting_desc_t	*desc;
	const til_setting_t		*invalid;
	size_t				len;
	const char			*buf;
	const char			*value;

	assert(ctxt);
	scener = ctxt->scener;
	assert(scener);
	assert(scener->input);
	assert(scener->new_scene.settings);

	setting = scener->new_scene.cur_setting;
	desc = scener->new_scene.cur_desc;
	invalid = scener->new_scene.cur_invalid;

	if (invalid && setting == invalid && !desc)
		desc = invalid->desc;

	assert(desc);
	
	/* we're adding a setting, the input may be a raw string, or
	 * it might be a subscript of an array of values, it all depends on
	 * scener->new_scene.cur_desc
	 *
	 * there should probably be some optional syntax supported for forcing
	 * raw values in the event you want to go outside what's in the values,
	 * bypassing any spec check in the process, relying entirely on the
	 * setup function's robustness to detect invalid input, which is
	 * going to break sometimes no doubt.  But the values[] arrays are
	 * quite limited in some cases where we've basically just given a
	 * handful of presets for something where really any old float value
	 * would work.  Maybe what's really necessary there is to just have
	 * something in the spec that says "arbitrary_ok" despite having
	 * values[].  There's certainly some strict scenarios where the list has
	 * the only valid options (eg drizzle::style={mask,map}), so blocking
	 * raw input for those seems sensible. TODO
	 */
	buf = til_str_buf(scener->input, &len);
	if (!len) {
		value = desc->spec.preferred;
	} else {
		til_str_t	*output;

		if (desc->spec.values) {
			unsigned	i, j, found;

			/* multiple choice, map numeric input to values entry */
			if (sscanf(buf, "%u", &j) < 1) {
				output = til_str_newf("Invalid input: \"%s\"\n", buf);
				if (!output)
					return -ENOMEM;

				return rkt_scener_send(scener, output, RKT_SCENER_FSM_SEND_NEWSCENE_SETUP);
			}

			for (found = i = 0; desc->spec.values[i]; i++) {
				if (i == j) {
					value = desc->spec.values[i];
					found = 1;
					break;
				}
			}

			if (!found) {
				output = til_str_newf("Invalid option: %u outside of range [0-%u]\n", j, i - 1);
				if (!output)
					return -ENOMEM;

				return rkt_scener_send(scener, output, RKT_SCENER_FSM_SEND_NEWSCENE_SETUP);
			}

		} else {
			/* use typed input as setting, TODO: apply regex */
			value = buf;
		}
	}

	/* we might be fixing an invalid setting instead of adding, determine that here */
	if (invalid && setting == invalid) {
		/* TODO: add til_setting_set_value() ? */
		free((void *)setting->value);
		setting->value = strdup(value);
		scener->new_scene.cur_invalid = NULL; /* try again */
	} else if (!til_settings_add_value(desc->container, desc->spec.key, value))
		return -ENOMEM;

	scener->input = til_str_free(scener->input);
	scener->state = RKT_SCENER_FSM_SEND_NEWSCENE_SETUP;

	return 0;
}



/* randomize the settings for ctxt->scenes[scene], keeping its current module */
static int rkt_scener_randomize_scene_settings(rkt_context_t *ctxt, unsigned scene_idx)
{
	const til_module_t	*module;
	rkt_scener_t		*scener;
	rkt_scene_t		*scene;
	til_settings_t		*scenes_settings;
	til_setting_t		*scene_setting;
	til_settings_t		*new_settings;
	til_setup_t		*setup;
	char			*label;
	int			r;

	assert(ctxt);
	scener = ctxt->scener;
	assert(scener);

	assert(scene_idx < ctxt->n_scenes);

	scenes_settings = ((rkt_setup_t *)ctxt->til_module_context.setup)->scenes_settings;
	scene = &ctxt->scenes[scene_idx];
	module = scene->module_ctxt->module,
	assert(module);

	til_settings_get_value_by_idx(scenes_settings, scene_idx, &scene_setting);
	/* FIXME: this is all rather janky TODO clean up the api for these uses, helpers, whatever */
	r = til_settings_label_setting(scenes_settings, scene_setting, &label);
	if (r < 0)
		return r;

	new_settings = til_settings_new(NULL, scenes_settings, label, module->name);
	free(label);
	if (!new_settings)
		return -ENOMEM;

	/* FIXME: seed reproducibility needs to be sorted out, maybe move seed into settings */
	til_module_setup_randomize(module,
				   new_settings,
				   rand_r(&ctxt->til_module_context.seed),
				   &setup,
				   NULL); /* TODO: errors! */

	scene_setting->value_as_nested_settings = new_settings;
#if 0
	free((void *)scene_setting->value);
	scene_setting->value = as_arg; /* XXX should I really overrite the original bare-value? maybe
					* preserve the ability to go back to what it was by leaving
					* this alone?  printing the settings_as_arg ignores this anyways,
					* when there's a non-NULL value_as_nested_settings...
					* Maybe once scene editing gets implemented, this may become
					* more of an issue with an obvious Right Way.
					*/
#endif
	scene->module_ctxt = til_module_context_free(scene->module_ctxt);
	(void) til_module_create_context(module,
					 ctxt->til_module_context.stream,
					 rand_r(&ctxt->til_module_context.seed),
					 ctxt->til_module_context.last_ticks,
					 ctxt->til_module_context.n_cpus,
					 setup,
					 &scene->module_ctxt); /* TODO: errors */
	til_setup_free(setup);

	/* This will probably get more complicated once rkt starts getting more active about
	 * creating and destroying scene contexts only while they're in use.  But today all
	 * contexts persist for the duration, so they always have a reference as long as they're
	 * extant in ctxt->scenes[]...
	 */
	til_stream_gc_module_contexts(ctxt->til_module_context.stream);

	return 0;
}


static int rkt_scener_handle_input_editscene(rkt_context_t *ctxt)
{
	rkt_scener_t	*scener;
	size_t		len, i;
	const char	*buf;

	assert(ctxt);
	scener = ctxt->scener;
	assert(scener);
	assert(scener->input);

	buf = til_str_buf(scener->input, &len);

	/* skip any leading whitespace XXX maybe move to a helper? */
	for (i = 0; i < len; i++) {
		if (buf[i] != '\t' && buf[i] != ' ')
			break;
	}

	switch (buf[i]) {
	case '0' ... '9': { /* edit scene */
		unsigned s = 0;

		/* XXX: move to function? */
		for (;i < len; i++) {
			if (buf[i] < '0' || buf[i] > '9')
				break;

			s *= 10;
			s += buf[i] - '0';
		}

		/* XXX: maybe skip trailing whitespace too? */
		if (i < len)
			return rkt_scener_send_invalid_input(scener, RKT_SCENER_FSM_SEND_EDITSCENE);

		if (s >= ctxt->n_scenes)
			return rkt_scener_send_invalid_input(scener, RKT_SCENER_FSM_SEND_EDITSCENE);

		scener->scene = s;
		scener->state = RKT_SCENER_FSM_SEND_EDITSCENE;
		break;
	}

	case 'E': /* edit scene settings */
	case 'e':
		/* TODO run through a setup cycle seeded with current settings */
		/* so editing is very similar to creating a new scene...
		 * but the current scene's as_args are used to seed the create,
		 * instead of leaving it blank as in NEWSCENE.
		 *
		 * The main reason I haven't already done this is I want to have the
		 * input as_args settings show up as raw input at the initial New scene
		 * prompt, in a way that the user can then arrow-key/backspace over and
		 * change things in the same manner as if they has copy and pasted it into
		 * a text editor.  But I'm not sure I can achieve that without going full ANSI
		 * BBS control codes up in here... still investigating. TODO
		 */
		return rkt_scener_send_message(scener,
						"\n\nScene editing not _yet_ implemented, create a new one?\n"
						"\n"
						"Keep in mind, it's the Rocket 'scene' track that governs scene ordering/visibility,\n"
						"think of these as more like Samples/Instruments in a tracker.\n"
						"So you can relatively harmlessly have unused scenes in here.\n"
						"\n"
						"A temporary way to edit is copy and paste the settings shown above into a text editor,\n"
						"make your tweaks (you can also just delete parts you want to revisit interactive setup for),\n"
						"then paste it into the initial New scene raw input prompt (*discard* the quotes!).\n"
						"\n"
						"One advantage to this approach is you'll always have the old scene's settings for a retry.\n"
						"\n",
						RKT_SCENER_FSM_SEND_EDITSCENE);

	case 'R': /* randomize only the settings (keep existing module) */
	case 'r': {
		int	r;

		r = rkt_scener_randomize_scene_settings(ctxt, scener->scene);
		if (r < 0)
			return rkt_scener_send_error(scener, r, RKT_SCENER_FSM_SEND_EDITSCENE);

		scener->state = RKT_SCENER_FSM_SEND_EDITSCENE;
		break;
	}

	case 'N':
	case 'n':
		scener->state = RKT_SCENER_FSM_SEND_NEWSCENE;
		break;

	case '!': /* toggle pin_scene */
		scener->pin_scene = !scener->pin_scene;
		scener->state = RKT_SCENER_FSM_SEND_EDITSCENE;
		break;

	case '\0': /* if you don't say anything to even quote as "invalid input", just go back to the scenes dialog */
		scener->state = RKT_SCENER_FSM_SEND_SCENES;
		break;

	default:
		return rkt_scener_send_invalid_input(scener, RKT_SCENER_FSM_SEND_EDITSCENE);
	}

	/* XXX: note it's important the above non-invalid-input cases break and not return,
	 * so we discard the input.
	 */
	scener->input = til_str_free(scener->input);

	return 0;
}


/* The architecture here is kept simple; just one client is supported, the sockets are
 * all put in non-blocking mode, so we just poll for accepts on the listener when not
 * connected (listening), or poll for send buffer availability when sending, or poll for
 * recv bytes when receiving.  We're only doing one of those at a time WRT IO, per-update.
 *
 * When something is to be sent, it gets buffered entirely and placed in scener->output
 * before entering a generic sending state which persists until the output is
 * all sent.  Once that happens, the queued "next state" gets entered.  Since we're not going
 * to be sending big binary streams of filesystem data, this is fine; it's basically a BBS UI.
 *
 * When something needs to be received, a "next state" is queued and the generic receiving
 * state entered.  The receiving state persists receiving bytes until a newline byte is
 * received, making this fundamentally line-oriented.  The received line is buffered and left
 * in scener->input, for the queued "next state" to handle, which is transitioned to @ newline.
 * Yes, this means scener will consume all memory if you just keep sending non-newline data to
 * it, this isn't a public tcp service, it's a tool just for you to use - the safety guards are
 * minimal if present at all.
 *
 * That's basically all there is... error situations are handled by reentering the listening
 * state which will first close the client fd if valid, before resuming polling the listener fd
 * w/accept.
 *
 * This update function is expected to be called regularly by rkt, probably every frame.  It's
 * kept in this "dumb polling" single-threaded synchronous fashion deliberately so scener can be
 * relatively unconcerned about mucking with the scenes state and any other rkt state without
 * introducing locks or other synchronization complexities.  It's not the greatest perf-wise, but
 * keep in mind this is strictly for the creative process, you wouldn't have this enabled when
 * running a finished prod in anything resembling release mode.
 *
 * The return value is -errno on errors, 0 on "everything's normal".  Some attempt
 * has been made to be resilient to errors in that many -errno paths will just return
 * the state to "listening" and may just recover.  I'm not sure right now what rkt will
 * do with the errors coming out of this function, for now they'll surely just be silently
 * ignored... but at some point it might be useful to print something to stderr about their
 * nature at least, in rkt that is, not here.
 */

int rkt_scener_update(rkt_context_t *ctxt)
{
	rkt_scener_t	*scener;
	unsigned	ctxt_scene;

	assert(ctxt);

	if (!ctxt->scener)
		return 0;

	scener = ctxt->scener;

	ctxt_scene = ctxt->scene;
	if (scener->pin_scene)
		ctxt->scene = scener->scene;

	switch (scener->state) {
	case RKT_SCENER_FSM_LISTENING: {
		int	fd, on = 1;

		/* any state can just resume listening anytime, which will close and free things */
		if (scener->client != -1) {
			close(scener->client);
			scener->client = -1;
		}

		scener->output = til_str_free(scener->output);
		scener->input  = til_str_free(scener->input);
		scener->new_scene.settings = til_settings_free(scener->new_scene.settings);

		fd = accept(scener->listener, NULL, NULL);
		if (fd == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return 0;

			return -errno;
		}

		if (fcntl(fd, F_SETFL, (int)O_NONBLOCK) == -1) {
			close(fd);
			return -errno;
		}

		scener->client = fd;
		(void) setsockopt(scener->client, IPPROTO_TCP, TCP_NODELAY, (void *)&on, sizeof(on));

		return rkt_scener_send_welcome(scener, RKT_SCENER_FSM_SEND_SCENES);
	}

	case RKT_SCENER_FSM_SENDING: {
		const char	*buf;
		size_t		len;
		ssize_t		ret;

		buf = til_str_buf(scener->output, &len);

		assert(scener->output_pos < len);

		ret = send(scener->client, &buf[scener->output_pos], len - scener->output_pos, MSG_NOSIGNAL);
		if (ret == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return 0;

			return rkt_scener_err_close(scener, errno);
		}

		scener->output_pos += ret;
		if (scener->output_pos < len)
			return 0;

		scener->output = til_str_free(scener->output);
		scener->state = scener->next_state;

		return 0;
	}

	case RKT_SCENER_FSM_RECVING: {
		char	b[2] = {};
		
		for (;;) {
			/* keep accumulating input until a newline, then transition to next_state */
			switch (recv(scener->client, &b, 1, 0)) {
			case -1:
				if (errno == EWOULDBLOCK || errno == EAGAIN)
					return 0;

				return rkt_scener_err_close(scener, errno);

			case 0: /* client shutdown before finding \n, return to listening (discard partial input) */
				return rkt_scener_err_close(scener, 0);

			case 1: /* add byte to input buffer, transition on newline (TODO: one-byte recv()s are _slow_) */
				if (!scener->input) {
					scener->input = til_str_new(b);
					if (!scener->input)
						return rkt_scener_err_close(scener, ENOMEM);
				} else {
					if (til_str_appendf(scener->input, "%c", b[0]) < 0)
						return rkt_scener_err_close(scener, ENOMEM);
				}

				if (b[0] == '\n') { /* TODO: maybe support escaping the newline for multiline input? */
					/* before transitioning. let's strip off the line delimiter.
					 * it's effectively encapsulation protocol and not part of the input buffer
					 */
					scener->input = til_str_chomp(scener->input);
					scener->state = scener->next_state;

					return 0;
				}

				continue;

			default:
				assert(0);
			}
		}
	}

	case RKT_SCENER_FSM_SEND_SETTINGS: {
		til_str_t	*output;
		char		*as_arg;

		as_arg = til_settings_as_arg(((rkt_setup_t *)ctxt->til_module_context.setup)->settings);
		if (!as_arg)
			return rkt_scener_err_close(scener, ENOMEM);

		output = til_str_newf("\n--module='%s'\n", as_arg);
		free(as_arg);
		if (!output)
			return rkt_scener_err_close(scener, ENOMEM);

		return rkt_scener_send(scener, output, RKT_SCENER_FSM_SEND_SCENES);
	}

	case RKT_SCENER_FSM_SEND_SCENES: {
		til_settings_t	*scenes_settings;
		til_str_t	*output;
		unsigned	i;

		output = til_str_new("\n\n");
		if (!output)
			return rkt_scener_err_close(scener, ENOMEM);

		scenes_settings = ((rkt_setup_t *)ctxt->til_module_context.setup)->scenes_settings;
		if (til_settings_strprint_path(scenes_settings, output) < 0)
			return rkt_scener_err_close(scener, ENOMEM);

		if (til_str_appendf(output, "/scenes:\n\n") < 0)
			return rkt_scener_err_close(scener, ENOMEM);

		if (til_str_appendf(output,	" +- Rocket\n"
						" |+- Scener\n"
						" ||+- Pinned by scener\n"
						" |||\n") < 0)
			return rkt_scener_err_close(scener, ENOMEM);

		for (i = 0; i < ctxt->n_scenes; i++) {
			if (til_str_appendf(output,
					    " %c%c%c%s\n",
					    ctxt_scene == i ? '*' : ' ',
					    scener->scene == i ? '*' : ' ',
					    (scener->scene == i && scener->pin_scene) ? '!' : ' ',
					    ctxt->scenes[i].module_ctxt->setup->path) < 0)
				return rkt_scener_err_close(scener, ENOMEM);
		}

		if (til_str_appendf(output, "\n") < 0)
			return rkt_scener_err_close(scener, ENOMEM);

		if (i) {
			if (til_str_appendf(output, " [0-%u]", i - 1) < 0)
				return rkt_scener_err_close(scener, ENOMEM);
		}

		if (til_str_appendf(output,
				    " (N)ewScene (S)howSettings %s (Q)uit: ",
				    scener->pin_scene ? "Unpin(!)" : "Pin(!)") < 0)
			return rkt_scener_err_close(scener, ENOMEM);

		return rkt_scener_send(scener, output, RKT_SCENER_FSM_RECV_SCENES);
	}

	case RKT_SCENER_FSM_RECV_SCENES:
		if (!scener->input)
			return rkt_scener_recv(scener, scener->state);

		return rkt_scener_handle_input_scenes(ctxt);

	case RKT_SCENER_FSM_SEND_NEWSCENE: {
		til_str_t	*output;

		output = til_str_new("Input new scene \"module[,settings...]\" <just enter goes interactive>:\n");
		if (!output)
			return rkt_scener_err_close(scener, ENOMEM);

		return rkt_scener_send(scener, output, RKT_SCENER_FSM_RECV_NEWSCENE);
	}

	case RKT_SCENER_FSM_RECV_NEWSCENE:
		if (!scener->input)
			return rkt_scener_recv(scener, scener->state);

		return rkt_scener_handle_input_newscene(ctxt);

	case RKT_SCENER_FSM_SEND_NEWSCENE_SETUP: {
		int	r;

		r = til_module_setup(scener->new_scene.settings,
				     &scener->new_scene.cur_setting,
				     &scener->new_scene.cur_desc,
				     NULL);	/* res_setup deliberately left NULL for two reasons:
						 * 1. prevents finalizing (path is "...WIP-new-scene...")
						 * 2. disambiguates -EINVAL errors from those potentially
						 *    returned while finalizing/baking into a til_setup_t.
						 *
						 *    This way if such an error is returned, we can expect
						 *    res_setting and res_desc to refer to /what's/ invalid,
						 *    and correct it before retrying.
						 *
						 *    It'd be rather obnoxious with how the setup funcs are
						 *    implemented currently to try force returning the relevant
						 *    setting+desc for errors found during baking...  I still
						 *    need to firm up and document setup_func error semantics, and
						 *    ideally it would be nice if the finalizing could say what
						 *    setting/desc is relevant when say.. doing an atoi().
						 *    That will require a bit of churn in the settings get_value
						 *    API and changing all the setup_funcs to be setting-centric
						 *    instead of the current char* value-centric mode TODO
						 */
		if (r < 0) {
			/* something went wrong... */
			if (r != -EINVAL) /* just give up on everything non-EINVAL */
				return rkt_scener_err_close(scener, r);

			/* Invalid setting! go back to prompting for input */
			scener->new_scene.cur_invalid = scener->new_scene.cur_setting;

			return rkt_scener_send_error(scener, EINVAL, RKT_SCENER_FSM_SEND_NEWSCENE_SETUP_PROMPT);

		} else if (r > 0) {
			til_setting_t			*setting = scener->new_scene.cur_setting;
			const til_setting_desc_t	*desc = scener->new_scene.cur_desc;
			til_setting_t			*invalid = scener->new_scene.cur_invalid;

			assert(desc);

			if (setting && setting != invalid && !setting->desc) {
				/* Apply override before, or after the spec_check()? unclear.
				 * TODO This probably also needs to move into a til_settings helper
				 */
				if (desc->spec.override) {
					const char	*o;

					o = desc->spec.override(setting->value);
					if (!o)
						return rkt_scener_err_close(scener, ENOMEM);

					if (o != setting->value) {
						free((void *)setting->value);
						setting->value = o;
					}
				}

				if (til_setting_spec_check(&desc->spec, setting->value) < 0) {
					/* setting invalid! go back to prompting for input */
					scener->new_scene.cur_invalid = setting;

					return rkt_scener_send_error(scener, EINVAL, RKT_SCENER_FSM_SEND_NEWSCENE_SETUP_PROMPT);
				}

				if (desc->spec.as_nested_settings && !setting->value_as_nested_settings) {
					char	*label = NULL;

					if (!desc->spec.key) {
						/* generate a positional label for bare-value specs */
						r = til_settings_label_setting(desc->container, setting, &label);
						if (r < 0)
							return rkt_scener_err_close(scener, r);
					}

					setting->value_as_nested_settings = til_settings_new(NULL, desc->container, desc->spec.key ? : label, setting->value);
					free(label);

					if (!setting->value_as_nested_settings) {
						/* FIXME: til_settings_new() seems like it should return an errno, since it can encounter parse errors too? */
						return rkt_scener_err_close(scener, ENOMEM);
					};
				}

				setting->desc = desc;

				/* setting OK and described, stay in this state and do more setup */
				return 0;
			}

			/* More settings needed! go back to prompting for input */
			scener->state = RKT_SCENER_FSM_SEND_NEWSCENE_SETUP_PROMPT;

			return 0;
		}

		/* new_scene.settings appears to be complete, but it still needs to be finalized.
		 * Before that can happen scenes_settings need to be enlarged, then new_settings
		 * can get added under the new entry.  At that point a proper path would result,
		 * so finalizing may proceed.
		 */

		/* these *may* move into helpers, so scoping them as ad-hoc unnamed functions for now */
		{ /* expand scenes settings */
			til_settings_t	*scenes_settings;
			til_setting_t	*scene_setting;
			char		*label, *as_arg;
			int		r;

			/* now that we know settings is completely valid, expand scenes and get everything wired up */
			as_arg = til_settings_as_arg(scener->new_scene.settings);
			if (!as_arg)
				return rkt_scener_err_close(scener, ENOMEM);

			scenes_settings = ((rkt_setup_t *)ctxt->til_module_context.setup)->scenes_settings;
			scene_setting = til_settings_add_value(scenes_settings, NULL, as_arg);
			if (!scene_setting)
				return rkt_scener_err_close(scener, ENOMEM);

			r = til_setting_desc_new(scenes_settings,
						 &(til_setting_spec_t){
							.as_nested_settings = 1,
						 },
						 &scene_setting->desc);
			if (r < 0) /* FIXME TODO we should probably drop the half-added value??? */
				return rkt_scener_err_close(scener, r);

			r = til_settings_label_setting(scenes_settings, scene_setting, &label);
			if (r < 0) /* FIXME TODO we should probably drop the half-added value??? */
				return rkt_scener_err_close(scener, r);

			r = til_settings_set_label(scener->new_scene.settings, label);
			free(label);
			if (r < 0) /* FIXME TODO we should probably drop the half-added value??? */
				return rkt_scener_err_close(scener, r);

			scene_setting->value_as_nested_settings = scener->new_scene.settings;
		}

		{ /* expand context scenes, finalize setup, create context */
			const char		*module_name;
			const til_module_t	*module;
			til_module_context_t	*module_ctxt;
			til_setup_t		*setup;
			rkt_scene_t		*new_scenes;
			int			r;

			/* Still haven't baked the setup.
			 * At this point the new scene's settings are inserted,
			 * and it's time to bake the setup and create the context,
			 * adding the rkt_scene_t instance corresponding to the settings.
			 */
			module_name = til_settings_get_value_by_idx(scener->new_scene.settings, 0, NULL);
			if (!module_name) /* FIXME TODO we should probably un-add the scene from scenes_settings??? */
				return rkt_scener_err_close(scener, EINVAL); /* this really shouldn't happen */

			module = til_lookup_module(module_name);
			if (!module) /* FIXME TODO we should probably un-add the scene from scenes_settings??? */
				return rkt_scener_err_close(scener, EINVAL); /* this really shouldn't happen */

			r = til_module_setup_finalize(module, scener->new_scene.settings, &setup);
			if (r < 0) /* FIXME TODO we should probably un-add the scene from scenes_settings??? */
				return rkt_scener_err_close(scener, r);

			/* have baked setup @ setup, create context using it */
			r = til_module_create_context(module,
							 ctxt->til_module_context.stream,
							 rand_r(&ctxt->til_module_context.seed), /* FIXME TODO seeds need work (make reproducible) */
							 ctxt->til_module_context.last_ticks,
							 ctxt->til_module_context.n_cpus,
							 setup,
							 &module_ctxt);
			til_setup_free(setup);
			if (r < 0)
				return rkt_scener_err_close(scener, r);

			/* have context, almost there, enlarge scener->scenes, bump scener->n_scenes */
			new_scenes = realloc(ctxt->scenes, (ctxt->n_scenes + 1) * sizeof(*new_scenes));
			if (!new_scenes) {
				til_module_context_free(module_ctxt);

				return rkt_scener_err_close(scener, r);
			}

			new_scenes[ctxt->n_scenes].module_ctxt = module_ctxt;
			ctxt->scenes = new_scenes;
			ctxt->n_scenes++;

			/* added! */

			scener->scene = ctxt->n_scenes - 1;
		}

		scener->new_scene.settings = NULL;

		return rkt_scener_send_message(scener,
					       "\n\nNew scene added successfully...\n",
					       RKT_SCENER_FSM_SEND_SCENES);
	}

	case RKT_SCENER_FSM_SEND_NEWSCENE_SETUP_PROMPT: {
		til_setting_t			*setting = scener->new_scene.cur_setting;
		const til_setting_desc_t	*desc = scener->new_scene.cur_desc;
		til_setting_t			*invalid = scener->new_scene.cur_invalid;
		til_str_t			*output;

		if (invalid && setting == invalid && !desc)
			desc = invalid->desc;

		assert(desc);

		/* construct a prompt based on cur_desc
		 *
		 * this was derived from setup_interactively(), but til_str_t-centric and
		 * decomposed further for the scener fsm integration.
		 */
		output = til_str_newf("\n", desc->spec.key);
		if (!output)
			return rkt_scener_err_close(scener, ENOMEM);

		if (desc->spec.values) {
			unsigned	i, preferred = 0;
			int		width = 0;

			for (i = 0; desc->spec.values[i]; i++) {
				int	len;

				len = strlen(desc->spec.values[i]);
				if (len > width)
					width = len;
			}

			/* multiple choice */
			if (til_setting_desc_strprint_path(desc, output) < 0)
				return rkt_scener_err_close(scener, ENOMEM);

			if (til_str_appendf(output, ":\n %s:\n", desc->spec.name) < 0)
				return rkt_scener_err_close(scener, ENOMEM);

			for (i = 0; desc->spec.values[i]; i++) {
				if (til_str_appendf(output, " %2u: %*s%s%s\n", i, width, desc->spec.values[i],
						    desc->spec.annotations ? ": " : "",
						    desc->spec.annotations ? desc->spec.annotations[i] : "") < 0)
					return rkt_scener_err_close(scener, ENOMEM);

				if (!strcasecmp(desc->spec.preferred, desc->spec.values[i]))
					preferred = i;
			}

			if (til_str_appendf(output, " Enter a value 0-%u [%u (%s)]: ",
					    i - 1, preferred, desc->spec.preferred) < 0)
				return rkt_scener_err_close(scener, ENOMEM);

		} else {
			/* arbitrarily typed input */
			if (til_setting_desc_strprint_path(desc, output) < 0)
				return rkt_scener_err_close(scener, ENOMEM);

			if (til_str_appendf(output, ":\n %s [%s]: ", desc->spec.name, desc->spec.preferred) < 0)
				return rkt_scener_err_close(scener, ENOMEM);
		}

		return rkt_scener_send(scener, output, RKT_SCENER_FSM_RECV_NEWSCENE_SETUP);
	}

	case RKT_SCENER_FSM_RECV_NEWSCENE_SETUP:
			if (!scener->input)
				return rkt_scener_recv(scener, scener->state);

		return rkt_scener_handle_input_newscene_setup(ctxt);

	case RKT_SCENER_FSM_SEND_EDITSCENE: {
		til_settings_t	*scenes_settings = ((rkt_setup_t *)ctxt->til_module_context.setup)->scenes_settings;
		til_setting_t	*scene_setting;
		char		*as_arg;
		til_str_t	*output;

		if (!til_settings_get_value_by_idx(scenes_settings, scener->scene, &scene_setting))
			return rkt_scener_err_close(scener, ENOENT);

		as_arg = til_settings_as_arg(scene_setting->value_as_nested_settings);
		if (!as_arg)
			return rkt_scener_err_close(scener, ENOMEM);

		output = til_str_newf("\n"
				      "%s:\n\n"
				      " Visible: %s\n"
				      " Pinned: %s\n"
				      " Settings: \'%s\'\n"
				      "\n"
				      " (E)dit (R)andomizeSetup (N)ewScene %s: ",
				      ctxt->scenes[scener->scene].module_ctxt->setup->path,
				      scener->pin_scene ?  "YES" : (ctxt_scene == scener->scene ? "YES" : "NO, PIN TO FORCE"),
				      scener->pin_scene ?  "YES, (!) to UNPIN" : "NO, (!) TO PIN",
				      as_arg,
				      scener->pin_scene ? "Unpin(!)" : "Pin(!)");
		free(as_arg);
		if (!output)
			return rkt_scener_err_close(scener, ENOMEM);

		return rkt_scener_send(scener, output, RKT_SCENER_FSM_RECV_EDITSCENE);
	}

	case RKT_SCENER_FSM_RECV_EDITSCENE:
		if (!scener->input)
			return rkt_scener_recv(scener, scener->state);

		return rkt_scener_handle_input_editscene(ctxt);

	default:
		assert(0);
	}

	return 0;
}


int rkt_scener_shutdown(rkt_context_t *ctxt)
{
	assert(ctxt);

	if (!ctxt->scener)
		return 0;

	close(ctxt->scener->listener);

	if (ctxt->scener->client != -1)
		close(ctxt->scener->client);

	til_str_free(ctxt->scener->input);
	til_str_free(ctxt->scener->output);

	free(ctxt->scener);
	ctxt->scener = NULL;

	return 0;
}
