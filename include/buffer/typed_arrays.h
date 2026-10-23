#ifdef BUFFER_TYPED_ARRAY
BUFFER_TYPED_ARRAY(INT8,          Int8,         int8_t,   INT,    (double),           (int8_t)js_to_int32)
BUFFER_TYPED_ARRAY(UINT8,         Uint8,        uint8_t,  INT,    (double),           (uint8_t)js_to_uint32)
BUFFER_TYPED_ARRAY(UINT8_CLAMPED, Uint8Clamped, uint8_t,  INT,    (double),           typedarray_to_uint8_clamped)
BUFFER_TYPED_ARRAY(INT16,         Int16,        int16_t,  INT,    (double),           (int16_t)js_to_int32)
BUFFER_TYPED_ARRAY(UINT16,        Uint16,       uint16_t, INT,    (double),           (uint16_t)js_to_uint32)
BUFFER_TYPED_ARRAY(INT32,         Int32,        int32_t,  INT,    (double),           js_to_int32)
BUFFER_TYPED_ARRAY(UINT32,        Uint32,       uint32_t, INT,    (double),           js_to_uint32)
BUFFER_TYPED_ARRAY(FLOAT16,       Float16,      uint16_t, FLOAT,  half_to_double,     double_to_half)
BUFFER_TYPED_ARRAY(FLOAT32,       Float32,      float,    FLOAT,  (double),           (float))
BUFFER_TYPED_ARRAY(FLOAT64,       Float64,      double,   FLOAT,  (double),           (double))
BUFFER_TYPED_ARRAY(BIGINT64,      BigInt64,     int64_t,  BIGINT, bigint_from_int64,  (int64_t))
BUFFER_TYPED_ARRAY(BIGUINT64,     BigUint64,    uint64_t, BIGINT, bigint_from_uint64, (uint64_t))
#undef BUFFER_TYPED_ARRAY
#endif
