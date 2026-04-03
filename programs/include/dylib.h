#pragma once

int dl_open(const char *path);
void *dl_sym(int handle, const char *name);
void dl_close(int handle);
const char *dl_error(void);

