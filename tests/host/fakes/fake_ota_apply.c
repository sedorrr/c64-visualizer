/* fake of ota_apply.c: the real one overwrites the running firmware and resets (RAM code, registers) */
#include <stdint.h>
uint32_t OTA_APPLIED_OFF = 0, OTA_APPLIED_SIZE = 0;
void ota_apply_now(uint32_t staged_offset, uint32_t size, uint8_t *ram_buf_4k) {
    (void)ram_buf_4k; OTA_APPLIED_OFF = staged_offset; OTA_APPLIED_SIZE = size;
}
/* audio_capture.c is not part of the host build; net.c shows this counter on the status page */
uint32_t audio_stream_waits(void) { return 0; }
int LAST_HINT = -1;
void audio_stereo_hint(int chips) { LAST_HINT = chips; }
