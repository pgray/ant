project(
  'google-brotli',
  'c',
  license: 'MIT',
  version: '1.2.1',
  meson_version: '>= 0.54',
)

c_args = ['-D_POSIX_SOURCE']
cc = meson.get_compiler('c')

dep_m = cc.find_library('m', required: false)
has_log2 = cc.has_function('log2', dependencies: dep_m)
c_args += ['-DBROTLI_HAVE_LOG2=@0@'.format(has_log2.to_int())]

inc = include_directories('c/include')

if host_machine.system() == 'linux'
  add_project_arguments('-DOS_LINUX', language: 'c')
elif host_machine.system() == 'freebsd'
  add_project_arguments('-DOS_FREEBSD', language: 'c')
elif host_machine.system() == 'darwin'
  add_project_arguments('-DOS_MACOSX', language: 'c')
endif

common_src = files(
  'c/common/constants.c',
  'c/common/context.c',
  'c/common/dictionary.c',
  'c/common/platform.c',
  'c/common/shared_dictionary.c',
  'c/common/transform.c',
)

libbrotli_common = static_library(
  'brotli_common',
  common_src,
  gnu_symbol_visibility: 'hidden',
  c_args: c_args,
  include_directories: inc,
)

brotli_common_dep = declare_dependency(
  link_with: libbrotli_common,
  include_directories: inc,
)

meson.override_dependency('libbrotlicommon', brotli_common_dep)

dec_src = files(
  'c/dec/bit_reader.c',
  'c/dec/decode.c',
  'c/dec/huffman.c',
  'c/dec/prefix.c',
  'c/dec/state.c',
  'c/dec/static_init.c',
)

libbrotli_decoder = static_library(
  'brotli_decoder',
  dec_src,
  gnu_symbol_visibility: 'hidden',
  c_args: c_args,
  dependencies: brotli_common_dep,
)

enc_src = files(
  'c/enc/backward_references.c',
  'c/enc/backward_references_hq.c',
  'c/enc/bit_cost.c',
  'c/enc/block_splitter.c',
  'c/enc/brotli_bit_stream.c',
  'c/enc/cluster.c',
  'c/enc/command.c',
  'c/enc/compound_dictionary.c',
  'c/enc/compress_fragment.c',
  'c/enc/compress_fragment_two_pass.c',
  'c/enc/dictionary_hash.c',
  'c/enc/encode.c',
  'c/enc/encoder_dict.c',
  'c/enc/entropy_encode.c',
  'c/enc/fast_log.c',
  'c/enc/histogram.c',
  'c/enc/literal_cost.c',
  'c/enc/memory.c',
  'c/enc/metablock.c',
  'c/enc/static_dict.c',
  'c/enc/static_dict_lut.c',
  'c/enc/static_init.c',
  'c/enc/utf8_util.c',
)

libbrotli_encoder = static_library(
  'brotli_encoder',
  enc_src,
  gnu_symbol_visibility: 'hidden',
  c_args: c_args,
  dependencies: [dep_m, brotli_common_dep],
)

brotli_encoder_dep = declare_dependency(
  link_with: libbrotli_encoder,
  dependencies: brotli_common_dep,
)

meson.override_dependency('libbrotlienc', brotli_encoder_dep)

brotli_decoder_dep = declare_dependency(
  link_with: libbrotli_decoder,
  dependencies: brotli_common_dep,
)

meson.override_dependency('libbrotlidec', brotli_decoder_dep)
