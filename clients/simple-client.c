/*
 * Copyright © 2011 Benjamin Franzke
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>

#include <wayland-client.h>
#include <wayland-egl.h>

#include <GLES2/gl2.h>
#include <EGL/egl.h>

//JK
//#include <sys/types.h>
//#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
struct display {
	struct wl_display *display;
	struct wl_visual *premultiplied_argb_visual;
	struct wl_compositor *compositor;
	struct wl_shell *shell;
	struct {
		EGLDisplay dpy;
		EGLContext ctx;
		EGLConfig conf;
	} egl;
	uint32_t mask;
};

struct window {
	struct display *display;
	struct {
		int width, height;
	} geometry;
	struct {
		GLuint fbo;
		GLuint color_rbo;

		GLuint program;
		GLuint rotation_uniform;

		GLuint pos;
		GLuint col;
	} gl;

	struct wl_egl_window *native;
	struct wl_surface *surface;
	EGLSurface egl_surface;
};

static const char *vert_shader_text =
	"uniform mat4 rotation;\n"
	"attribute vec4 pos;\n"
	"attribute vec4 color;\n"
	"varying vec4 v_color;\n"
	"void main() {\n"
	"  gl_Position = rotation * pos;\n"
	"  v_color = color;\n"
	"}\n";

static const char *frag_shader_text =
	"precision mediump float;\n"
	"varying vec4 v_color;\n"
	"void main() {\n"
	"  gl_FragColor = v_color;\n"
	"}\n";

static void
init_egl(struct display *display)
{
	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	static const EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 1,
		EGL_DEPTH_SIZE, 1,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	EGLint major, minor, n;
	EGLBoolean ret;

	setenv("EGL_PLATFORM", "wayland", 1);
	display->egl.dpy = eglGetDisplay(display->display);
	assert(display->egl.dpy);

	ret = eglInitialize(display->egl.dpy, &major, &minor);
	assert(ret == EGL_TRUE);
	ret = eglBindAPI(EGL_OPENGL_ES_API);
	assert(ret == EGL_TRUE);

	assert(eglChooseConfig(display->egl.dpy, config_attribs,
			       &display->egl.conf, 1, &n) && n == 1);

	display->egl.ctx = eglCreateContext(display->egl.dpy,
					    display->egl.conf,
					    EGL_NO_CONTEXT, context_attribs);
	assert(display->egl.ctx);

}

static GLuint
create_shader(struct window *window, const char *source, GLenum shader_type)
{
	GLuint shader;
	GLint status;

	shader = glCreateShader(shader_type);
	assert(shader != 0);

	glShaderSource(shader, 1, (const char **) &source, NULL);
	glCompileShader(shader);

	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetShaderInfoLog(shader, 1000, &len, log);
		fprintf(stderr, "Error: compiling %s: %*s\n",
			shader_type == GL_VERTEX_SHADER ? "vertex" : "fragment",
			len, log);
		exit(1);
	}

	return shader;
}

static void
init_gl(struct window *window)
{
	GLuint frag, vert;
	GLint status;

	glViewport(0, 0, window->geometry.width, window->geometry.height);

	frag = create_shader(window, frag_shader_text, GL_FRAGMENT_SHADER);
	vert = create_shader(window, vert_shader_text, GL_VERTEX_SHADER);

	window->gl.program = glCreateProgram();
	glAttachShader(window->gl.program, frag);
	glAttachShader(window->gl.program, vert);
	glLinkProgram(window->gl.program);

	glGetProgramiv(window->gl.program, GL_LINK_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetProgramInfoLog(window->gl.program, 1000, &len, log);
		fprintf(stderr, "Error: linking:\n%*s\n", len, log);
		exit(1);
	}

	glUseProgram(window->gl.program);
	
	window->gl.pos = 0;
	window->gl.pos = 1;

	glBindAttribLocation(window->gl.program, window->gl.pos, "pos");
	glBindAttribLocation(window->gl.program, window->gl.col, "color");
	glLinkProgram(window->gl.program);

	window->gl.rotation_uniform =
		glGetUniformLocation(window->gl.program, "rotation");
}

static void
sync_callback(void *data)
{
	int *done = data;

	*done = 1;
}

static void
create_surface(struct window *window)
{
	struct display *display = window->display;
	struct wl_visual *visual;
	EGLBoolean ret;
	int done = 0;
	
	if (!display->premultiplied_argb_visual) {
		wl_display_sync_callback(display->display, sync_callback, &done);
		while (!done)
			wl_display_iterate(display->display, display->mask);
		if (!display->premultiplied_argb_visual) {
			fprintf(stderr, "premultiplied argb visual missing\n");
			exit(1);
		}
	}

	window->surface = wl_compositor_create_surface(display->compositor);
	visual = display->premultiplied_argb_visual;
	window->native =
		wl_egl_window_create(window->surface,
				     window->geometry.width,
				     window->geometry.height,
				     visual);
	window->egl_surface =
		eglCreateWindowSurface(display->egl.dpy,
				       display->egl.conf,
				       window->native,
				       NULL);

	wl_shell_set_toplevel(display->shell, window->surface);

	ret = eglMakeCurrent(window->display->egl.dpy, window->egl_surface,
			     window->egl_surface, window->display->egl.ctx);
	assert(ret == EGL_TRUE);
}
/*
static void
redraw(struct wl_surface *surface, void *data, uint32_t time)
{
	struct window *window = data;
	static const GLfloat verts[3][2] = {
		{ -0.5, -0.5 },
		{  0.5, -0.5 },
		{  0,    0.5 }
	};
	static const GLfloat colors[3][3] = {
		{ 1, 0, 0 },
		{ 0, 1, 0 },
		{ 0, 0, 1 }
	};
	GLfloat angle;
	GLfloat rotation[4][4] = {
		{ 1, 0, 0, 0 },
		{ 0, 1, 0, 0 },
		{ 0, 0, 1, 0 },
		{ 0, 0, 0, 1 }
	};
	static const int32_t speed_div = 5;
	static uint32_t start_time = 0;

	if (start_time == 0)
		start_time = time;

	angle = ((time-start_time) / speed_div) % 360 * M_PI / 180.0;
	rotation[0][0] =  cos(angle);
	rotation[0][2] =  sin(angle);
	rotation[2][0] = -sin(angle);
	rotation[2][2] =  cos(angle);

	glUniformMatrix4fv(window->gl.rotation_uniform, 1, GL_FALSE,
			   (GLfloat *) rotation);

	glClearColor(0.0, 0.0, 0.0, 0.5);
	glClear(GL_COLOR_BUFFER_BIT);

	glVertexAttribPointer(window->gl.pos, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(window->gl.col, 3, GL_FLOAT, GL_FALSE, 0, colors);
	glEnableVertexAttribArray(window->gl.pos);
	glEnableVertexAttribArray(window->gl.col);

	glDrawArrays(GL_TRIANGLES, 0, 3);

	glDisableVertexAttribArray(window->gl.pos);
	glDisableVertexAttribArray(window->gl.col);

	glFlush();

	eglSwapBuffers(window->display->egl.dpy, window->egl_surface);
	wl_display_frame_callback(window->display->display,
				  window->surface,
				  redraw, window);
}*/

