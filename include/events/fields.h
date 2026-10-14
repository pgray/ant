#ifdef ANT_EVENT_FIELD
ANT_EVENT_FIELD(CUSTOM,            detail,      0)
ANT_EVENT_FIELD(ERROR,             message,     0)
ANT_EVENT_FIELD(ERROR,             filename,    1)
ANT_EVENT_FIELD(ERROR,             lineno,      2)
ANT_EVENT_FIELD(ERROR,             colno,       3)
ANT_EVENT_FIELD(ERROR,             error,       4)
ANT_EVENT_FIELD(PROMISE_REJECTION, promise,     0)
ANT_EVENT_FIELD(PROMISE_REJECTION, reason,      1)
ANT_EVENT_FIELD(MESSAGE,           data,        0)
ANT_EVENT_FIELD(MESSAGE,           origin,      1)
ANT_EVENT_FIELD(MESSAGE,           lastEventId, 2)
ANT_EVENT_FIELD(CLOSE,             wasClean,    0)
ANT_EVENT_FIELD(CLOSE,             code,        1)
ANT_EVENT_FIELD(CLOSE,             reason,      2)
#undef ANT_EVENT_FIELD
#endif
