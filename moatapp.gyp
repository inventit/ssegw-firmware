{
  'variables': {
    'sseutils_root': './moat-c-utils',
  },
  'includes': [
    'common.gypi',
    'config.gypi',
    './moat-c-utils/sseutils.gypi',
  ],
  'targets': [
    # your M2M/IoT application
    {
      'target_name': '<(package_name)',
      'sources': [
        '<@(sseutils_src)',
        'src/<(package_name).c',
        'src/firmware/download_info_model.c',
        'src/firmware/firmware_package.c',
        'src/firmware/firmware_updater.c',
       ],
      'product_prefix': '',
      'type': 'shared_library',
      'cflags': [ '-fPIC' ],
      'include_dirs' : [
        '<(sseutils_include)',
      ],
      'libraries': [
      ],
      'dependencies': [
      ],
    },

  ],
}
