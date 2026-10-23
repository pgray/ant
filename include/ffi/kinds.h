#ifdef ANT_FFI_KIND
ANT_FFI_KIND(library,  "FFIL", "FFILibrary",  object_proto)
ANT_FFI_KIND(function, "FFIF", "FFIFunction", function_proto)
ANT_FFI_KIND(pointer,  "FFIP", "FFIPointer",  object_proto)
ANT_FFI_KIND(callback, "FFIC", "FFICallback", object_proto)
#undef ANT_FFI_KIND
#endif