static void
compositor_handle_visual(void *data,
			 struct wl_compositor *compositor,
			 uint32_t id, uint32_t token)
{
	struct display *d = data;

	switch (token) {
	case WL_COMPOSITOR_VISUAL_PREMULTIPLIED_ARGB32:
		d->premultiplied_argb_visual =
			wl_visual_create(d->display, id, 1);
		break;
	}
}

static const struct wl_compositor_listener compositor_listener = {
	compositor_handle_visual,
};

static void
display_handle_global(struct wl_display *display, uint32_t id,
		      const char *interface, uint32_t version, void *data)
{
	struct display *d = data;

	if (strcmp(interface, "wl_compositor") == 0) {
		d->compositor = wl_compositor_create(display, id, 1);
		wl_compositor_add_listener(d->compositor,
					   &compositor_listener, d);
	} else if (strcmp(interface, "wl_shell") == 0) {
		d->shell = wl_shell_create(display, id, 1);
	}
}

static int
event_mask_update(uint32_t mask, void *data)
{
	struct display *d = data;

	d->mask = mask;

	return 0;
}

struct rwl_connection;

typedef int (*dispatch_func_ptr)(struct rwl_connection *rc, int epoll_fd);


struct wl_buffer {
	char data[4096];
	int head, tail;
};

