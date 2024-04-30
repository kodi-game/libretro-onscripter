#include <SDL_libretro.h>
#include <retro_miscellaneous.h>
#include <file/file_path.h>
#include <onscripter/ONScripter.h>
extern "C" {
#include <llco.h>
}

retro_audio_sample_batch_t SDL_libretro_audio_batch_cb;
retro_input_state_t SDL_libretro_input_state_cb;
SDL_AudioSpec *SDL_libretro_audio_spec = NULL;

static void fallback_log(enum retro_log_level level, const char *fmt, ...);
static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;

static retro_log_printf_t log_cb = fallback_log;
static retro_environment_t environ_cb;
static ONScripter ons;
static struct llco *retro_co;
static struct llco *ons_co;
static bool audio_enabled = true;


void SDL_libretro_co_yield(void)
{
  llco_switch(retro_co, false);
}

void SDL_libretro_video_refresh()
{
  static SDL_Surface *screen = SDL_GetVideoSurface();
  video_cb(screen->pixels, screen->w, screen->h, screen->pitch);
}

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
  (void)level;
  va_list va;
  va_start(va, fmt);
  vfprintf(stderr, fmt, va);
  va_end(va);
}

unsigned retro_api_version(void)
{
  return RETRO_API_VERSION;
}

void retro_set_environment(retro_environment_t cb)
{
  static struct retro_log_callback log;
  environ_cb = cb;
  if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
    log_cb = log.log;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
  video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
  SDL_libretro_audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
  input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
  SDL_libretro_input_state_cb = cb;
}

void retro_get_system_info(struct retro_system_info *info)
{
  info->need_fullpath = true;
  info->valid_extensions = "txt|dat|___|ons";
  info->library_version = "0.2";
  info->library_name = "onscripter";
  info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
  int width = ons.getWidth();
  int height = ons.getHeight();
  info->geometry.base_width = width;
  info->geometry.base_height = height;
  info->geometry.max_width = width;
  info->geometry.max_height = height;
  info->timing.fps = 60.0;
  info->timing.sample_rate = 44100.0;
}

static void ons_main(void *udata)
{
  ons_co = llco_current();
  llco_switch(retro_co, false);
  if (ons.init()) {
    log_cb(RETRO_LOG_ERROR, "Failed to initialize ONScripter\n");
    return;
  }
  SDL_ShowCursor(SDL_DISABLE);
  ons.executeLabel();
  llco_switch(retro_co, true);
}

static void ons_cleanup(void *stack, size_t stack_size, void *udata)
{
  free(stack);
}

static void audio_cb(void)
{
  // XXX: convert audio format?
  static const size_t frames = 256;
  static const size_t len = frames * 4;
  static int16_t stream[frames * 2];

  SDL_AudioSpec *spec = SDL_libretro_audio_spec;
  if (spec && SDL_GetAudioStatus() == SDL_AUDIO_PLAYING && audio_enabled) {
    memset(stream, 0, len);
    spec->callback(NULL, (uint8_t *)stream, len);
    SDL_libretro_audio_batch_cb(stream, frames);
  }
}

static void audio_set_state_cb(bool enabled)
{
  audio_enabled = enabled;
}

void retro_init(void)
{
  const size_t ons_stacksz = 65536*8;
  enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_XRGB8888;
  struct retro_audio_callback audio = {
    .callback = audio_cb,
    .set_state = audio_set_state_cb,
  };
  environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixfmt);
  if (!environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK, &audio)) {
    log_cb(RETRO_LOG_ERROR, "SET_AUDIO_CALLBACK failed, no audio...\n");
  }
  retro_co = llco_current();

  struct llco_desc desc = {
    .stack = malloc(ons_stacksz),
    .stack_size = ons_stacksz,
    .entry = ons_main,
    .cleanup = ons_cleanup,
  };
  llco_start(&desc, false);
}

bool retro_load_game(const struct retro_game_info *game)
{
  if (!game)
    return false;

  char archive_path[PATH_MAX_LENGTH];
  fill_pathname_basedir(archive_path, game->path, sizeof(archive_path));
  ons.setArchivePath(archive_path);

  if (ons.openScript() != 0) {
    return false;
  }
  return true;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
}

void retro_deinit(void)
{
}

void retro_reset(void)
{
  ons.resetCommand();
}

void retro_run(void)
{
  llco_switch(ons_co, false);
  input_poll_cb();
  SDL_libretro_video_refresh();

}

size_t retro_serialize_size(void)
{
  return 0;
}

bool retro_serialize(void *data, size_t size)
{
  return false;
}

bool retro_unserialize(const void *data, size_t size)
{
  return false;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
  return false;
}

void retro_unload_game(void)
{
  SDL_Quit();
}

unsigned retro_get_region(void)
{
  return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id)
{
  return 0;
}

size_t retro_get_memory_size(unsigned id)
{
  return 0;
}


#ifdef ANDROID
extern "C" {
#undef fseek
int fseek_ons(FILE *stream, long offset, int whence) {
  return fseek(stream, offset, whence);
}

#undef ftell
long ftell_ons(FILE *stream) {
  return ftell(stream);
}

#undef fgetc
int fgetc_ons(FILE *stream) {
  return fgetc(stream);
}

#undef fgets
char *fgets_ons(char *s, int size, FILE *stream) {
  return fgets(s, size, stream);
}

#undef fread
size_t fread_ons(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  return fread(ptr, size, nmemb, stream);
}

#undef fopen
FILE *fopen_ons(const char *str, const char *mode) {
  return fopen(str, mode);
}

#undef mkdir
extern int mkdir(const char *pathname, mode_t mode);
int mkdir_ons(const char *pathname, mode_t mode) {
  return mkdir(pathname, mode);
}

void playVideoAndroid(const char *filename) {}
int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  log_cb(RETRO_LOG_INFO, fmt, va);
  va_end(va);
  return 0;
}
}
#endif
