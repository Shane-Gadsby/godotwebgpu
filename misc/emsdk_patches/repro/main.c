/**************************************************************************/
/*  main.c                                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

// Reproduces the Emscripten dylink + pthread ASM_CONSTS initialization race
// (Task 12 bug #2 / Task 14 subtask 1.5). A side module carrying an EM_ASM is
// instantiated on a *freshly spawned* pthread worker -- not one taken from the
// pre-spawned pool, which loads its side modules during startup and so never hits
// the race. On an unpatched toolchain the new worker's own `var ASM_CONSTS = {...}`
// initializer has not run by the time the side module's addEmAsm() does
// `ASM_CONSTS[start] = ...`, which throws on `undefined`.
//
// main() must NOT join the thread: the worker cannot start until the main thread
// returns to the browser event loop, so joining here deadlocks instead of racing.
#include <emscripten.h>
#include <pthread.h>
#include <stdio.h>

extern int side_value(void);

static void *worker(void *arg) {
	(void)arg;
	int v = side_value();
	printf("WORKER_RESULT %d\n", v);
	return NULL;
}

int main(void) {
	printf("MAIN_START\n");
	pthread_t t;
	if (pthread_create(&t, NULL, worker, NULL) != 0) {
		printf("REPRO_FAIL pthread_create\n");
		return 1;
	}
	pthread_detach(t);
	printf("MAIN_DONE\n");
	return 0;
}
