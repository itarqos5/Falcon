// The zip reader shared with the game (src/assets/zip_archive.cpp) inflates entries with stb_image's raw-deflate decoder, and the player's
// avatar (avatar.cpp) is read from a skin PNG and written as one; this is where those libraries' code lives in the launcher.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
