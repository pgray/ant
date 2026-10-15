#ifdef REGEXP_FLAG
REGEXP_FLAG(HAS_INDICES, hasIndices,  'd')
REGEXP_FLAG(GLOBAL,      global,      'g')
REGEXP_FLAG(IGNORE_CASE, ignoreCase,  'i')
REGEXP_FLAG(MULTILINE,   multiline,   'm')
REGEXP_FLAG(DOTALL,      dotAll,      's')
REGEXP_FLAG(UNICODE,     unicode,     'u')
REGEXP_FLAG(UNICODE_SET, unicodeSets, 'v')
REGEXP_FLAG(STICKY,      sticky,      'y')
#undef REGEXP_FLAG
#endif
