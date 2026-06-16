#ifndef GLANCE_IMAGE_SIZE_H
#define GLANCE_IMAGE_SIZE_H

/* Read an image's pixel dimensions straight from its header — no decode, no
 * library — so the renderer can reserve a row count that matches the picture's
 * aspect ratio. Supports PNG, GIF, BMP, JPEG, and WebP; returns 1 and fills
 * w and h on success, 0 for an unreadable file or unrecognised format (the
 * caller then falls back to a default height). */
int img_pixel_size(const char *path, int *w, int *h);

#endif /* GLANCE_IMAGE_SIZE_H */
