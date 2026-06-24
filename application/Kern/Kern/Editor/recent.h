/* recent.h — most-recently-used file list (for C-x b buffer switching). */
#ifndef RECENT_H
#define RECENT_H

#define RECENT_MAX 32

/* Move `path` to the front of the MRU list (dedup, capped at RECENT_MAX). */
void recent_push(const char *path);

/* Number of entries currently tracked (0..RECENT_MAX). */
int recent_count(void);

/* MRU entry `i` (0 = most recent); NULL if out of range. */
const char *recent_get(int i);

/* Clear the list (used by tests; also a natural "forget recents"). */
void recent_reset(void);

/* Basename of a path — the part after the last '/', or the whole string. */
const char *path_base(const char *p);

#endif /* RECENT_H */
