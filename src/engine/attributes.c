#include "pd_engine.h"

bool pd_is_piece_id(int value) {
    return value >= 0 && value < PD_PIECE_COUNT;
}

bool pd_decode_piece(int piece, pd_attributes_t *out) {
    if (!pd_is_piece_id(piece) || !out) {
        return false;
    }
    out->tall = (piece & 0x8) != 0;
    out->light = (piece & 0x4) != 0;
    out->square = (piece & 0x2) != 0;
    out->solid = (piece & 0x1) != 0;
    return true;
}

int pd_encode_piece(pd_attributes_t attrs) {
    return (attrs.tall ? 0x8 : 0) |
           (attrs.light ? 0x4 : 0) |
           (attrs.square ? 0x2 : 0) |
           (attrs.solid ? 0x1 : 0);
}

const char *pd_piece_short_label(int piece, char buf[8]) {
    if (!buf) {
        return NULL;
    }
    pd_attributes_t attrs;
    if (!pd_decode_piece(piece, &attrs)) {
        buf[0] = '-';
        buf[1] = '-';
        buf[2] = '-';
        buf[3] = '-';
        buf[4] = '\0';
        return buf;
    }
    buf[0] = attrs.tall ? 'T' : 'S';
    buf[1] = attrs.light ? 'L' : 'D';
    buf[2] = attrs.square ? 'Q' : 'R';
    buf[3] = attrs.solid ? 'S' : 'H';
    buf[4] = '\0';
    return buf;
}
