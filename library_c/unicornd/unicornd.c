/*
 * Copyright (C) 2014 jibi <jibi@paranoici.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h> 
#include <sys/un.h>

#include "ws2811.h"

#define TARGET_FREQ    WS2811_TARGET_FREQ
#define GPIO_PIN       18
#define DMA            10

#define WIDTH          4
#define HEIGHT         16
#define LED_COUNT      (WIDTH * HEIGHT)

ws2811_t ledstring =
{
    .freq = TARGET_FREQ,
    .dmanum = DMA,
    .channel =
    {
        [0] =
        {
            .gpionum    = GPIO_PIN,
            .count      = LED_COUNT,
            .invert     = 0,
            .brightness = 255,
            .strip_type = SK6812_STRIP_RGBW,
        }
    }
};

static inline
int
get_pixel_pos(uint8_t x, uint8_t y)
{
	int map[4][16] = {
		{  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15},
		{ 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16},
		{ 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47},
		{ 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48}
	};

	return map[x][y];
}

static inline
void
set_pixel_color(int pixel, int w, int r, int g, int b)
{
    ledstring.channel[0].leds[pixel] = (w << 24) | (r << 16) | (g << 8) | b;
}


static inline
void
clear_led_buffer(void)
{
    int i;

    for(i = 0; i < LED_COUNT; i++){
        set_pixel_color(i, 0, 0, 0, 0);
    }
}

static inline
void
set_brightness(int b)
{
    ledstring.channel[0].brightness = b;
}

static inline
void
show(){
    ws2811_render(&ledstring);
}

static inline
void
unicornd_exit(int status)
{
	clear_led_buffer();
	show();

	ws2811_fini(&ledstring);

	exit(status);
}

static inline
void
init_unicorn_hat(void)
{
	int i;
	struct sigaction sa;

	for (i = 0; i < LED_COUNT; i++) {
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = unicornd_exit;
		sigaction(i, &sa, NULL);
	}

	setvbuf(stdout, NULL, _IONBF, 0);

	if (ws2811_init(&ledstring) < 0) {
		exit(1);
	}

	clear_led_buffer();
	set_brightness(20);
}

#define SOCK_PATH "/var/run/unicornd.socket"

#define	UNICORND_CMD_SET_BRIGHTNESS 0
#define	UNICORND_CMD_SET_PIXEL      1
#define	UNICORND_CMD_SET_ALL_PIXELS 2
#define	UNICORND_CMD_SHOW           3

#define recv_or_return(socket, buf, len, flags) \
{                                               \
	int _ret;                               \
	_ret = recv(socket, buf, len, flags);   \
                                                \
	if (_ret <= 0) {                        \
		close(socket);                  \
		return;                         \
	}                                       \
}

typedef struct col_s {
	uint8_t w;
	uint8_t r;
	uint8_t g;
	uint8_t b;
} col_t;

typedef struct pos_s {
	uint8_t x;
	uint8_t y;
} pos_t;

static
int
setup_listen_socket(void)
{
	int listen_socket;
	int ret;
	socklen_t len;
	struct sockaddr_un local;

	listen_socket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listen_socket == -1) {
		fprintf(stderr, "cannot create unix socket");
		exit(1);
	}

	unlink(SOCK_PATH);

	local.sun_family = AF_UNIX;
	strcpy(local.sun_path, SOCK_PATH);
	len = strlen(local.sun_path) + sizeof(local.sun_family);

	ret = bind(listen_socket, (struct sockaddr *) &local, len);
	if (ret == -1) {
		fprintf(stderr, "cannot bind socket");
		exit(1);
	}

	chmod(SOCK_PATH, 0777);

	ret = listen(listen_socket, 4);
	if (ret == -1) {
		fprintf(stderr, "cannot listen on socket");
		exit(1);
	}

	return listen_socket;
}

static
int
do_accept(int listen_socket) {
	struct sockaddr_un client;
	int client_socket;
	socklen_t len;

	len = sizeof(client);

	client_socket = accept(listen_socket, (struct sockaddr *)&client, &len);
	if (client_socket == -1) {
		fprintf(stderr, "cannot accept client connection");
		exit(1);
	}

	return client_socket;
}

static
void
handle_client(int client_socket) {
	uint8_t cmd;

	char bright;

	pos_t pos;
	col_t col;

	col_t pixels[LED_COUNT];
	
	int x, y;

	while (true) {
		recv_or_return(client_socket, &cmd, sizeof(char), 0);

		switch (cmd) {
			case UNICORND_CMD_SET_BRIGHTNESS:

				recv_or_return(client_socket, &bright, sizeof(char), 0);

				set_brightness(bright);
				break;

			case UNICORND_CMD_SET_PIXEL:

				recv_or_return(client_socket, &pos, sizeof(pos_t), 0);
				recv_or_return(client_socket, &col, sizeof(col_t), 0);

				set_pixel_color(get_pixel_pos(pos.x, pos.y), col.w, col.r, col.g, col.b);
				break;

			case UNICORND_CMD_SET_ALL_PIXELS:
				recv_or_return(client_socket, &pixels, LED_COUNT * sizeof(col_t), 0);

				for (x = 0; x < WIDTH; x++) {
					for (y = 0; y < HEIGHT; y++) {
						col_t *col = &pixels[x * HEIGHT + y];
						set_pixel_color(get_pixel_pos(x, y), col->w, col->r, col->g, col->b);
					}
				}

				break;

			case UNICORND_CMD_SHOW:

				show();
				break;

			default:

				close(client_socket);
				return;
		}
	}
}

int
main(void)
{
	int listen_socket, client_socket;

	init_unicorn_hat();
	listen_socket = setup_listen_socket();

	while (true) {
		client_socket = do_accept(listen_socket);

		if (client_socket != -1) {
			handle_client(client_socket);
		}
	}

	return 0;
}

