// Zone pieces onto a strip. A zone is rendered once as one strip of zone_len
// pixels; each piece is a run of physical pixels on this controller's strip.
//   chain: the piece shows zone pixels zone_offset .. zone_offset + count - 1
//   copy:  every piece shows zone pixels 0 .. count - 1
// A reversed piece runs the other way: its first physical pixel shows the last
// of its zone pixels. Zone pixels past zone_len and strip pixels past strip_len
// are left alone.
#include <string.h>
#include "nl_engine.h"

extern "C" void nl_layout_apply(const uint8_t *zone_rgb, uint16_t zone_len, uint8_t copy,
                                const nl_piece *pieces, int n_pieces,
                                uint8_t *strip_rgb, uint16_t strip_len) {
  for (int p = 0; p < n_pieces; p++) {
    const nl_piece &pc = pieces[p];
    const int first = copy ? 0 : pc.zone_offset;
    for (int k = 0; k < pc.light_count; k++) {
      int s = pc.start_pixel + k;
      int z = first + (pc.reversed ? pc.light_count - 1 - k : k);
      if (s >= strip_len || z >= zone_len) continue;
      memcpy(strip_rgb + s * 3, zone_rgb + z * 3, 3);
    }
  }
}
