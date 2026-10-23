#ifdef ANT_FFI_TYPE
ANT_FFI_TYPE(VOID,    void,    "void",    &ffi_type_void,    void,     NONE)
ANT_FFI_TYPE(INT8,    int8,    "int8",    &ffi_type_sint8,   int8_t,   INT)
ANT_FFI_TYPE(INT16,   int16,   "int16",   &ffi_type_sint16,  int16_t,  INT)
ANT_FFI_TYPE(INT,     int,     "int",     &ffi_type_sint32,  int32_t,  INT)
ANT_FFI_TYPE(INT64,   int64,   "int64",   &ffi_type_sint64,  int64_t,  WIDE)
ANT_FFI_TYPE(UINT8,   uint8,   "uint8",   &ffi_type_uint8,   uint8_t,  INT)
ANT_FFI_TYPE(UINT16,  uint16,  "uint16",  &ffi_type_uint16,  uint16_t, INT)
ANT_FFI_TYPE(UINT64,  uint64,  "uint64",  &ffi_type_uint64,  uint64_t, WIDE)
ANT_FFI_TYPE(FLOAT,   float,   "float",   &ffi_type_float,   float,    REAL)
ANT_FFI_TYPE(DOUBLE,  double,  "double",  &ffi_type_double,  double,   REAL)
ANT_FFI_TYPE(POINTER, pointer, "pointer", &ffi_type_pointer, void *,   PTR)
ANT_FFI_TYPE(STRING,  string,  "string",  &ffi_type_pointer, char *,   STR)
ANT_FFI_TYPE(SPREAD,  spread,  "...",     NULL,              void,     NONE)
#undef ANT_FFI_TYPE
#endif
