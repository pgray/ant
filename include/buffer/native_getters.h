#ifdef BUFFER_SIZE_GETTER
BUFFER_SIZE_GETTER(typedarray_length_getter,             js_typedarray_length_getter,      TYPEDARRAY,  TypedArrayData,  REF_SLOW, views_slow,  length)
BUFFER_SIZE_GETTER(typedarray_byte_length_getter,        js_typedarray_byteLength_getter,  TYPEDARRAY,  TypedArrayData,  REF_SLOW, views_slow,  byte_length)
BUFFER_SIZE_GETTER(typedarray_byte_offset_getter,        js_typedarray_byteOffset_getter,  TYPEDARRAY,  TypedArrayData,  REF_SLOW, views_slow,  byte_offset)
BUFFER_SIZE_GETTER(dataview_byte_length_getter,          js_dataview_byteLength_getter,    DATAVIEW,    DataViewData,    REF_SLOW, is_detached, byte_length)
BUFFER_SIZE_GETTER(dataview_byte_offset_getter,          js_dataview_byteOffset_getter,    DATAVIEW,    DataViewData,    REF_SLOW, is_detached, byte_offset)
BUFFER_SIZE_GETTER(arraybuffer_byte_length_getter,       js_arraybuffer_byteLength_getter, ARRAYBUFFER, ArrayBufferData, SELF,     is_detached, length)
BUFFER_SIZE_GETTER(sharedarraybuffer_byte_length_getter, js_arraybuffer_byteLength_getter, ARRAYBUFFER, ArrayBufferData, SELF,     is_detached, length)
#undef BUFFER_SIZE_GETTER
#endif

#ifdef BUFFER_SLOT_GETTER
BUFFER_SLOT_GETTER(typedarray_buffer_getter, js_typedarray_buffer_getter, TYPEDARRAY)
BUFFER_SLOT_GETTER(dataview_buffer_getter,   js_dataview_buffer_getter,   DATAVIEW)
#undef BUFFER_SLOT_GETTER
#endif
