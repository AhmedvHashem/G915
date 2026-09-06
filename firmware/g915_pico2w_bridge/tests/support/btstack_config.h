#ifndef G915_TEST_BTSTACK_CONFIG_H
#define G915_TEST_BTSTACK_CONFIG_H

// The native decoder test needs only BTstack's HID parser. Keep logging and
// platform-specific features disabled so the parser stays host-portable.
#define HCI_ACL_PAYLOAD_SIZE 1024

#endif