struct rwl_connection{
	struct wl_buffer in, out;
	struct wl_buffer fds_in, fds_out;
	int fd_to;
	int fd_from;
	dispatch_func_ptr dispatch;
	struct display *display;
};

int
rwl_client_init( struct rwl_connection *rc, int epoll_fd)
{
	struct epoll_event ep_remote, ep_local;
	struct rwl_connection *local_rc, *remote_rc;
	memset(local_rc, 0, sizeof *local_rc);
	memset(remote_rc, 0, sizeof *remote_rc);

	//create new remote socket/read in
	struct sockaddr_storage incoming_addr;
	socklen_t size;
	int remote_fd = accept(rc->fd_from,(struct sockaddr *) &incoming_addr, size);

	//create new socket to compositor
	struct display display;
	memset(&display, 0, sizeof display);
	display.display = wl_display_connect(NULL);

	//pack and send rc for remote connection
	remote_rc->fd_to = rwl_get_display_fd(&(display.display));
	remote_rc->fd_from = remote_fd;
	//where do I store display?
	remote_rc->dispatch = rwl_client_forward;

	ep_remote.data.ptr = remote_rc;
	epoll_ctl(epoll_fd,EPOLL_CTL_ADD, remote_rc->fd_from, &ep_remote);
	rwl_client_forward(remote_rc);
	
	//pack and send rc for local connection(to compositor)
	local_rc->fd_from = remote_rc->fd_to;
	local_rc->fd_to = remote_rc->fd_from;
	local_rc->dispatch = rwl_client_forward;

	ep_local.data.ptr = local_rc;
	epoll_ctl(epoll_fd,EPOLL_CTL_ADD, local_rc->fd_from, &ep_local);

	rwl_client_forward(local_rc);

	
	
	return 0;
}

int
main(int argc, char **argv)
{
	struct display display = { 0 };
	struct window  window  = { 0 };

	memset(&display, 0, sizeof display);
	memset(&window,  0, sizeof window);

	window.display = &display;
	window.geometry.width  = 250;
	window.geometry.height = 250;

	display.display = wl_display_connect(NULL);
	assert(display.display);

	wl_display_add_global_listener(display.display,
				       display_handle_global, &display);

	wl_display_get_fd(display.display, event_mask_update, &display);

	init_egl(&display);
	create_surface(&window);
	init_gl(&window);


	//I need a new struct, something with a connection, and another fd
	int err, fd = 0, epoll_fd = 0, len, i = 0;
	struct addrinfo *local_address_info;
	
	epoll_fd = epoll_create(16);

	//set up server connection
	if((err = getaddrinfo(NULL, "35000", NULL, &local_address_info)) != 0) {
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(err));
		return EXIT_FAILURE;
	}
	fd = socket(local_address_info->ai_family, 
			local_address_info->ai_socktype,
			local_address_info->ai_protocol);
	bind(fd,local_address_info->ai_addr,local_address_info->ai_addrlen);
	listen(fd,8);
	//add to epoll
	struct epoll_event ep[32];
	struct epoll_event ep_in;
	struct rwl_connection rc, *rc_out;

	memset(&rc, 0, sizeof rc);

	rc.fd_from = fd;
	rc.dispatch = rwl_client_init;
	ep_in.data.ptr = &rc;
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ep_in);
	printf("Connected\n");//jkdebug
	while (true){
		len = epoll_wait(epoll_fd, ep, 32, -1);
		printf("epoll read\n");
		for(i = 0; i < len; i++){
			rc_out = ep[i].data.ptr;
			rc_out->dispatch(rc_out, epoll_fd);
		}
	}

	return 0;
}
