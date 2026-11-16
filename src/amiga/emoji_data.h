/* AmiRocket - pixel-art emoji bitmaps (generated, see tools/emoji). */
#ifndef AR_EMOJI_DATA_H
#define AR_EMOJI_DATA_H

#define EMOJI_COLOURS 13
#define EMOJI_NSIZES  5

extern const unsigned long emoji_palette[EMOJI_COLOURS];  /* 0xRRGGBB, [0] transparent */
extern const int emoji_sizes[EMOJI_NSIZES];                 /* square sizes in pixels */
/* size*size palette indices for emoji idx, or NULL. */
const unsigned char *emoji_bitmap(int idx, int size_index);

#endif
