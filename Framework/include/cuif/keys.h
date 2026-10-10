#ifndef CUIF_KEYS_H
#define CUIF_KEYS_H

/*
 * Keyboard input (#131). Values mirror lpi.gui.keyboard.v1's LPI_KEY_* /
 * LPI_MOD_* / LPI_VK_* in PluginABI/include/lpi/lpi.h bit for bit, so the
 * LPI wrapper forwards them unchanged (it static_asserts this); the Win32
 * backend maps WM_KEYDOWN/WM_KEYUP/WM_CHAR onto them.
 *
 * Event types: DOWN/UP carry a CUIF_VK_* code; CHAR carries a UTF-32 code
 * point (sent after DOWN for a single printable character with no
 * Ctrl/Alt/Meta held). Auto-repeat is DOWN (then CHAR) again with no UP.
 */
enum {
    CUIF_KEY_DOWN = 0,
    CUIF_KEY_UP   = 1,
    CUIF_KEY_CHAR = 2
};

enum {
    CUIF_MOD_SHIFT = 1u << 0,
    CUIF_MOD_CTRL  = 1u << 1,
    CUIF_MOD_ALT   = 1u << 2,
    CUIF_MOD_META  = 1u << 3
};

/* Virtual keys. Digits and letters are their ASCII code ('0'..'9',
 * 'A'..'Z'); the rest are above 0x100 so no CHAR code point can collide. */
enum {
    CUIF_VK_NONE      = 0,
    CUIF_VK_SPACE     = 0x20,
    CUIF_VK_0 = '0', CUIF_VK_9 = '9',
    CUIF_VK_A = 'A', CUIF_VK_Z = 'Z',
    CUIF_VK_ENTER     = 0x101,
    CUIF_VK_ESCAPE    = 0x102,
    CUIF_VK_BACKSPACE = 0x103,
    CUIF_VK_TAB       = 0x104,
    CUIF_VK_DELETE    = 0x105,
    CUIF_VK_HOME      = 0x106,
    CUIF_VK_END       = 0x107,
    CUIF_VK_LEFT      = 0x108,
    CUIF_VK_RIGHT     = 0x109,
    CUIF_VK_UP        = 0x10A,
    CUIF_VK_DOWN      = 0x10B,
    CUIF_VK_MINUS     = 0x10C,
    CUIF_VK_PERIOD    = 0x10D,
    CUIF_VK_COMMA     = 0x10E
};

#endif /* CUIF_KEYS_H */
